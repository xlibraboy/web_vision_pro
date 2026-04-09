#include "CameraManager.h"
#include "../config/CameraConfig.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <memory>
#include <pylon/gige/GigETransportLayer.h>
#include <pylon/gige/BaslerGigEInstantCamera.h>
#include <QDir>

// Use Pylon namespace
using namespace Pylon;

namespace {
std::string normalizeMacAddress(const std::string& mac) {
    std::string normalized;
    normalized.reserve(mac.size());
    for (unsigned char ch : mac) {
        if (std::isxdigit(ch)) {
            normalized.push_back(static_cast<char>(std::toupper(ch)));
        }
    }
    return normalized;
}
}

// DeviceRemovalHandler Implementation
void CameraManager::DeviceRemovalHandler::OnCameraDeviceRemoved(Pylon::CInstantCamera& camera) {
    try {
        if (!manager_) return;

        std::cout << "[CameraManager] DEVICE REMOVAL EVENT: camera context="
                  << camera.GetCameraContext() << std::endl;

        // Camera context stores config array index in the per-camera runtime model.
        uint32_t configIdx = static_cast<uint32_t>(camera.GetCameraContext());

        // 1. Mark this camera as disconnected so the acquisition loop skips it gracefully.
        //    Do NOT touch acquiring_ — surviving cameras must keep streaming uninterrupted.
        {
            std::lock_guard<std::mutex> lock(manager_->disconnectedMutex_);
            manager_->disconnectedCameras_.insert(configIdx);
        }

        // 2. Blank ONLY the disconnected camera's UI tile.
        {
            std::lock_guard<std::mutex> lock(manager_->callbackMutex_);
            if (manager_->callback_) {
                if (configIdx < manager_->cameraRuntimes_.size()) {
                    manager_->callback_(static_cast<int>(configIdx), cv::Mat());
                }
            }
        }

        // 3. Launch the background recovery thread to wait for the camera to reappear and
        //    rebuild only the missing slot.  If a recovery is already running, it will
        //    handle the newly-disconnected camera on its next poll cycle.
        if (!manager_->recovering_) {
            if (manager_->recoveryThread_.joinable()) manager_->recoveryThread_.join();
            manager_->recoveryThread_ = std::thread(&CameraManager::recoveryLoop, manager_);
        }
    } catch (const std::exception& e) {
        std::cerr << "[CameraManager] Exception in DeviceRemovalHandler: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[CameraManager] Unknown exception in DeviceRemovalHandler" << std::endl;
    }
}

CameraManager::CameraManager(int numCameras) 
    : numCameras_(numCameras), acquiring_(false), recovering_(false), width_(782), height_(582), fps_(10.0), 
      defectDetectionEnabled_(false) {
    prevTempStatus_.assign(numCameras, TemperatureStatus::Unknown);
    // Pylon requires initialization
    
    // Check environment for emulation mode (set externally via docker-compose or workflow script)
    const char* pylonCamEmu = getenv("PYLON_CAMEMU");
    if (pylonCamEmu) {
        std::cout << "[CameraManager] PYLON_CAMEMU detected in environment (value=" << pylonCamEmu 
                  << "). Running in EMULATION mode." << std::endl;
    } else {
        std::cout << "[CameraManager] No PYLON_CAMEMU in environment. Searching for REAL cameras." << std::endl;
    }

    try {
        PylonInitialize();
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Failed to initialize Pylon: " << e.GetDescription() << std::endl;
    }

    // Use centralized camera labels from config
    for (int i = 0; i < numCameras_; ++i) {
        cameraLabels_.push_back(CameraConfig::getCameraLabel(i).toStdString());
        modelNames_.push_back("Unknown Model"); // Default
        snapshotRequests_.push_back(false);
        swGain_.push_back(1.0);
        swGamma_.push_back(1.0);
        swContrast_.push_back(1.0);
        lutCache_.push_back(cv::Mat());
        lutValid_.push_back(false);
    }
    cameraRuntimes_.resize(numCameras_);
}

CameraManager::~CameraManager() {
    stopAcquisition();
    try {
        PylonTerminate();
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Failed to terminate Pylon: " << e.GetDescription() << std::endl;
    }
}

bool CameraManager::attachConfiguredCamera(int configArrayIndex, const CameraInfo& camInfo,
                                           const Pylon::DeviceInfoList_t& devices,
                                           std::set<int>& claimedDeviceIndices,
                                           bool suppressBlank) {
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameraRuntimes_.size())) {
        return false;
    }

    CameraRuntime& runtime = cameraRuntimes_[configArrayIndex];
    runtime.configId = camInfo.id;
    runtime.source = camInfo.source;
    runtime.connected = false;
    runtime.targetDevice = Pylon::CDeviceInfo();
    runtime.camera.reset();

    cameraIndexToConfigId_[configArrayIndex] = camInfo.id;
    configArrayIndexToPylonIndex_[configArrayIndex] = -1;
    pylonIndexToConfigArrayIndex_[configArrayIndex] = configArrayIndex;

    if (camInfo.source == 2) {
        if (!suppressBlank) {
            clearCameraTile(configArrayIndex);
        }
        return false;
    }

    int matchedDeviceIndex = -1;
    for (int devIndex = 0; devIndex < static_cast<int>(devices.size()); ++devIndex) {
        if (claimedDeviceIndices.count(devIndex)) {
            continue;
        }

        const auto& dev = devices[devIndex];
        const bool isEmulatedDevice = (dev.GetDeviceClass() == "BaslerCamEmu");
        bool canMatch = false;

        if (camInfo.source == 0 && isEmulatedDevice) {
            canMatch = true;
        } else if (camInfo.source == 1 && !isEmulatedDevice && !camInfo.macAddress.isEmpty() &&
                   camInfo.macAddress != "None / Auto" &&
                   normalizeMacAddress(camInfo.macAddress.toStdString()) == normalizeMacAddress(dev.GetMacAddress().c_str())) {
            canMatch = true;
        }

        if (canMatch) {
            matchedDeviceIndex = devIndex;
            break;
        }
    }

    if (matchedDeviceIndex < 0) {
        std::cerr << "[CameraManager] WARNING: Could not find matching physical device for Camera ID "
                  << camInfo.id << " (Source: " << (camInfo.source == 0 ? "Emulated" : "Real") << ")" << std::endl;
        if (!suppressBlank) {
            clearCameraTile(configArrayIndex);
        }
        return false;
    }

    claimedDeviceIndices.insert(matchedDeviceIndex);
    const auto& matchedDevice = devices[matchedDeviceIndex];

    try {
        CTlFactory& tlFactory = CTlFactory::GetInstance();
        runtime.camera = std::make_unique<CInstantCamera>(tlFactory.CreateDevice(matchedDevice));
        runtime.camera->RegisterConfiguration(new DeviceRemovalHandler(this), RegistrationMode_Append, Cleanup_Delete);
        runtime.camera->SetCameraContext(configArrayIndex);
        runtime.targetDevice = matchedDevice;
        runtime.connected = true;

        configArrayIndexToPylonIndex_[configArrayIndex] = configArrayIndex;
        pylonIndexToConfigArrayIndex_[configArrayIndex] = configArrayIndex;

        std::cout << "[CameraManager] Config array index " << configArrayIndex
                  << " (Config ID " << camInfo.id << ") attached to device "
                  << matchedDevice.GetModelName() << std::endl;

        if (configArrayIndex < static_cast<int>(modelNames_.size())) {
            modelNames_[configArrayIndex] = matchedDevice.GetModelName().c_str();
        }

        return true;
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Failed to attach camera for config index " << configArrayIndex
                  << ": " << e.GetDescription() << std::endl;
        runtime.camera.reset();
        runtime.connected = false;
        configArrayIndexToPylonIndex_[configArrayIndex] = -1;
        if (!suppressBlank) {
            clearCameraTile(configArrayIndex);
        }
        return false;
    }
}

void CameraManager::clearCameraTile(int configArrayIndex) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (callback_ && configArrayIndex >= 0) {
        callback_(configArrayIndex, cv::Mat());
    }
}

Pylon::CInstantCamera* CameraManager::getCameraByConfigIndex(int configArrayIndex) {
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameraRuntimes_.size())) {
        return nullptr;
    }
    return cameraRuntimes_[configArrayIndex].camera.get();
}

