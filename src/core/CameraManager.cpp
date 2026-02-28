#include "CameraManager.h"
#include "../config/CameraConfig.h"
#include <iostream>
#include <chrono>
#include <pylon/gige/GigETransportLayer.h>

// Use Pylon namespace
using namespace Pylon;

CameraManager::CameraManager(int numCameras) 
    : numCameras_(numCameras), acquiring_(false), width_(782), height_(582), fps_(10.0), 
      defectDetectionEnabled_(false) {
    // Pylon requires initialization
    
    // Check Config for Source
    if (CameraConfig::getCameraSource() == CameraConfig::CameraSource::RealCamera) {
        std::cout << "[CameraManager] Configured for REAL CAMERA. Disabling Emulation." << std::endl;
        unsetenv("PYLON_CAMEMU");
    } else {
        std::cout << "[CameraManager] Configured for EMULATION." << std::endl;
        setenv("PYLON_CAMEMU", std::to_string(numCameras).c_str(), 1); 
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
    }
}

CameraManager::~CameraManager() {
    stopAcquisition();
    try {
        PylonTerminate();
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Failed to terminate Pylon: " << e.GetDescription() << std::endl;
    }
}

bool CameraManager::initialize() {
    try {
        CTlFactory& tlFactory = CTlFactory::GetInstance();
        DeviceInfoList_t devices;
        
        // Find all attached devices. 
        // With PYLON_CAMEMU=8, this will find 8 emulated devices.
        if (tlFactory.EnumerateDevices(devices) == 0) {
            std::cerr << "[CameraManager] No cameras found!" << std::endl;
            return false;
        }

        std::cout << "[CameraManager] Found " << devices.size() << " Pylon devices." << std::endl;

        // Initialize camera array
        cameras_.Initialize(std::min((int)devices.size(), numCameras_));

        for (size_t i = 0; i < cameras_.GetSize(); ++i) {
            cameras_[i].Attach(tlFactory.CreateDevice(devices[i]));
            // Set context to identify the camera in the grab result
            cameras_[i].SetCameraContext(i);
            
            // Apply industrial configuration (PTP, MTU 9000)
            // Use GetNodeMap() directly from the camera object, which returns INodeMap&
            configureCamera(cameras_[i].GetNodeMap(), cameras_[i].GetDeviceInfo().GetDeviceClass() == "BaslerCamEmu");

            std::cout << "[CameraManager] Attached camera " << i << ": " 
                      << cameras_[i].GetDeviceInfo().GetModelName() << std::endl;
            
            // Store model name
            if (i < modelNames_.size()) {
                modelNames_[i] = cameras_[i].GetDeviceInfo().GetModelName().c_str();
            }
        }

        return true;
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Pylon exception during initialization: " 
                  << e.GetDescription() << std::endl;
        return false;
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
        cameras_.Open();
        
        // Update width/height
        if (cameras_.GetSize() > 0) {
            GenApi::CIntegerPtr ptrWidth(cameras_[0].GetNodeMap().GetNode("Width"));
            GenApi::CIntegerPtr ptrHeight(cameras_[0].GetNodeMap().GetNode("Height"));
            if (IsReadable(ptrWidth) && IsReadable(ptrHeight)) {
                width_ = (int)ptrWidth->GetValue();
                height_ = (int)ptrHeight->GetValue();
                std::cout << "[CameraManager] Updated resolution from camera: " << width_ << "x" << height_ << std::endl;
            }
        }
        
        // Initialize buffer pools
        bufferPools_.clear();
        for (int i = 0; i < numCameras_; ++i) {
            bufferPools_.push_back(
                std::make_unique<BufferPool>(3, width_, height_, CV_8UC1)
            );
        }
        
        // Initialize latest frames
        {
            std::lock_guard<std::mutex> lock(latestFramesMutex_);
            latestFrames_.assign(numCameras_, cv::Mat());
        }
        
        // Camera Configuration
        for (size_t i = 0; i < cameras_.GetSize(); ++i) {
            cameras_[i].MaxNumBuffer.SetValue(5);
        }
        
        cameras_.StartGrabbing(GrabStrategy_LatestImageOnly);

        // Debug info for Stream Grabber (as per Grab.cpp)
        for (size_t i = 0; i < cameras_.GetSize(); ++i) {
             if (cameras_[i].IsGigE()) {
                 std::cout << "[CameraManager] Cam " << i << " Stream grabber uses " 
                           << Pylon::CEnumParameter(cameras_[i].GetStreamGrabberNodeMap(), "Type").GetValueOrDefault("Other") 
                           << std::endl;
             }
        }
        
        // Initialize Event Controller
        EventController::instance().initialize(100, 10.0, 50);

        acquiring_ = true;
        acquisitionThread_ = std::thread(&CameraManager::acquisitionLoop, this);
        
        std::cout << "[CameraManager] Started Pylon acquisition loop." << std::endl;
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Pylon exception during start: " 
                  << e.GetDescription() << std::endl;
    }
}

void CameraManager::stopAcquisition() {
    acquiring_ = false;
    if (acquisitionThread_.joinable()) {
        acquisitionThread_.join();
    }
    
    try {
        if (cameras_.IsGrabbing()) {
            cameras_.StopGrabbing();
        }
        if (cameras_.IsOpen()) {
            cameras_.Close();
        }
        // Detach all devices so they can be re-enumerated
        for (size_t i = 0; i < cameras_.GetSize(); ++i) {
            if (cameras_[i].IsPylonDeviceAttached()) {
                cameras_[i].DestroyDevice();
            }
        }
        cameras_.Initialize(0); // Reset camera array
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Pylon exception during stop: " 
                  << e.GetDescription() << std::endl;
    }
    
    // Clear buffer pools
    bufferPools_.clear();
}

