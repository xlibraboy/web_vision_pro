#include "CameraManager.h"
#include "../config/CameraConfig.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <pylon/gige/GigETransportLayer.h>

// Use Pylon namespace
using namespace Pylon;

// DeviceRemovalHandler Implementation
void CameraManager::DeviceRemovalHandler::OnCameraDeviceRemoved(Pylon::CInstantCamera& camera) {
    if (manager_) {
        std::cout << "[CameraManager] 🚨 DEVICE REMOVAL EVENT DETECTED AND TRIGGERED 🚨" << std::endl;
        
        // When a single camera drops from a multi-camera array, the RetrieveResult 
        // often just hangs or spits out non-fatal incomplete buffers instead of throwing
        // a GenericException. So we MUST forcefully terminate the acquisition loop from here!
        if (!manager_->recovering_ && manager_->acquiring_) {
            manager_->acquiring_ = false; // Force the main loop to exit its next iteration
            
            // Detach thread safely and launch recovery
            if (manager_->recoveryThread_.joinable()) manager_->recoveryThread_.join();
            manager_->recoveryThread_ = std::thread(&CameraManager::recoveryLoop, manager_);
        }
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
        std::vector<Pylon::CDeviceInfo> activeDevices;
        
        std::cout << "[CameraManager] Loaded " << configuredCams.size() << " Camera configs." << std::endl;
        for (const auto& c : configuredCams) {
            std::cout << "[CameraManager] Config - ID: " << c.id << " Source: " << c.source << " MAC: " << c.macAddress.toStdString() << std::endl;
        }
        
        // Detect if running in emulation mode from environment
        bool isEmulationEnv = (getenv("PYLON_CAMEMU") != nullptr);
        
        // Match discovered devices against configuration
        cameraIndexToConfigId_.clear();
        std::vector<int> matchedConfigIds; // To track in order
        
        for (const auto& camInfo : configuredCams) {
            bool foundMatch = false;
            
            if (camInfo.source == 2) { 
                std::cout << "[CameraManager] Camera ID " << camInfo.id << " is DISABLED. Skipping." << std::endl;
                continue;
            }
            
            for (const auto& dev : devices) {
                bool isEmulatedDevice = (dev.GetDeviceClass() == "BaslerCamEmu");
                
                bool canMatch = false;
                
                if (camInfo.source == 0 && isEmulatedDevice) {
                    canMatch = true;
                } else if (camInfo.source == 1) {
                    if (!isEmulatedDevice) {
                        // Strict Match: Camera must have an explicit MAC set to connect.
                        if (!camInfo.macAddress.isEmpty() && 
                            camInfo.macAddress != "None / Auto" && 
                            camInfo.macAddress.toStdString() == dev.GetMacAddress().c_str()) {
                            canMatch = true;
                        }
                    }
                }
                
                if (canMatch) {
                    bool alreadyUsed = false;
                    for (const auto& active : activeDevices) {
                        if (active.GetSerialNumber() == dev.GetSerialNumber()) { alreadyUsed = true; break;}
                    }
                    if (!alreadyUsed) {
                        activeDevices.push_back(dev);
                        matchedConfigIds.push_back(camInfo.id); // <-- store config ID in order
                        foundMatch = true;
                        std::string statusMsg = "[CameraManager] Matched " + std::string(isEmulatedDevice ? "EMULATED" : "REAL") 
                                  + " camera" + (isEmulatedDevice ? "" : std::string(" (") + dev.GetMacAddress().c_str() + ")")
                                  + " for Config ID " + std::to_string(camInfo.id)
                                  + " -> Pylon index " + std::to_string(activeDevices.size() - 1);
                        std::cout << statusMsg << std::endl;
                        if (statusCallback_) statusCallback_(statusMsg);
                        break;
                    }
                }
            }
            
            if (!foundMatch) {
                std::cerr << "[CameraManager] WARNING: Could not find matching physical device for Camera ID " 
                          << camInfo.id << " (Source: " << (camInfo.source == 0 ? "Emulated" : "Real") << ")" << std::endl;
            }
        }

        // Initialize camera array with active matched devices ONLY
        size_t numToInitialize = std::min(activeDevices.size(), (size_t)numCameras_);
        cameras_.Initialize(numToInitialize);
        
        // ONLY clear and build targetDevices_ on the very first fresh startup!
        // During a degraded recovery rebuild, we MUST preserve the original targetDevices_ 
        // list so we don't forget about the missing camera!
        bool firstInit = targetDevices_.empty();
        cameraIndexToConfigId_.clear();

        for (size_t i = 0; i < cameras_.GetSize(); ++i) {
            cameras_[i].Attach(tlFactory.CreateDevice(activeDevices[i]));
            
            // Store Config ID mapping (index i -> config ID from matched list)
            int configId = (i < matchedConfigIds.size()) ? matchedConfigIds[i] : (int)(i + 1);
            cameraIndexToConfigId_.push_back(configId);
            std::cout << "[CameraManager] Pylon index " << i 
                      << " -> Config ID " << configId << std::endl;
            
            // Store device properties for recovery (ONLY on first successful startup)
            if (firstInit) {
                CDeviceInfo targetInfo;
                targetInfo.SetDeviceClass(cameras_[i].GetDeviceInfo().GetDeviceClass());
                targetInfo.SetSerialNumber(cameras_[i].GetDeviceInfo().GetSerialNumber());
                targetDevices_.push_back(targetInfo);
            }
            
            // Register Device Removal Handler
            cameras_[i].RegisterConfiguration(new DeviceRemovalHandler(this), RegistrationMode_Append, Cleanup_Delete);

            // Set context to Pylon array index (still needed for frame routing)
            cameras_[i].SetCameraContext(i);
            
            // Apply industrial configuration (PTP, MTU 9000)
            configureCamera(cameras_[i].GetNodeMap(), cameras_[i].GetDeviceInfo().GetDeviceClass() == "BaslerCamEmu");

            std::cout << "[CameraManager] Attached camera " << i << " (Config ID " << configId << "): " 
                      << cameras_[i].GetDeviceInfo().GetModelName() << std::endl;
            
            // Store model name
            if (i < modelNames_.size()) {
                modelNames_[i] = cameras_[i].GetDeviceInfo().GetModelName().c_str();
            }
        }

        // Also build reverse map: config array index (0-based) -> Pylon index
        // This allows getTemperature/getModelName to look up the device by UI slot index
        configArrayIndexToPylonIndex_.clear();
        {
            auto configuredCamsForMap = CameraConfig::getCameras();
            for (int cfgArrayIdx = 0; cfgArrayIdx < (int)configuredCamsForMap.size(); ++cfgArrayIdx) {
                int cfgId = configuredCamsForMap[cfgArrayIdx].id;
                int pylonIdx = -1; // -1 means not connected
                for (int pi = 0; pi < (int)cameraIndexToConfigId_.size(); ++pi) {
                    if (cameraIndexToConfigId_[pi] == cfgId) {
                        pylonIdx = pi;
                        break;
                    }
                }
                configArrayIndexToPylonIndex_.push_back(pylonIdx);
                std::cout << "[CameraManager] Config array index " << cfgArrayIdx
                          << " (Config ID " << cfgId << ") -> Pylon index " << pylonIdx << std::endl;
            }
            
            // Build the inverse: pylon index -> config array index
            // Size = number of connected cameras (Pylon array size)
            pylonIndexToConfigArrayIndex_.assign(cameras_.GetSize(), -1);
            for (int cfgArrayIdx = 0; cfgArrayIdx < (int)configArrayIndexToPylonIndex_.size(); ++cfgArrayIdx) {
                int pi = configArrayIndexToPylonIndex_[cfgArrayIdx];
                if (pi >= 0 && pi < (int)pylonIndexToConfigArrayIndex_.size()) {
                    pylonIndexToConfigArrayIndex_[pi] = cfgArrayIdx;
                } else {
                    // Camera is disconnected or missing. Send empty frame to clear GUI.
                    std::lock_guard<std::mutex> lock(callbackMutex_);
                    if (callback_) {
                        callback_(cfgArrayIdx, cv::Mat());
                    }
                }
            }
        } // end reverse map scope
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
    if (cameraIndex < 0 || cameraIndex >= (int)cameras_.GetSize()) return;
    try {
        if (cameras_[cameraIndex].IsPylonDeviceAttached() && cameras_[cameraIndex].IsOpen()) {
            GenApi::INodeMap& nodemap = cameras_[cameraIndex].GetNodeMap();
            Pylon::CFloatParameter(nodemap, "Gain").SetValue(gain);
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Error setting Gain for cam " << cameraIndex
                  << ": " << e.GetDescription() << std::endl;
    }
}

void CameraManager::setCameraExposure(int cameraIndex, double exposureUs) {
    if (cameraIndex < 0 || cameraIndex >= (int)cameras_.GetSize()) return;
    try {
        if (cameras_[cameraIndex].IsPylonDeviceAttached() && cameras_[cameraIndex].IsOpen()) {
            GenApi::INodeMap& nodemap = cameras_[cameraIndex].GetNodeMap();
            Pylon::CFloatParameter(nodemap, "ExposureTimeAbs").SetValue(exposureUs);
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Error setting ExposureTime for cam " << cameraIndex
                  << ": " << e.GetDescription() << std::endl;
    }
}

void CameraManager::setCameraGamma(int cameraIndex, double gamma) {
    if (cameraIndex < 0 || cameraIndex >= (int)cameras_.GetSize()) return;
    try {
        if (cameras_[cameraIndex].IsPylonDeviceAttached() && cameras_[cameraIndex].IsOpen()) {
            GenApi::INodeMap& nodemap = cameras_[cameraIndex].GetNodeMap();
            Pylon::CFloatParameter(nodemap, "Gamma").SetValue(gamma);
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Error setting Gamma for cam " << cameraIndex
                  << ": " << e.GetDescription() << std::endl;
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
        // Update width/height
        if (cameras_.GetSize() > 0) {
            cameras_.Open();
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
        
        // Only start Pylon grabbing if we actually have cameras
        if (cameras_.GetSize() > 0) {
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

            // === TEMPERATURE MONITORING: Enable Pylon camera events per Basler App Note ===
            for (size_t i = 0; i < cameras_.GetSize(); ++i) {
                try {
                    GenApi::INodeMap& nodemap = cameras_[i].GetNodeMap();
                    // Only applicable to real (non-emulated) cameras
                    bool isEmu = (cameras_[i].GetDeviceInfo().GetDeviceClass() == "BaslerCamEmu");
                    if (!isEmu) {
                        GenApi::CEnumerationPtr ptrEvtSel(nodemap.GetNode("EventSelector"));
                        GenApi::CEnumerationPtr ptrEvtNotif(nodemap.GetNode("EventNotification"));
                        if (IsWritable(ptrEvtSel) && IsWritable(ptrEvtNotif)) {
                            // Enable CriticalTemperature event
                            if (GenApi::IsAvailable(ptrEvtSel->GetEntryByName("CriticalTemperature"))) {
                                ptrEvtSel->FromString("CriticalTemperature");
                                ptrEvtNotif->FromString("On");
                                std::cout << "[CameraManager] CriticalTemperature event enabled on camera " << i << std::endl;
                            }
                            // Enable OverTemperature event
                            if (GenApi::IsAvailable(ptrEvtSel->GetEntryByName("OverTemperature"))) {
                                ptrEvtSel->FromString("OverTemperature");
                                ptrEvtNotif->FromString("On");
                                std::cout << "[CameraManager] OverTemperature event enabled on camera " << i << std::endl;
                            }
                        }
                    }
                } catch (const GenericException& e) {
                    std::cout << "[CameraManager] Temp event setup warning (cam " << i << "): " << e.GetDescription() << std::endl;
                }
            }
        }

        // Start background temperature monitor thread (runs regardless of array size)
        prevTempStatus_.assign(cameras_.GetSize(), TemperatureStatus::Unknown);
        
        // Safely join old threads if they exist to prevent std::terminate on unjoined threads
        if (tempMonitorThread_.joinable()) {
            tempMonitorRunning_ = false; 
            tempMonitorThread_.join();
        }
        
        tempMonitorRunning_ = true;
        tempMonitorThread_ = std::thread(&CameraManager::temperatureMonitorLoop, this);

        if (acquisitionThread_.joinable()) {
            // acquiring_ is already false if this is running or recovering
            acquisitionThread_.join();
        }
        
        if (cameras_.GetSize() > 0) {
            acquiring_ = true;
            acquisitionThread_ = std::thread(&CameraManager::acquisitionLoop, this);
            std::cout << "[CameraManager] Started Pylon acquisition loop." << std::endl;
        } else {
            acquiring_ = false;
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

    // Stop temperature monitor thread
    tempMonitorRunning_ = false;
    if (tempMonitorThread_.joinable()) {
        tempMonitorThread_.join();
    }

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

void CameraManager::pauseGrabbing(bool pause) {
    paused_ = pause;
    std::cout << "[CameraManager] Grabbing " << (pause ? "PAUSED" : "RESUMED") << std::endl;
}

bool CameraManager::isGrabbingPaused() const {
    return paused_;
}

void CameraManager::recoveryLoop() {
    recovering_ = true;
    std::cout << "[CameraManager] --- RECOVERY LOOP STARTED ---" << std::endl;

    // 1. Give the system time to cleanly disconnect everything
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // 2. Terminate the old camera handles safely
    try {
        if (cameras_.IsGrabbing()) cameras_.StopGrabbing();
        if (cameras_.IsOpen()) cameras_.Close();
        for (size_t i = 0; i < cameras_.GetSize(); ++i) {
            if (cameras_[i].IsPylonDeviceAttached()) {
                cameras_[i].DestroyDevice();
            }
        }
    } catch (...) {
        std::cerr << "[CameraManager] Ignored exception during old handle teardown in recovery." << std::endl;
    }
    
    // 3. IMMEDIATELY try to re-initialize with any *surviving* cameras so the user doesn't lose all views!
    if (initialize()) {
        std::cout << "[CameraManager] Re-init with surviving cameras complete. Restarting degraded acquisition..." << std::endl;
        startAcquisition(); 
    }
    
    CTlFactory& tlFactory = CTlFactory::GetInstance();
    
    // 4. Continuously poll for the target devices in the background
    while (recovering_) {
        int activeCount = cameras_.GetSize();
        auto configuredCams = CameraConfig::getCameras();
        int expectedCount = configuredCams.size();
        
        // If we miraculously have all expected cameras, exit recovery entirely
        if (activeCount >= expectedCount) {
             std::cout << "[CameraManager] All expected cameras are active. Exiting recovery loop." << std::endl;
             recovering_ = false;
             if (statusCallback_) statusCallback_("[CameraManager] System Fully Recovered.");
             break;
        }
        
        // Count how many physical devices match the config right now
        int matchedPhysicalCount = 0;
        DeviceInfoList_t allDevices;
        if (tlFactory.EnumerateDevices(allDevices) > 0) {
            for (const auto& camInfo : configuredCams) {
                bool isEmulatedDevice = (camInfo.source == 0);
                std::string targetMac = camInfo.macAddress.toStdString();
                targetMac.erase(std::remove(targetMac.begin(), targetMac.end(), ':'), targetMac.end());
                
                for (const auto& dev : allDevices) {
                    if (isEmulatedDevice && dev.GetDeviceClass() == "BaslerCamEmu") {
                        matchedPhysicalCount++;
                        break;
                    } else if (!isEmulatedDevice && dev.GetDeviceClass() == "BaslerGigE" && dev.GetMacAddress() == targetMac.c_str()) {
                        matchedPhysicalCount++;
                        break;
                    }
                }
            }
        }
        
        // If the number of matching physical devices is greater than what's currently in the array,
        // OR acquiring_ has crashed again, we need to completely rebuild the array!
        if (matchedPhysicalCount > activeCount || (!acquiring_ && activeCount > 0)) {
            std::cout << "[CameraManager] Detected newly returned camera (or degraded loop crashed). Rebuilding array..." << std::endl;
            
            acquiring_ = false;
            if (acquisitionThread_.joinable()) acquisitionThread_.join();
            
            try {
                if (cameras_.IsGrabbing()) cameras_.StopGrabbing();
                if (cameras_.IsOpen()) cameras_.Close();
                for (size_t i = 0; i < cameras_.GetSize(); ++i) {
                    if (cameras_[i].IsPylonDeviceAttached()) cameras_[i].DestroyDevice();
                }
            } catch (...) {}
            
            // Wait an extra second for hardware stability before seizing control
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            
            if (initialize()) {
                std::cout << "[CameraManager] Re-init successful. Restarting acquisition..." << std::endl;
                startAcquisition(); 
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Poll every second
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
    // Translate config array index -> Pylon index using the reverse map
    int pylonIndex = -1;
    if (configArrayIndex >= 0 && configArrayIndex < (int)configArrayIndexToPylonIndex_.size()) {
        pylonIndex = configArrayIndexToPylonIndex_[configArrayIndex];
    }
    
    if (pylonIndex < 0) {
        return "Not Connected";
    }
    
    // Try to get live data if camera is attached
    try {
        if (pylonIndex >= 0 && pylonIndex < (int)cameras_.GetSize() && cameras_[pylonIndex].IsPylonDeviceAttached()) {
            return cameras_[pylonIndex].GetDeviceInfo().GetModelName().c_str();
        }
    } catch (...) {}
    
    // Fallback to cache
    if (pylonIndex >= 0 && pylonIndex < (int)modelNames_.size()) {
        return modelNames_[pylonIndex];
    }
    return "Unknown Model";
}

double CameraManager::getTemperature(int configArrayIndex) {
    // Translate config array index -> Pylon index using the reverse map
    int pylonIndex = -1;
    if (configArrayIndex >= 0 && configArrayIndex < (int)configArrayIndexToPylonIndex_.size()) {
        pylonIndex = configArrayIndexToPylonIndex_[configArrayIndex];
    }
    
    if (pylonIndex < 0) {
        return -1.0; // Sentinel: camera not connected
    }
    
    try {
        if (pylonIndex < (int)cameras_.GetSize() && cameras_[pylonIndex].IsPylonDeviceAttached() && cameras_[pylonIndex].IsOpen()) {
            GenApi::INodeMap& nodemap = cameras_[pylonIndex].GetNodeMap();
            
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
                GenApi::INodeMap& tlNodemap = cameras_[pylonIndex].GetTLNodeMap();
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

                if (paused_) continue; // Skip frame processing when paused

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
                             processFrame(wrapper, displayFrame, (int)cameraIndex);
                        } else {
                            displayFrame = wrapper; 
                        }

                        // Resolve the Config ID from the Pylon index
                        int configId = (cameraIndex < cameraIndexToConfigId_.size())
                                        ? cameraIndexToConfigId_[cameraIndex]
                                        : (int)(cameraIndex + 1); // fallback

                        // 4. EVENT CONTROLLER: feed all connected cameras, using 1-based configId
                        EventController::instance().addFrame(configId, wrapper, timestamp, frameCounter);

                        // 5. CALLBACK: emit config array index (0-based UI slot) resolved from Pylon index
                        {
                            std::lock_guard<std::mutex> lock(callbackMutex_);
                            if (callback_) {
                                // Resolve pylon index -> config array index using reverse map
                                int configArrayIdx = (cameraIndex < pylonIndexToConfigArrayIndex_.size())
                                                         ? pylonIndexToConfigArrayIndex_[cameraIndex]
                                                         : (int)cameraIndex;
                                callback_(configArrayIdx, displayFrame);
                            }
                        }
                    } else {
                        std::cout << "[CameraManager] Invalid frame: " << width << "x" << height << " Buffer: " << (pImageBuffer ? "OK" : "NULL") << std::endl;
                    }
                } else {
                    std::cerr << "[CameraManager] Grab failed: " 
                              << ptrGrabResult->GetErrorDescription() << std::endl;
                              
                    // Some cameras/switches report disconnects as non-fatal grab failures
                    bool deviceRemoved = false;
                    
                    // If the array completely collapsed (size 0), we must recover!
                    if (cameras_.GetSize() < targetDevices_.size() || cameras_.GetSize() == 0) {
                         deviceRemoved = true;
                         std::cerr << "[CameraManager] Array collapsed or cameras missing. Forcing recovery." << std::endl;
                         
                         // Emit blank frames for all previously known cameras so the UI updates
                         for (size_t i = 0; i < pylonIndexToConfigArrayIndex_.size(); ++i) {
                              int configArrayIdx = pylonIndexToConfigArrayIndex_[i];
                              if (configArrayIdx >= 0) {
                                  std::lock_guard<std::mutex> lock(callbackMutex_);
                                  if (callback_) callback_(configArrayIdx, cv::Mat());
                              }
                         }
                    } else {
                        for (size_t i = 0; i < cameras_.GetSize(); ++i) {
                            if (cameras_[i].IsCameraDeviceRemoved()) {
                                std::cerr << "[CameraManager] Hardware disconnect confirmed on Pylon Camera " << i << std::endl;
                                
                                int configArrayIdx = (i < pylonIndexToConfigArrayIndex_.size())
                                                         ? pylonIndexToConfigArrayIndex_[i]
                                                         : (int)i;
                                {
                                    std::lock_guard<std::mutex> lock(callbackMutex_);
                                    if (callback_) {
                                        callback_(configArrayIdx, cv::Mat());
                                    }
                                }
                                
                                deviceRemoved = true;
                            }
                        }
                    }
                    
                    if (deviceRemoved && !recovering_) {
                        acquiring_ = false; // Stop this loop
                        
                        // Detach thread safely and launch recovery
                        // We cannot join our own thread, the recoveryThread_.join() should happen in a new thread context
                        // But since we are going to launch recoveryThread_, we join the *old* one if it exists.
                        if (recoveryThread_.joinable()) recoveryThread_.join();
                        recoveryThread_ = std::thread(&CameraManager::recoveryLoop, this);
                        break; // Exit this acquisition thread immediately
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
                          
                // Check if the exception was caused by device removal or array crash
                Pylon::WaitObject::Sleep(1000); 
                bool deviceRemoved = false;
                
                // If the array completely collapsed (size 0), we must recover!
                if (cameras_.GetSize() < targetDevices_.size() || cameras_.GetSize() == 0) {
                     deviceRemoved = true;
                     std::cerr << "[CameraManager] Array collapsed or cameras missing. Forcing recovery." << std::endl;
                     
                     // Emit blank frames for all previously known cameras so the UI updates
                     for (size_t i = 0; i < pylonIndexToConfigArrayIndex_.size(); ++i) {
                          int configArrayIdx = pylonIndexToConfigArrayIndex_[i];
                          if (configArrayIdx >= 0) {
                              std::lock_guard<std::mutex> lock(callbackMutex_);
                              if (callback_) callback_(configArrayIdx, cv::Mat());
                          }
                     }
                } else {
                    for (size_t i = 0; i < cameras_.GetSize(); ++i) {
                        if (cameras_[i].IsCameraDeviceRemoved()) {
                            std::cerr << "[CameraManager] Hardware disconnect confirmed on Pylon Camera " << i << std::endl;
                            
                            // Emit empty frame to correctly clear UI based on config index
                            int configArrayIdx = (i < pylonIndexToConfigArrayIndex_.size())
                                                     ? pylonIndexToConfigArrayIndex_[i]
                                                     : (int)i;
                            {
                                std::lock_guard<std::mutex> lock(callbackMutex_);
                                if (callback_) {
                                    callback_(configArrayIdx, cv::Mat());
                                }
                            }
                            
                            deviceRemoved = true;
                        }
                    }
                }
                
                if (deviceRemoved && !recovering_) {
                    acquiring_ = false; // Stop this loop
                    
                    // Detach thread safely and launch recovery
                    if (recoveryThread_.joinable()) recoveryThread_.join();
                    recoveryThread_ = std::thread(&CameraManager::recoveryLoop, this);
                    break; // Exit this acquisition thread immediately
                }
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
    
    if (restart) {
        if (cameras_.GetSize() > 0) {
            cameras_.StartGrabbing(Pylon::GrabStrategy_LatestImageOnly);
            acquiring_ = true;
            acquisitionThread_ = std::thread(&CameraManager::acquisitionLoop, this);
        } else {
            acquiring_ = false;
        }
    }
}

void CameraManager::setCameraFrameRate(int cameraIndex, double fps) {
    std::cout << "[CameraManager] Setting FPS for Camera " << cameraIndex << " to " << fps << std::endl;
    
    // Check if index is valid
    if (cameraIndex < 0 || cameraIndex >= (int)cameras_.GetSize()) {
        std::cerr << "[CameraManager] Invalid camera index for FPS update: " << cameraIndex << std::endl;
        return;
    }
    
    bool restart = acquiring_;
    if (restart) {
        acquiring_ = false;
        if (acquisitionThread_.joinable()) acquisitionThread_.join();
        if (cameras_.IsGrabbing()) cameras_.StopGrabbing();
    }
    
    try {
        if (cameras_[cameraIndex].IsPylonDeviceAttached()) {
             cameras_[cameraIndex].Open(); // Must be open to write nodes
             GenApi::INodeMap& nodemap = cameras_[cameraIndex].GetNodeMap();
             
             try {
                Pylon::CFloatParameter(nodemap, "AcquisitionFrameRate").SetValue(fps);
             } catch (...) {
                std::cerr << "[CameraManager] Could not set AcquisitionFrameRate on camera " << cameraIndex << std::endl;
             }
             cameras_[cameraIndex].Close(); 
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Pylon Error during individual FPS set: " << e.GetDescription() << std::endl;
    }
    
    if (restart) {
        if (cameras_.GetSize() > 0) {
            cameras_.StartGrabbing(Pylon::GrabStrategy_LatestImageOnly);
            acquiring_ = true;
            acquisitionThread_ = std::thread(&CameraManager::acquisitionLoop, this);
        } else {
            acquiring_ = false;
        }
    }
}

void CameraManager::setGlobalResolution(int binningFactor) {
    std::cout << "[CameraManager] Setting global Binning to " << binningFactor << "x" << binningFactor << std::endl;
    
    bool restart = acquiring_;
    if (restart) {
        acquiring_ = false;
        if (acquisitionThread_.joinable()) acquisitionThread_.join();
        if (cameras_.IsGrabbing()) cameras_.StopGrabbing();
    }
    
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
    
    if (restart) {
        if (cameras_.GetSize() > 0) {
            cameras_.StartGrabbing(Pylon::GrabStrategy_LatestImageOnly);
            acquiring_ = true;
            acquisitionThread_ = std::thread(&CameraManager::acquisitionLoop, this);
        } else {
            acquiring_ = false;
        }
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