const Pylon::CInstantCamera* CameraManager::getCameraByConfigIndex(int configArrayIndex) const {
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameraRuntimes_.size())) {
        return nullptr;
    }
    return cameraRuntimes_[configArrayIndex].camera.get();
}

bool CameraManager::isCameraConnected(int configArrayIndex) const {
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameraRuntimes_.size())) {
        return false;
    }
    return cameraRuntimes_[configArrayIndex].connected && cameraRuntimes_[configArrayIndex].camera != nullptr;
}

void CameraManager::stopCameraRuntime(int configArrayIndex) {
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameraRuntimes_.size())) {
        return;
    }

    CameraRuntime& runtime = cameraRuntimes_[configArrayIndex];
    runtime.connected = false;

    // Stop the stream first so the grab loop can exit, but do not destroy the
    // camera object until the worker thread has fully joined.
    try {
        if (runtime.camera && runtime.camera->IsGrabbing()) {
            runtime.camera->StopGrabbing();
        }
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] stopCameraRuntime stop warning: " << e.GetDescription() << std::endl;
    }

    if (runtime.grabThread.joinable()) {
        runtime.grabThread.join();
    }

    try {
        if (runtime.camera) {
            if (runtime.camera->IsOpen()) {
                runtime.camera->Close();
            }
            if (runtime.camera->IsPylonDeviceAttached()) {
                runtime.camera->DestroyDevice();
            }
        }
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] stopCameraRuntime warning: " << e.GetDescription() << std::endl;
    }

    runtime.camera.reset();
    configArrayIndexToPylonIndex_[configArrayIndex] = -1;
}

bool CameraManager::initialize(const std::set<int>& suppressBlankFor) {
    try {
        CTlFactory& tlFactory = CTlFactory::GetInstance();
        DeviceInfoList_t devices;
        
        // Find all attached devices. 
        if (tlFactory.EnumerateDevices(devices) == 0) {
            std::cerr << "[CameraManager] No cameras found!" << std::endl;
            return false;
        }

        std::cout << "[CameraManager] Found " << devices.size() << " Pylon devices." << std::endl;
        for (const auto& dev : devices) {
            std::cout << "  - DeviceClass: " << dev.GetDeviceClass() << " | MAC: " << (dev.GetDeviceClass() != "BaslerCamEmu" ? dev.GetMacAddress().c_str() : "N/A") << std::endl;
        }

        // Get configured cameras from CameraConfig
        std::vector<CameraInfo> configuredCams = CameraConfig::getCameras();
        
        std::cout << "[CameraManager] Loaded " << configuredCams.size() << " Camera configs." << std::endl;
        for (const auto& c : configuredCams) {
            std::cout << "[CameraManager] Config - ID: " << c.id << " Source: " << c.source << " MAC: " << c.macAddress.toStdString() << std::endl;
        }
        
        if (cameraRuntimes_.size() < configuredCams.size()) {
            cameraRuntimes_.resize(configuredCams.size());
        }

        cameraIndexToConfigId_.assign(configuredCams.size(), -1);
        configArrayIndexToPylonIndex_.assign(configuredCams.size(), -1);
        pylonIndexToConfigArrayIndex_.assign(configuredCams.size(), -1);

        std::set<int> claimedDeviceIndices;
        for (int cfgArrayIdx = 0; cfgArrayIdx < static_cast<int>(configuredCams.size()); ++cfgArrayIdx) {
            const bool suppressBlank = suppressBlankFor.find(cfgArrayIdx) != suppressBlankFor.end();
            attachConfiguredCamera(cfgArrayIdx, configuredCams[cfgArrayIdx], devices, claimedDeviceIndices, suppressBlank);
        }

        return true;
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Pylon exception during initialization: " 
                  << e.GetDescription() << std::endl;
        return false;
    }
}

// ============================================================
// Live Camera Parameter Adjustment (no acquisition restart needed)
// ============================================================

void CameraManager::setCameraGain(int cameraIndex, double gain) {
    std::lock_guard<std::mutex> lock(paramMutex_);
    auto* camera = getCameraByConfigIndex(cameraIndex);
    if (!camera) {
        std::cerr << "[CameraManager] setCameraGain: invalid cameraIndex " << cameraIndex << std::endl;
        return;
    }
    
    if (cameraIndex < (int)swGain_.size()) swGain_[cameraIndex] = std::max(0.0, gain);
    if (cameraIndex < (int)lutValid_.size()) lutValid_[cameraIndex] = false;
    try {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            Pylon::CFloatParameter(nodemap, "Gain").SetValue(gain);
            std::cout << "[CameraManager] setCameraGain: cam=" << cameraIndex << " gain=" << gain << " OK" << std::endl;
        } else {
            std::cerr << "[CameraManager] setCameraGain: cam " << cameraIndex << " camera not open" << std::endl;
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] setCameraGain: cam " << cameraIndex << " ERROR: " << e.GetDescription() << std::endl;
    }
}

void CameraManager::setCameraExposure(int cameraIndex, double exposureUs) {
    auto* camera = getCameraByConfigIndex(cameraIndex);
    if (!camera) {
        std::cerr << "[CameraManager] setCameraExposure: invalid cameraIndex " << cameraIndex << std::endl;
        return;
    }
    
    try {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            Pylon::CFloatParameter(nodemap, "ExposureTimeAbs").SetValue(exposureUs);
            std::cout << "[CameraManager] setCameraExposure: cam=" << cameraIndex << " exp=" << exposureUs << " OK" << std::endl;
        } else {
            std::cerr << "[CameraManager] setCameraExposure: cam " << cameraIndex << " camera not open" << std::endl;
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] setCameraExposure: cam " << cameraIndex << " ERROR: " << e.GetDescription() << std::endl;
    }
}

void CameraManager::setCameraGamma(int cameraIndex, double gamma) {
    std::lock_guard<std::mutex> lock(paramMutex_);
    auto* camera = getCameraByConfigIndex(cameraIndex);
    if (!camera) {
        std::cerr << "[CameraManager] setCameraGamma: invalid cameraIndex " << cameraIndex << std::endl;
        return;
    }
    
    if (cameraIndex < (int)swGamma_.size()) swGamma_[cameraIndex] = std::max(0.01, gamma);
    if (cameraIndex < (int)lutValid_.size()) lutValid_[cameraIndex] = false;
    try {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            Pylon::CFloatParameter(nodemap, "Gamma").SetValue(gamma);
            std::cout << "[CameraManager] setCameraGamma: cam=" << cameraIndex << " gamma=" << gamma << " OK" << std::endl;
        } else {
            std::cerr << "[CameraManager] setCameraGamma: cam " << cameraIndex << " camera not open" << std::endl;
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] setCameraGamma: cam " << cameraIndex << " ERROR: " << e.GetDescription() << std::endl;
    }
}

CameraManager::CameraParams CameraManager::getCameraParams(int configArrayIndex) {
    CameraParams p{0.0, 5000.0, 1.0, 1.0, 0.0};
    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera || !(camera->IsPylonDeviceAttached() && camera->IsOpen()))
        return p;
    try {
        GenApi::INodeMap& nm = camera->GetNodeMap();
        GenApi::CFloatPtr g(nm.GetNode("Gain"));
        if (g && IsReadable(g)) p.gain = g->GetValue();

        GenApi::CFloatPtr e(nm.GetNode("ExposureTimeAbs"));
        if (!e || !IsReadable(e)) e = GenApi::CFloatPtr(nm.GetNode("ExposureTime"));
        if (e && IsReadable(e)) p.exposureUs = e->GetValue();

        GenApi::CFloatPtr gm(nm.GetNode("Gamma"));
        if (gm && IsReadable(gm)) p.gamma = gm->GetValue();

        GenApi::CFloatPtr ct(nm.GetNode("BslContrast"));
        if (ct && IsReadable(ct)) p.contrast = ct->GetValue();

        GenApi::CFloatPtr fps(nm.GetNode("ResultingFrameRateAbs"));
        if (!fps || !IsReadable(fps)) fps = GenApi::CFloatPtr(nm.GetNode("ResultingFrameRate"));
        if (fps && IsReadable(fps)) p.fps = fps->GetValue();
    } catch (...) {}
    return p;
}