void CameraManager::registerCallback(FrameCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callback_ = callback;
}

std::vector<std::string> CameraManager::getCameraLabels() const {
    return cameraLabels_;
}

std::string CameraManager::getModelName(int index) const {
    if (index >= 0 && index < (int)modelNames_.size()) {
        return modelNames_[index];
    }
    return "Unknown";
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

void CameraManager::acquisitionLoop() {
    std::cout << "[CameraManager] Entering acquisition loop." << std::endl;
    CGrabResultPtr ptrGrabResult;
    CImageFormatConverter formatConverter;
    formatConverter.OutputPixelFormat = PixelType_Mono8;

    CPylonImage pylonImage; 

    while (cameras_.IsGrabbing() && acquiring_) {
        try {
            if (cameras_.RetrieveResult(5000, ptrGrabResult, TimeoutHandling_ThrowException)) {
                if (!ptrGrabResult) continue;  

                uint32_t cameraIndex = (uint32_t)ptrGrabResult->GetCameraContext();

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
                        if (defectDetectionEnabled_) {
                             // Use 'this' to call processFrame
                             processFrame(wrapper, displayFrame, (int)cameraIndex);
                        } else {
                            displayFrame = wrapper; 
                        }

                        // 4. EVENT CONTROLLER
                        if (cameraIndex == 0) {
                            EventController::instance().addFrame(wrapper, timestamp, frameCounter);
                        }

                        // 5. CALLBACK
                        {
                            std::lock_guard<std::mutex> lock(callbackMutex_);
                            if (callback_) {
                                callback_((int)cameraIndex, displayFrame);
                            }
                        }
                    } else {
                        std::cout << "[CameraManager] Invalid frame: " << width << "x" << height << " Buffer: " << (pImageBuffer ? "OK" : "NULL") << std::endl;
                    }
                } else {
                    std::cerr << "[CameraManager] Grab failed: " 
                              << ptrGrabResult->GetErrorDescription() << std::endl;
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
            }
        } catch (const std::exception& e) {
            std::cerr << "[CameraManager] Standard exception in loop: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[CameraManager] Unknown exception in loop!" << std::endl;
        }
    }
    std::cout << "[CameraManager] Exiting acquisition loop." << std::endl;
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
    if (input.data != output.data) {
        input.copyTo(output);
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
    
    // If running, stop acquisition before changing params
    bool restart = acquiring_;
    if (restart) stopAcquisition();
    
    try {
        if (cameras_.GetSize() > 0) {
            for (size_t i = 0; i < cameras_.GetSize(); ++i) {
                if (cameras_[i].IsPylonDeviceAttached()) {
                     cameras_[i].Open(); // Must be open to write nodes
                     GenApi::INodeMap& nodemap = cameras_[i].GetNodeMap();
                     
                     // Helper to set float if available
                     try {
                        Pylon::CFloatParameter(nodemap, "AcquisitionFrameRate").SetValue(fps);
                     } catch (...) {
                        std::cerr << "[CameraManager] Could not set AcquisitionFrameRate on camera " << i << std::endl;
                     }
                     cameras_[i].Close(); 
                }
            }
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Pylon Error during FPS set: " << e.GetDescription() << std::endl;
    }
    
    if (restart) startAcquisition();
}

void CameraManager::setGlobalResolution(int binningFactor) {
    std::cout << "[CameraManager] Setting global Binning to " << binningFactor << "x" << binningFactor << std::endl;
    
    bool restart = acquiring_;
    if (restart) stopAcquisition();
    
    try {
        if (cameras_.GetSize() > 0) {
            for (size_t i = 0; i < cameras_.GetSize(); ++i) {
                if (cameras_[i].IsPylonDeviceAttached()) {
                     cameras_[i].Open();
                     GenApi::INodeMap& nodemap = cameras_[i].GetNodeMap();
                     
                     // Set Binning
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
                     
                     cameras_[i].Close();
                }
            }
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Pylon Error during Binning set: " << e.GetDescription() << std::endl;
    }
    
    if (restart) startAcquisition();
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
            info.ipAddress = dev.GetIpAddress().c_str();
            info.subnetMask = dev.GetSubnetMask().c_str();
            info.defaultGateway = dev.GetDefaultGateway().c_str();
            info.userDefinedName = dev.GetUserDefinedName().c_str();
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

        // Find user defined name
        Pylon::DeviceInfoList_t lstDevices;
        pTl->EnumerateAllDevices(lstDevices);
        std::string userDefinedName = "";
        for (const auto& dev : lstDevices) {
            if (dev.GetMacAddress().c_str() == mac) {
                userDefinedName = dev.GetUserDefinedName().c_str();
                break;
            }
        }

        // isStatic = true, isDhcp = false for fixed IP broadcast
        bool setOk = pTl->BroadcastIpConfiguration(mac.c_str(), true, false, ip.c_str(), mask.c_str(), gateway.c_str(), userDefinedName.c_str());
        
        if (setOk) {
            pTl->RestartIpConfiguration(mac.c_str());
            std::cout << "[CameraManager] Successfully changed IP for MAC " << mac << " to " << ip << std::endl;
        } else {
            std::cerr << "[CameraManager] Failed to change IP for MAC " << mac << std::endl;
        }
        
        TlFactory.ReleaseTl(pTl);
        return setOk;
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Error applying IP config: " << e.GetDescription() << std::endl;
        return false;
    }
}