void CameraManager::setCameraContrast(int cameraIndex, double contrast) {
    std::lock_guard<std::mutex> lock(paramMutex_);
    auto* camera = getCameraByConfigIndex(cameraIndex);
    if (!camera) {
        std::cerr << "[CameraManager] setCameraContrast: invalid cameraIndex " << cameraIndex << std::endl;
        return;
    }
    
    if (cameraIndex < (int)swContrast_.size()) swContrast_[cameraIndex] = std::max(0.0, contrast);
    if (cameraIndex < (int)lutValid_.size()) lutValid_[cameraIndex] = false;
    try {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            GenApi::CFloatPtr ptrContrast(nodemap.GetNode("BslContrast"));
            if (ptrContrast && GenApi::IsWritable(ptrContrast)) {
                ptrContrast->SetValue(contrast);
                std::cout << "[CameraManager] setCameraContrast: cam=" << cameraIndex << " contrast=" << contrast << " OK" << std::endl;
            } else {
                std::cerr << "[CameraManager] setCameraContrast: cam " << cameraIndex << " BslContrast node not found or not writable" << std::endl;
            }
        } else {
            std::cerr << "[CameraManager] setCameraContrast: cam " << cameraIndex << " camera not open" << std::endl;
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] setCameraContrast: cam " << cameraIndex << " ERROR: " << e.GetDescription() << std::endl;
    }
}

bool CameraManager::saveParameters(int configArrayIndex) {
    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera || !camera->IsPylonDeviceAttached() || !camera->IsOpen()) {
        std::cerr << "[CameraManager] saveParameters: camera " << configArrayIndex << " not connected." << std::endl;
        return false;
    }
    // Determine the config-level ID (1-based) for a unique filename
    int configId = configArrayIndex + 1;
    if (configArrayIndex < (int)cameraIndexToConfigId_.size() && cameraIndexToConfigId_[configArrayIndex] > 0) {
        configId = cameraIndexToConfigId_[configArrayIndex];
    }
    std::string pfsDir = "/etc/papervision/cameras";
    // Ensure directory exists
    {
        QDir dir(QString::fromStdString(pfsDir));
        if (!dir.exists()) dir.mkpath(".");
    }
    std::string filename = pfsDir + "/camera_" + std::to_string(configId) + ".pfs";

    bool result = false;
    try {
        Pylon::CFeaturePersistence::Save(filename.c_str(), &camera->GetNodeMap());
        std::cout << "[CameraManager] Parameters saved to " << filename << std::endl;
        result = true;
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Error saving parameters: " << e.GetDescription() << std::endl;
    }
    return result;
}

bool CameraManager::loadParameters(int configArrayIndex) {
    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera || !camera->IsPylonDeviceAttached() || !camera->IsOpen()) {
        std::cerr << "[CameraManager] loadParameters: camera " << configArrayIndex << " not connected." << std::endl;
        return false;
    }
    int configId = configArrayIndex + 1;
    std::string filename = "/etc/papervision/cameras/camera_" + std::to_string(configId) + ".pfs";

    bool result = false;
    try {
        Pylon::CFeaturePersistence::Load(filename.c_str(), &camera->GetNodeMap(), true);
        std::cout << "[CameraManager] Parameters loaded from " << filename << std::endl;
        result = true;
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Error loading parameters: " << e.GetDescription() << std::endl;
    }
    return result;
}

void CameraManager::saveParametersForAll(const std::vector<CameraInfo>& cameras) {
    std::lock_guard<std::mutex> lock(cameraParamsMutex_);
    
    std::string pfsDir = "/etc/papervision/cameras";
    {
        QDir dir(QString::fromStdString(pfsDir));
        if (!dir.exists()) dir.mkpath(".");
    }
    
    // CFeaturePersistence::Save only reads the NodeMap — safe to call while grabbing,
    // no need to StopGrabbing which would block the UI thread.
    for (int i = 0; i < (int)cameras.size(); ++i) {
        if (cameras[i].source != 1) continue; // Only real cameras
        
        auto* camera = getCameraByConfigIndex(i);
        if (!camera) continue;
        
        if (!camera->IsPylonDeviceAttached() || !camera->IsOpen()) {
            std::cerr << "[CameraManager] saveParametersForAll: camera " << cameras[i].id << " not connected." << std::endl;
            continue;
        }
        
        int configId = cameras[i].id;
        std::string filename = pfsDir + "/camera_" + std::to_string(configId) + ".pfs";
        
        try {
            Pylon::CFeaturePersistence::Save(filename.c_str(), &camera->GetNodeMap());
            std::cout << "[CameraManager] Parameters saved to " << filename << std::endl;
        } catch (const Pylon::GenericException& e) {
            std::cerr << "[CameraManager] Error saving parameters for cam " << cameras[i].id << ": " << e.GetDescription() << std::endl;
        }
    }
}

void CameraManager::configureCamera(GenApi::INodeMap& nodemap, bool isEmulation) {
    try {
        // GenApi::INodeMap& nodemap = device->GetNodeMap(); // Removed, passed directly

        // --- CHUNK DATA CONFIGURATION (Moved to top for verification) ---
        // 4. Enable Chunk Data (Timestamp, FrameCounter, CRC)
        if (!isEmulation) {
            // Enable Chunk Mode
            GenApi::CBooleanPtr ptrChunkModeActive(nodemap.GetNode("ChunkModeActive"));
            if (ptrChunkModeActive.IsValid()) {
                 if (IsWritable(ptrChunkModeActive)) {
                    ptrChunkModeActive->SetValue(true);
                    std::cout << "[CameraManager] Chunk Mode Enabled." << std::endl;
                 } else {
                    std::cout << "[CameraManager] ChunkModeActive found but NOT Writable." << std::endl;
                 }
            } else {
                 std::cout << "[CameraManager] ChunkModeActive Node NOT FOUND." << std::endl;
            }

            GenApi::CEnumerationPtr ptrChunkSelector(nodemap.GetNode("ChunkSelector"));
            GenApi::CBooleanPtr ptrChunkEnable(nodemap.GetNode("ChunkEnable"));

            if (IsWritable(ptrChunkSelector) && IsWritable(ptrChunkEnable)) {
                // Enable Timestamp
                if (GenApi::IsAvailable(ptrChunkSelector->GetEntryByName("Timestamp"))) {
                    ptrChunkSelector->FromString("Timestamp");
                    ptrChunkEnable->SetValue(true);
                }
                // Enable Framecounter
                if (GenApi::IsAvailable(ptrChunkSelector->GetEntryByName("Framecounter"))) {
                    ptrChunkSelector->FromString("Framecounter");
                    ptrChunkEnable->SetValue(true);
                }
                // Enable CRC
                if (GenApi::IsAvailable(ptrChunkSelector->GetEntryByName("PayloadCRC16"))) {
                    ptrChunkSelector->FromString("PayloadCRC16");
                    ptrChunkEnable->SetValue(true);
                }
            }
        } else {
             std::cout << "[CameraManager] Running in EMULATION mode. Skipping Chunk Mode configuration." << std::endl;
        }
        // ---------------------------------------------------------------
        
        // 0. Set Resolution and Pixel Format for scA780 emulation
        // Note: For emulation, this requires the emulated device to support these values.
        GenApi::CIntegerPtr(nodemap.GetNode("Width"))->SetValue(782);
        GenApi::CIntegerPtr(nodemap.GetNode("Height"))->SetValue(582);
        GenApi::CEnumerationPtr(nodemap.GetNode("PixelFormat"))->FromString("Mono8");

        // 1. Enable PTP (IEEE 1588)
        // Note: Emulated cameras might not support this, check for existence
        GenApi::CBooleanPtr ptrPtpEnable(nodemap.GetNode("GevIEEE1588"));
        if (GenApi::IsWritable(ptrPtpEnable)) {
            ptrPtpEnable->SetValue(true);
            std::cout << "[CameraManager] PTP Enabled." << std::endl;
        }

        // 2. Set Jumbo Frames (MTU 9000)
        // Ideally 9000 -> 8192 payload + headers
        GenApi::CIntegerPtr ptrPacketSize(nodemap.GetNode("GevSCPSPacketSize"));
        if (IsWritable(ptrPacketSize)) {
            ptrPacketSize->SetValue(8192); // typical value for 9000 MTU
            std::cout << "[CameraManager] Packet Size set to 8192." << std::endl;
        }
        
        // 3. Persistent IP (Fixed IP)
        GenApi::CBooleanPtr ptrCurrentIpConfigPersistent(nodemap.GetNode("GevCurrentIPConfigurationPersistentIP"));
        if (IsWritable(ptrCurrentIpConfigPersistent)) {
            ptrCurrentIpConfigPersistent->SetValue(true);
        }


    } catch (const GenericException& e) {
        // Ignored for emulation, but printed for debug
        std::cout << "[CameraManager] Config warning: " << e.GetDescription() << std::endl;
    }
}

void CameraManager::startAcquisition() {
    if (acquiring_) return;

    try {
        bufferPools_.clear();
        for (int i = 0; i < numCameras_; ++i) {
            bufferPools_.push_back(std::make_unique<BufferPool>(3, width_, height_, CV_8UC1));
        }

        {
            std::lock_guard<std::mutex> lock(disconnectedMutex_);
            disconnectedCameras_.clear();
        }

        {
            std::lock_guard<std::mutex> lock(latestFramesMutex_);
            latestFrames_.assign(numCameras_, cv::Mat());
        }

        EventController::instance().initialize(100, 10.0, 50);

        bool anyConnected = false;
        for (int i = 0; i < static_cast<int>(cameraRuntimes_.size()); ++i) {
            auto* camera = getCameraByConfigIndex(i);
            if (!camera) {
                continue;
            }

            try {
                if (!camera->IsOpen()) {
                    camera->Open();
                }

                if (!anyConnected) {
                    GenApi::CIntegerPtr ptrWidth(camera->GetNodeMap().GetNode("Width"));
                    GenApi::CIntegerPtr ptrHeight(camera->GetNodeMap().GetNode("Height"));
                    if (IsReadable(ptrWidth) && IsReadable(ptrHeight)) {
                        width_ = static_cast<int>(ptrWidth->GetValue());
                        height_ = static_cast<int>(ptrHeight->GetValue());
                        std::cout << "[CameraManager] Updated resolution from camera: " << width_ << "x" << height_ << std::endl;
                    }
                }

                configureCamera(camera->GetNodeMap(), camera->GetDeviceInfo().GetDeviceClass() == "BaslerCamEmu");
                camera->MaxNumBuffer.SetValue(5);
                camera->StartGrabbing(GrabStrategy_LatestImageOnly, GrabLoop_ProvidedByUser);
                anyConnected = true;

                if (camera->IsGigE()) {
                    std::cout << "[CameraManager] Cam " << i << " Stream grabber uses "
                              << Pylon::CEnumParameter(camera->GetStreamGrabberNodeMap(), "Type").GetValueOrDefault("Other")
                              << std::endl;
                }

                try {
                    GenApi::INodeMap& nodemap = camera->GetNodeMap();
                    bool isEmu = (camera->GetDeviceInfo().GetDeviceClass() == "BaslerCamEmu");
                    if (!isEmu) {
                        GenApi::CEnumerationPtr ptrEvtSel(nodemap.GetNode("EventSelector"));
                        GenApi::CEnumerationPtr ptrEvtNotif(nodemap.GetNode("EventNotification"));
                        if (IsWritable(ptrEvtSel) && IsWritable(ptrEvtNotif)) {
                            if (GenApi::IsAvailable(ptrEvtSel->GetEntryByName("CriticalTemperature"))) {
                                ptrEvtSel->FromString("CriticalTemperature");
                                ptrEvtNotif->FromString("On");
                            }
                            if (GenApi::IsAvailable(ptrEvtSel->GetEntryByName("OverTemperature"))) {
                                ptrEvtSel->FromString("OverTemperature");
                                ptrEvtNotif->FromString("On");
                            }
                        }
                    }
                } catch (const GenericException& e) {
                    std::cout << "[CameraManager] Temp event setup warning (cam " << i << "): " << e.GetDescription() << std::endl;
                }
            } catch (const GenericException& e) {
                std::cerr << "[CameraManager] Failed to start camera " << i << ": " << e.GetDescription() << std::endl;
                stopCameraRuntime(i);
                {
                    std::lock_guard<std::mutex> lock(disconnectedMutex_);
                    disconnectedCameras_.insert(static_cast<uint32_t>(i));
                }
                clearCameraTile(i);
            }
        }

        prevTempStatus_.assign(std::max(numCameras_, static_cast<int>(cameraRuntimes_.size())), TemperatureStatus::Unknown);

        if (tempMonitorThread_.joinable()) {
            tempMonitorRunning_ = false;
            tempMonitorThread_.join();
        }

        tempMonitorRunning_ = true;
        tempMonitorThread_ = std::thread(&CameraManager::temperatureMonitorLoop, this);

        acquiring_ = anyConnected;
        if (anyConnected) {
            for (int i = 0; i < static_cast<int>(cameraRuntimes_.size()); ++i) {
                CameraRuntime& runtime = cameraRuntimes_[i];
                if (runtime.connected && runtime.camera && !runtime.grabThread.joinable()) {
                    runtime.grabThread = std::thread(&CameraManager::acquisitionLoop, this, i);
                }
            }
            std::cout << "[CameraManager] Started per-camera acquisition loops." << std::endl;
        } else {
            std::cout << "[CameraManager] Skipped acquisition loop (0 cameras attached)." << std::endl;
            if (!recovering_) {
                std::cout << "[CameraManager] Bootstrapping recovery thread to poll for cameras..." << std::endl;
                if (recoveryThread_.joinable()) recoveryThread_.join();
                recoveryThread_ = std::thread(&CameraManager::recoveryLoop, this);
            }
        }
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Pylon exception during start: "
                  << e.GetDescription() << std::endl;
    }
}

void CameraManager::stopAcquisition() {
    acquiring_ = false;

    // Stop recovery thread first — it may try to restart acquisition
    recovering_ = false;
    if (recoveryThread_.joinable()) {
        recoveryThread_.join();
    }

    // Stop temperature monitor thread
    tempMonitorRunning_ = false;
    if (tempMonitorThread_.joinable()) {
        tempMonitorThread_.join();
    }

    for (int i = 0; i < static_cast<int>(cameraRuntimes_.size()); ++i) {
        stopCameraRuntime(i);
    }
    
    // Clear buffer pools
    bufferPools_.clear();
}

void CameraManager::pauseGrabbing(bool pause) {
    paused_ = pause;
    std::cout << "[CameraManager] Grabbing " << (pause ? "PAUSED" : "RESUMED") << std::endl;
}

bool CameraManager::isGrabbingPaused() const {
    return paused_;
}

bool CameraManager::tryReconnectCamera(int configArrayIndex) {
    auto configuredCams = CameraConfig::getCameras();
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(configuredCams.size())) {
        return false;
    }

    stopCameraRuntime(configArrayIndex);

    CTlFactory& tlFactory = CTlFactory::GetInstance();
    DeviceInfoList_t devices;
    tlFactory.EnumerateDevices(devices);
    std::set<int> claimed;

    for (int i = 0; i < static_cast<int>(cameraRuntimes_.size()); ++i) {
        if (i == configArrayIndex) {
            continue;
        }

        const auto* camera = getCameraByConfigIndex(i);
        if (!camera || !camera->IsPylonDeviceAttached()) {
            continue;
        }

        for (int devIndex = 0; devIndex < static_cast<int>(devices.size()); ++devIndex) {
            if (claimed.count(devIndex)) {
                continue;
            }
            if (camera->GetDeviceInfo().GetSerialNumber() == devices[devIndex].GetSerialNumber()) {
                claimed.insert(devIndex);
                break;
            }
        }
    }

    if (!attachConfiguredCamera(configArrayIndex, configuredCams[configArrayIndex], devices, claimed, true)) {
        return false;
    }

    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera) {
        return false;
    }

    try {
        if (!camera->IsOpen()) {
            camera->Open();
        }
        configureCamera(camera->GetNodeMap(), camera->GetDeviceInfo().GetDeviceClass() == "BaslerCamEmu");
        camera->MaxNumBuffer.SetValue(5);
        camera->StartGrabbing(GrabStrategy_LatestImageOnly, GrabLoop_ProvidedByUser);
        cameraRuntimes_[configArrayIndex].connected = true;
        cameraRuntimes_[configArrayIndex].grabThread = std::thread(&CameraManager::acquisitionLoop, this, configArrayIndex);
        setCameraFrameRate(configArrayIndex, configuredCams[configArrayIndex].fps, configuredCams[configArrayIndex].enableAcquisitionFps);
        return true;
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Reconnect failed for camera " << configArrayIndex << ": " << e.GetDescription() << std::endl;
        stopCameraRuntime(configArrayIndex);
        return false;
    }
}

void CameraManager::recoveryLoop() {
    recovering_ = true;
    std::cout << "[CameraManager] --- RECOVERY LOOP STARTED ---" << std::endl;

    while (recovering_) {
        std::vector<int> disconnected;
        {
            std::lock_guard<std::mutex> lock(disconnectedMutex_);
            disconnected.assign(disconnectedCameras_.begin(), disconnectedCameras_.end());
        }

        bool anyRecovered = false;
        for (int configIdx : disconnected) {
            if (tryReconnectCamera(configIdx)) {
                anyRecovered = true;
                std::lock_guard<std::mutex> lock(disconnectedMutex_);
                disconnectedCameras_.erase(static_cast<uint32_t>(configIdx));
            }
        }

        {
            std::lock_guard<std::mutex> lock(disconnectedMutex_);
            if (disconnectedCameras_.empty()) {
                recovering_ = false;
                if (statusCallback_) statusCallback_("[CameraManager] System Fully Recovered.");
                break;
            }
        }

        if (!acquiring_) {
            bool anyConnected = false;
            for (const auto& runtime : cameraRuntimes_) {
                if (runtime.connected && runtime.camera && runtime.camera->IsGrabbing()) {
                    anyConnected = true;
                    break;
                }
            }
            acquiring_ = anyConnected;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(anyRecovered ? 100 : 500));
    }

    std::cout << "[CameraManager] --- RECOVERY LOOP COMPLETED ---" << std::endl;
}

// ============================================================
// Temperature Monitor Loop (background thread, per Basler App Note AW00138003000)
// ============================================================
void CameraManager::temperatureMonitorLoop() {
    std::cout << "[TempMonitor] Started. Polling every 10 seconds." << std::endl;

    while (tempMonitorRunning_) {
        // Poll each configured camera slot (config array index, 0-based)
        int numCfg = (int)configArrayIndexToPylonIndex_.size();
        if (numCfg == 0) numCfg = numCameras_;

        for (int cfgIdx = 0; cfgIdx < numCfg && tempMonitorRunning_; ++cfgIdx) {
            // Skip cameras that are currently disconnected — avoids spamming error logs
            int pylonIdx = (cfgIdx < (int)configArrayIndexToPylonIndex_.size())
                           ? configArrayIndexToPylonIndex_[cfgIdx] : -1;
            {
                std::lock_guard<std::mutex> lk(disconnectedMutex_);
                if (pylonIdx < 0 || disconnectedCameras_.count(static_cast<uint32_t>(pylonIdx)))
                    continue;
            }

            double temp = getTemperature(cfgIdx);
            TemperatureStatus status = classifyTemperature(temp);

            // Resize tracking vector if needed
            if (cfgIdx >= (int)prevTempStatus_.size()) {
                prevTempStatus_.resize(cfgIdx + 1, TemperatureStatus::Unknown);
            }

            // Fire callback only when status changes (or first known reading)
            if (status != prevTempStatus_[cfgIdx]) {
                prevTempStatus_[cfgIdx] = status;
                if (tempAlertCallback_) {
                    tempAlertCallback_(cfgIdx, temp, status);
                }
            }

            // Also always fire for Critical/Error so the UI stays updated
            if ((status == TemperatureStatus::Critical || status == TemperatureStatus::Error)
                && tempAlertCallback_) {
                tempAlertCallback_(cfgIdx, temp, status);
            }
        }

        // Sleep 10 seconds in 500ms increments so the thread is responsive to stop
        for (int t = 0; t < 20 && tempMonitorRunning_; ++t) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    std::cout << "[TempMonitor] Stopped." << std::endl;
}

void CameraManager::registerCallback(FrameCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callback_ = callback;
}

std::vector<std::string> CameraManager::getCameraLabels() const {
    return cameraLabels_;
}

std::string CameraManager::getModelName(int configArrayIndex) {
    const auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera) {
        return "Not Connected";
    }

    try {
        if (camera->IsPylonDeviceAttached()) {
            return camera->GetDeviceInfo().GetModelName().c_str();
        }
    } catch (...) {}

    if (configArrayIndex >= 0 && configArrayIndex < (int)modelNames_.size()) {
        return modelNames_[configArrayIndex];
    }
    return "Unknown Model";
}

std::string CameraManager::getIpAddress(int configArrayIndex) {
    const auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera) {
        return "Offline";
    }

    try {
        if (camera->IsPylonDeviceAttached()) {
            return camera->GetDeviceInfo().GetIpAddress().c_str();
        }
    } catch (...) {}

    return "Offline";
}

cv::Size CameraManager::getCameraResolution(int configArrayIndex) {
    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera) {
        return cv::Size(0, 0);
    }

    try {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            int w = (int)GenApi::CIntegerPtr(nodemap.GetNode("Width"))->GetValue();
            int h = (int)GenApi::CIntegerPtr(nodemap.GetNode("Height"))->GetValue();
            return cv::Size(w, h);
        }
    } catch (...) {}

    return cv::Size(0, 0);
}

double CameraManager::getCameraFps(int configArrayIndex) {
    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera) {
        return 0.0;
    }

    try {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            GenApi::CFloatPtr ptrFpsAbs(nodemap.GetNode("ResultingFrameRateAbs"));
            if (GenApi::IsReadable(ptrFpsAbs)) {
                return ptrFpsAbs->GetValue();
            }
            GenApi::CFloatPtr ptrFps(nodemap.GetNode("ResultingFrameRate"));
            if (GenApi::IsReadable(ptrFps)) {
                return ptrFps->GetValue();
            }
        }
    } catch (...) {}

    return 0.0;
}

double CameraManager::getTemperature(int configArrayIndex) {
    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera) {
        return -1.0; // Sentinel: camera not connected
    }

    try {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            
            // === Attempt 1: Scout GigE (TemperatureSelector + TemperatureAbs) ===
            GenApi::CEnumerationPtr ptrTempSelector(nodemap.GetNode("TemperatureSelector"));
            if (IsWritable(ptrTempSelector)) {
                GenApi::CEnumEntryPtr ptrSensorboard(ptrTempSelector->GetEntryByName("Sensorboard"));
                if (IsReadable(ptrSensorboard)) {
                    ptrTempSelector->SetIntValue(ptrSensorboard->GetValue());
                }
            }
            GenApi::CFloatPtr ptrTempAbs(nodemap.GetNode("TemperatureAbs"));
            if (IsReadable(ptrTempAbs)) {
                return ptrTempAbs->GetValue();
            }
            
            // === Attempt 2: Ace U/L (DeviceTemperatureSelector + DeviceTemperature) ===
            GenApi::CEnumerationPtr ptrDevTempSelector(nodemap.GetNode("DeviceTemperatureSelector"));
            if (IsWritable(ptrDevTempSelector)) {
                GenApi::CEnumEntryPtr ptrCoreboard(ptrDevTempSelector->GetEntryByName("Coreboard"));
                if (IsReadable(ptrCoreboard)) {
                    ptrDevTempSelector->SetIntValue(ptrCoreboard->GetValue());
                }
            }
            GenApi::CFloatPtr ptrDevTemp(nodemap.GetNode("DeviceTemperature"));
            if (IsReadable(ptrDevTemp)) {
                return ptrDevTemp->GetValue();
            }
            
            // === Attempt 3: Try reading from Transport Layer NodeMap ===
            try {
                GenApi::INodeMap& tlNodemap = camera->GetTLNodeMap();
                GenApi::CFloatPtr ptrTlTemp(tlNodemap.GetNode("TemperatureAbs"));
                if (IsReadable(ptrTlTemp)) {
                    return ptrTlTemp->GetValue();
                }
                GenApi::CFloatPtr ptrTlDevTemp(tlNodemap.GetNode("DeviceTemperature"));
                if (IsReadable(ptrTlDevTemp)) {
                    return ptrTlDevTemp->GetValue();
                }
            } catch (...) {}
            
            // === Attempt 4: Check alternative/legacy node names ===
            const char* tempNodeNames[] = {
                "TemperatureAbs", "DeviceTemperature", "Temperature",
                "SensorBoardTemperature", "BoardTemperature",
                "BslTemperature", "DeviceTemperatureAbs",
                nullptr
            };
            for (int n = 0; tempNodeNames[n] != nullptr; ++n) {
                GenApi::CFloatPtr ptrT(nodemap.GetNode(tempNodeNames[n]));
                if (ptrT && IsReadable(ptrT)) {
                    return ptrT->GetValue();
                }
                GenApi::CIntegerPtr ptrTi(nodemap.GetNode(tempNodeNames[n]));
                if (ptrTi && IsReadable(ptrTi)) {
                    return (double)ptrTi->GetValue();
                }
            }
            
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Temperature read failed: " << e.GetDescription() << std::endl;
    }
    return -1.0; // Sentinel: unavailable
}

cv::Size CameraManager::getResolution() const {
    return cv::Size(width_, height_);
}

void CameraManager::setDefectDetectionEnabled(bool enabled) {
    defectDetectionEnabled_ = enabled;
    std::cout << "[CameraManager] Defect Detection " << (enabled ? "ENABLED" : "DISABLED") << std::endl;
}

bool CameraManager::isDefectDetectionEnabled() const {
    return defectDetectionEnabled_;
}

void CameraManager::triggerSnapshot(int cameraIndex) {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    if (cameraIndex >= 0 && cameraIndex < (int)snapshotRequests_.size()) {
        snapshotRequests_[cameraIndex] = true;
        std::cout << "[CameraManager] Snapshot requested for Camera " << cameraIndex << std::endl;
    }
}

void CameraManager::acquisitionLoop(int configArrayIndex) {
    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera) {
        return;
    }

    std::cout << "[CameraManager] Entering acquisition loop for slot " << configArrayIndex << std::endl;
    CGrabResultPtr ptrGrabResult;
    CImageFormatConverter formatConverter;
    formatConverter.OutputPixelFormat = PixelType_Mono8;

    CPylonImage pylonImage; 

    while (acquiring_ && cameraRuntimes_[configArrayIndex].connected && camera->IsGrabbing()) {
        try {
            if (camera->RetrieveResult(5000, ptrGrabResult, TimeoutHandling_Return)) {
                if (!ptrGrabResult) continue;  

                if (paused_) continue; // Skip frame processing when paused

                uint32_t cameraIndex = static_cast<uint32_t>(configArrayIndex);

                // Skip frames from cameras that have been flagged as disconnected.
                {
                    std::lock_guard<std::mutex> lock(disconnectedMutex_);
                    if (disconnectedCameras_.count(cameraIndex)) continue;
                }

                if (ptrGrabResult->GrabSucceeded()) {
                    // 1. CHUNK DATA & METADATA
                    int64_t timestamp = 0;
                    int64_t frameCounter = 0;
                    
                    bool chunkValid = false;
                    
                    if (PayloadType_ChunkData == ptrGrabResult->GetPayloadType()) {
                        if (ptrGrabResult->HasCRC() && !ptrGrabResult->CheckCRC()) {
                             std::cerr << "[CameraManager] Error: Image CRC failed!" << std::endl;
                        } else {
                            // Extract Metadata via NodeMap
                            GenApi::INodeMap& chunkNodeMap = ptrGrabResult->GetChunkDataNodeMap();
                            
                            // Timestamp
                            GenApi::CIntegerPtr ptrTs(chunkNodeMap.GetNode("ChunkTimestamp"));
                            if (IsReadable(ptrTs)) {
                                timestamp = ptrTs->GetValue();
                            }
                            
                            // Frame Counter
                            GenApi::CIntegerPtr ptrFc(chunkNodeMap.GetNode("ChunkFramecounter"));
                            if (IsReadable(ptrFc)) {
                                frameCounter = ptrFc->GetValue();
                            }
                            
                            if (timestamp > 0) chunkValid = true;
                        }
                    } 
                    
                    // FALLBACK: If Chunk Data is missing or invalid (e.g. Emulation or timestamp=0)
                    if (!chunkValid || timestamp == 0) {
                        // Generate high-precision software timestamp (nanoseconds)
                        auto now = std::chrono::system_clock::now();
                        auto duration = now.time_since_epoch();
                        timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
                        
                        // Increment frame counter manually (per camera)
                        static std::map<int, int64_t> softwareCounters;
                        frameCounter = ++softwareCounters[cameraIndex];
                    }

                    // 2. IMAGE CONVERSION
                    const uint8_t *pImageBuffer = (uint8_t *) ptrGrabResult->GetBuffer();
                    int width = ptrGrabResult->GetWidth();
                    int height = ptrGrabResult->GetHeight();

                    if (width > 0 && height > 0 && pImageBuffer) {
                        cv::Mat displayFrame;
                        cv::Mat wrapper(height, width, CV_8UC1, (void*)pImageBuffer);
                        
                        // 3. PROCESSING
                        // Always apply per-camera software LUT (Gain/Gamma/Contrast) for visual feedback
                        cv::Mat softFrame;
                        {
                            std::lock_guard<std::mutex> lock(paramMutex_);
                            int cfgIdx = pylonIndexToConfigArrayIndex_.size() > cameraIndex
                                       ? pylonIndexToConfigArrayIndex_[cameraIndex] : (int)cameraIndex;
                            double g  = (cfgIdx >= 0 && cfgIdx < (int)swGain_.size())     ? swGain_[cfgIdx]     : 1.0;
                            double gm = (cfgIdx >= 0 && cfgIdx < (int)swGamma_.size())    ? swGamma_[cfgIdx]    : 1.0;
                            double c  = (cfgIdx >= 0 && cfgIdx < (int)swContrast_.size()) ? swContrast_[cfgIdx] : 1.0;
                            bool needsLUT = (std::abs(g-1.0) > 0.01 || std::abs(gm-1.0) > 0.01 || std::abs(c-1.0) > 0.01);
                            if (needsLUT) {
                                if (cfgIdx >= 0 && cfgIdx < (int)lutValid_.size() && lutValid_[cfgIdx]) {
                                    // Use cached LUT — avoids rebuilding 256-entry lookup + pow() on every frame
                                    cv::LUT(wrapper, lutCache_[cfgIdx], softFrame);
                                } else {
                                    // Build and cache the LUT
                                    cv::Mat lut(1, 256, CV_8U);
                                    for (int i = 0; i < 256; ++i) {
                                        double v = i * g;
                                        v = (v - 128) * c + 128;
                                        v = std::pow(std::max(v / 255.0, 0.0), 1.0 / gm) * 255.0;
                                        lut.at<uchar>(i) = static_cast<uchar>(std::min(255.0, std::max(0.0, v)));
                                    }
                                    if (cfgIdx >= 0 && cfgIdx < (int)lutCache_.size()) {
                                        lutCache_[cfgIdx] = lut;
                                        lutValid_[cfgIdx] = true;
                                    }
                                    cv::LUT(wrapper, lut, softFrame);
                                }
                            } else {
                                softFrame = wrapper; // no-op reference copy, zero overhead
                            }
                        }

                        if (defectDetectionEnabled_) {
                             processFrame(softFrame, displayFrame, (int)cameraIndex);
                        } else {
                            displayFrame = softFrame;
                        }

                        // Resolve the Config ID from the Pylon index
                        int configId = (cameraIndex < cameraIndexToConfigId_.size())
                                        ? cameraIndexToConfigId_[cameraIndex]
                                        : (int)(cameraIndex + 1); // fallback

                        // 4. EVENT CONTROLLER: feed all connected cameras, using 1-based configId
                        EventController::instance().addFrame(configId, wrapper, timestamp, frameCounter);

                        // 5. CALLBACK: emit config array index (0-based UI slot) resolved from Pylon index
                        {
                            int configArrayIdx = static_cast<int>(cameraIndex);

                            if (configArrayIdx >= 0) {
                                std::lock_guard<std::mutex> lk(latestFramesMutex_);
                                if (configArrayIdx < (int)latestFrames_.size()) {
                                    latestFrames_[configArrayIdx] = displayFrame.clone();
                                }
                            }

                            std::lock_guard<std::mutex> lock(callbackMutex_);
                            if (callback_ && configArrayIdx >= 0) {
                                callback_(configArrayIdx, displayFrame);
                            }
                        }
                    } else {
                        std::cout << "[CameraManager] Invalid frame: " << width << "x" << height << " Buffer: " << (pImageBuffer ? "OK" : "NULL") << std::endl;
                    }
                } else {
                    std::cerr << "[CameraManager] Grab failed: " 
                              << ptrGrabResult->GetErrorDescription() << std::endl;
                              
                    if (camera->IsCameraDeviceRemoved()) {
                        std::cerr << "[CameraManager] Hardware disconnect on camera slot " << cameraIndex << std::endl;
                        {
                            std::lock_guard<std::mutex> lock(disconnectedMutex_);
                            disconnectedCameras_.insert(cameraIndex);
                        }
                        cameraRuntimes_[configArrayIndex].connected = false;
                        int configArrayIdx = (int)cameraIndex;
                        if (configArrayIdx >= 0) {
                            std::lock_guard<std::mutex> lock(callbackMutex_);
                            if (callback_) callback_(configArrayIdx, cv::Mat());
                        }
                        if (!recovering_) {
                            if (recoveryThread_.joinable()) recoveryThread_.join();
                            recoveryThread_ = std::thread(&CameraManager::recoveryLoop, this);
                        }
                        break;
                    }
                }

                // 6. SNAPSHOT REQUESTS
                bool takeSnapshot = false;
                {
                    std::lock_guard<std::mutex> lock(snapshotMutex_);
                    if (cameraIndex < (uint32_t)snapshotRequests_.size() && snapshotRequests_[cameraIndex]) {
                        takeSnapshot = true;
                        snapshotRequests_[cameraIndex] = false; 
                    }
                }

                if (takeSnapshot) {
                    try {
                        auto now = std::chrono::system_clock::now();
                        auto time_t = std::chrono::system_clock::to_time_t(now);
                        std::stringstream ss;
                        ss << "../data/Snapshot_Cam" << cameraIndex << "_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") << ".png";
                        std::string filename = ss.str();
                        std::cout << "[CameraManager] Saving snapshot to: " << filename << std::endl;
                        CImagePersistence::Save(ImageFileFormat_Png, filename.c_str(), ptrGrabResult);
                    } catch (const GenericException& e) {
                         std::cerr << "[CameraManager] Failed to save snapshot: " << e.GetDescription() << std::endl;
                    }
                }
            }
        } catch (const GenericException& e) {
            if (acquiring_) {
                std::cerr << "[CameraManager] Pylon exception in loop: " 
                          << e.GetDescription() << std::endl;

                // Brief pause to let the OS settle after a hot-unplug event
                Pylon::WaitObject::Sleep(200);

                if (camera->IsCameraDeviceRemoved()) {
                    std::cerr << "[CameraManager] Hardware disconnect confirmed on camera slot " << configArrayIndex << std::endl;
                    {
                        std::lock_guard<std::mutex> lock(disconnectedMutex_);
                        disconnectedCameras_.insert(static_cast<uint32_t>(configArrayIndex));
                    }
                    cameraRuntimes_[configArrayIndex].connected = false;
                    clearCameraTile(configArrayIndex);
                    if (!recovering_) {
                        if (recoveryThread_.joinable()) recoveryThread_.join();
                        recoveryThread_ = std::thread(&CameraManager::recoveryLoop, this);
                    }
                    break;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[CameraManager] Standard exception in loop: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[CameraManager] Unknown exception in loop!" << std::endl;
        }
    }
    std::cout << "[CameraManager] Exiting acquisition loop for slot " << configArrayIndex << std::endl;
}

void CameraManager::processFrame(const cv::Mat& input, cv::Mat& output, int cameraIndex) {
    // Telemetry Overlay (always shown)
    std::string label = (cameraIndex < (int)cameraLabels_.size()) ? cameraLabels_[cameraIndex] : "Cam " + std::to_string(cameraIndex);
    
    // Frame Counter / Timestamp
    static std::atomic<long> frameCount{0};
    frameCount++;
    
    std::string info = label + " | F:" + std::to_string(frameCount);
    
    // OPTIMIZATION: Keep Mono8 (Grayscale) to save 3x Memory
    // Input is already Mono8.
    // Apply software Gain, Gamma, and Contrast via a LUT for performance
    {
        // Resolve config index for this pylon camera index
        int cfgIdx = cameraIndex;
        if (cameraIndex < (int)pylonIndexToConfigArrayIndex_.size()) {
            cfgIdx = pylonIndexToConfigArrayIndex_[cameraIndex];
        }
        double gain     = (cfgIdx >= 0 && cfgIdx < (int)swGain_.size())     ? swGain_[cfgIdx]     : 1.0;
        double gamma    = (cfgIdx >= 0 && cfgIdx < (int)swGamma_.size())    ? swGamma_[cfgIdx]    : 1.0;
        double contrast = (cfgIdx >= 0 && cfgIdx < (int)swContrast_.size()) ? swContrast_[cfgIdx] : 1.0;

        bool needsProcessing = (std::abs(gain - 1.0) > 0.01 || std::abs(gamma - 1.0) > 0.01 || std::abs(contrast - 1.0) > 0.01);
        if (needsProcessing) {
            // Build a 256-entry LUT: Gain -> Contrast -> Gamma
            cv::Mat lut(1, 256, CV_8U);
            for (int i = 0; i < 256; ++i) {
                double v = i * gain;                // apply gain
                v = (v - 128) * contrast + 128;    // apply contrast around midpoint
                v = std::pow(std::max(v / 255.0, 0.0), 1.0 / gamma) * 255.0; // apply gamma
                lut.at<uchar>(i) = static_cast<uchar>(std::min(255.0, std::max(0.0, v)));
            }
            cv::LUT(input, lut, output);
        } else {
            if (input.data != output.data) {
                input.copyTo(output);
            }
        }
    }
    
    // Only run defect detection if enabled
    if (defectDetectionEnabled_) {
        // "Best Result": Convert Mono8 to BGR to allow colored (RED) defect visualization
        // converting input (Mono8) to gray for processing
        cv::Mat gray, processed;
        
        // Input is already Mono8 (guaranteed by buffer pool)
        gray = input; // Soft copy
        
        // 1. Noise Reduction
        cv::GaussianBlur(gray, processed, cv::Size(5, 5), 1.5);
        
        // 2. Defect Detection (Adaptive Threshold)
        cv::adaptiveThreshold(processed, processed, 255, 
            cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY_INV, 11, 2);
            
        // 3. Web Edge Stability (Canny)
        cv::Canny(processed, processed, 50, 150);
        
        // Find contours
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(processed, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        
        // Draw significant contours (potential defects)
        for (const auto& contour : contours) {
            double area = cv::contourArea(contour);
            if (area > 100) { // Filter small noise
                 // Draw in WHITE (255) for Mono8 Optimized Result
                 cv::drawContours(output, std::vector<std::vector<cv::Point>>{contour}, -1, cv::Scalar(255), 2);
                 
                 // SIMULATED TRIGGER
                 if (area > 5000) { 
                     EventController::instance().triggerEvent();
                     cv::putText(output, "TRIGGERED!", cv::Point(10, 80), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255), 2);
                 }
            }
        }
        
        info += " | Defect Scan: ACTIVE";
    } else {
        // Defect Scan OFF
        info += " | Defect Scan: OFF";
    }
    // removed overlay drawing
}


void CameraManager::setGlobalFrameRate(double fps) {
    fps_ = static_cast<int>(fps);
    std::cout << "[CameraManager] Setting global FPS to " << fps << std::endl;

    try {
        for (int i = 0; i < static_cast<int>(cameraRuntimes_.size()); ++i) {
            auto* camera = getCameraByConfigIndex(i);
            if (!camera || !camera->IsPylonDeviceAttached() || !camera->IsOpen()) {
                continue;
            }

            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            try {
                Pylon::CFloatParameter(nodemap, "AcquisitionFrameRate").SetValue(fps);
            } catch (...) {
                std::cerr << "[CameraManager] Could not set AcquisitionFrameRate on camera " << i << std::endl;
            }
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Pylon Error during FPS set: " << e.GetDescription() << std::endl;
    }
}

void CameraManager::setCameraFrameRate(int cameraIndex, double fps, bool enableFrameRate) {
    std::cout << "[CameraManager] Setting FPS for Camera " << cameraIndex << " to " << fps
              << " (enable=" << enableFrameRate << ")" << std::endl;

    auto* camera = getCameraByConfigIndex(cameraIndex);
    if (!camera) {
        std::cerr << "[CameraManager] Invalid camera index for FPS update: " << cameraIndex << std::endl;
        return;
    }

    try {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
             GenApi::INodeMap& nodemap = camera->GetNodeMap();

             try {
                GenApi::CBooleanPtr enableNode = nodemap.GetNode("AcquisitionFrameRateEnable");
                if (enableNode && GenApi::IsWritable(enableNode)) {
                    enableNode->SetValue(enableFrameRate);
                }
                if (enableFrameRate) {
                    Pylon::CFloatParameter(nodemap, "AcquisitionFrameRate").SetValue(fps);
                }
             } catch (...) {
                std::cerr << "[CameraManager] Could not set AcquisitionFrameRate on camera " << cameraIndex << std::endl;
             }
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Pylon Error during individual FPS set: " << e.GetDescription() << std::endl;
    }
}

void CameraManager::setGlobalResolution(int binningFactor) {
    std::cout << "[CameraManager] Setting global Binning to " << binningFactor << "x" << binningFactor << std::endl;

    try {
        for (int i = 0; i < static_cast<int>(cameraRuntimes_.size()); ++i) {
            auto* camera = getCameraByConfigIndex(i);
            if (!camera || !camera->IsPylonDeviceAttached() || !camera->IsOpen()) {
                continue;
            }

            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            try {
                if (Pylon::CIntegerParameter(nodemap, "BinningHorizontal").IsWritable()) {
                    Pylon::CIntegerParameter(nodemap, "BinningHorizontal").SetValue(binningFactor);
                }

                if (Pylon::CIntegerParameter(nodemap, "BinningVertical").IsWritable()) {
                    Pylon::CIntegerParameter(nodemap, "BinningVertical").SetValue(binningFactor);
                }
            } catch (...) {
                std::cerr << "[CameraManager] Binning not supported on camera " << i << std::endl;
            }
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Pylon Error during Binning set: " << e.GetDescription() << std::endl;
    }
}

std::vector<GigEDeviceInfo> CameraManager::enumerateGigEDevices() {
    std::vector<GigEDeviceInfo> devices;
    try {
        Pylon::CTlFactory& TlFactory = Pylon::CTlFactory::GetInstance();
        Pylon::IGigETransportLayer* pTl = dynamic_cast<Pylon::IGigETransportLayer*>(TlFactory.CreateTl(Pylon::BaslerGigEDeviceClass));
        if (pTl == nullptr) {
            std::cerr << "[CameraManager] Error: No GigE transport layer installed." << std::endl;
            return devices;
        }

        Pylon::DeviceInfoList_t lstDevices;
        pTl->EnumerateAllDevices(lstDevices);

        for (const auto& dev : lstDevices) {
            GigEDeviceInfo info;
            info.friendlyName = dev.GetFriendlyName().c_str();
            info.macAddress = dev.GetMacAddress().c_str();
            
            Pylon::String_t val;
            if (dev.GetPropertyValue("IpAddress", val)) info.ipAddress = val.c_str();
            if (dev.GetPropertyValue("SubnetMask", val)) info.subnetMask = val.c_str();
            if (dev.GetPropertyValue("DefaultGateway", val)) info.defaultGateway = val.c_str();
            if (dev.GetPropertyValue("UserDefinedName", val)) info.userDefinedName = val.c_str();
            
            devices.push_back(info);
        }
        
        TlFactory.ReleaseTl(pTl);
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Error enumerating GigE devices: " << e.GetDescription() << std::endl;
    }
    return devices;
}

bool CameraManager::applyIpConfiguration(const std::string& mac, const std::string& ip, const std::string& mask, const std::string& gateway) {
    try {
        Pylon::CTlFactory& TlFactory = Pylon::CTlFactory::GetInstance();
        Pylon::IGigETransportLayer* pTl = dynamic_cast<Pylon::IGigETransportLayer*>(TlFactory.CreateTl(Pylon::BaslerGigEDeviceClass));
        if (pTl == nullptr) {
            std::cerr << "[CameraManager] Error: No GigE transport layer installed." << std::endl;
            return false;
        }

        // Find user defined name and validate device exists
        Pylon::DeviceInfoList_t lstDevices;
        pTl->EnumerateAllDevices(lstDevices);
        const std::string targetMac = normalizeMacAddress(mac);
        std::string userDefinedName = "";
        std::string currentIp;
        Pylon::CDeviceInfo matchedDeviceInfo;
        bool found = false;
        for (const auto& dev : lstDevices) {
            const std::string enumeratedMac = normalizeMacAddress(dev.GetMacAddress().c_str());
            if (enumeratedMac == targetMac) {
                found = true;
                matchedDeviceInfo = dev;
                userDefinedName = dev.GetUserDefinedName().c_str();
                Pylon::String_t val;
                if (dev.GetPropertyValue("IpAddress", val)) {
                    currentIp = val.c_str();
                }
                break;
            }
        }

        if (!found) {
            std::cerr << "[CameraManager] Cannot apply IP config: target MAC " << mac
                      << " was not found in the current GigE device discovery list." << std::endl;
            TlFactory.ReleaseTl(pTl);
            return false;
        }

        std::cout << "[CameraManager] Applying GigE IP config: input MAC=" << mac
                  << " normalized=" << targetMac
                  << " currentIp=" << (currentIp.empty() ? "<unknown>" : currentIp)
                  << " targetIp=" << ip
                  << " mask=" << mask
                  << " gateway=" << gateway << std::endl;

        // Prefer the direct device API when the camera is currently reachable.
        // This writes the persistent IP and enables persistent-IP mode on the device itself.
        try {
            std::unique_ptr<Pylon::IPylonDevice> device(TlFactory.CreateDevice(matchedDeviceInfo));
            Pylon::CBaslerGigEInstantCamera camera(device.release());
            camera.Open();
            camera.SetPersistentIpAddress(ip.c_str(), mask.c_str(), gateway.c_str());
            camera.ChangeIpConfiguration(true, false);
            camera.Close();
            TlFactory.ReleaseTl(pTl);
            std::cout << "[CameraManager] Successfully changed persistent IP for MAC " << targetMac
                      << " to " << ip << " using direct GigE device API." << std::endl;
            return true;
        } catch (const Pylon::GenericException& e) {
            std::cerr << "[CameraManager] Direct GigE IP configuration failed for MAC " << targetMac
                      << ": " << e.GetDescription() << ". Falling back to broadcast IP configuration." << std::endl;
        }

        // Pylon SDK requires MAC address with NO delimiters (e.g., "003053061a58")
        // and the camera must NOT be open when reconfiguring IP.
        // Use the normalized (delimiter-free, uppercase) MAC for all transport layer calls.
        bool setOk = pTl->BroadcastIpConfiguration(targetMac.c_str(), true, false, ip.c_str(), mask.c_str(), gateway.c_str(), userDefinedName.c_str());
        
        if (setOk) {
            pTl->RestartIpConfiguration(targetMac.c_str());
            std::cout << "[CameraManager] Successfully changed IP for MAC " << targetMac << " to " << ip << std::endl;
        } else {
            std::cerr << "[CameraManager] Failed to change IP for MAC " << targetMac << " (input=" << mac << ")" << std::endl;
        }
        
        TlFactory.ReleaseTl(pTl);
        return setOk;
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Error applying IP config: " << e.GetDescription() << std::endl;
        return false;
    }
}
