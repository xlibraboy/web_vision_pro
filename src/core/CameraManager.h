#pragma once

#include <opencv2/opencv.hpp>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <set>
#include <unordered_map>
#include <limits>

// Pylon Includes
#include <pylon/PylonIncludes.h>

// Shared lightweight temperature status (avoids circular dependencies)
#include "TemperatureStatus.h"

// CameraInfo for saveParametersForAll
#include "../gui/CameraInfo.h"

// Buffer pool for optimized memory management
#include "BufferPool.h"
#include "EventController.h"

// Define callback type for new frames (cameraId, frame)
using FrameCallback = std::function<void(int, const cv::Mat&)>;

// Struct to hold GigE Device Information for Network Config
struct GigEDeviceInfo {
    std::string friendlyName;
    std::string macAddress;
    std::string ipAddress;
    std::string subnetMask;
    std::string defaultGateway;
    std::string userDefinedName;
};

class CameraManager {
public:
    // Temperature status aliases — types defined in TemperatureStatus.h
    using TemperatureStatus = TempStatus::Status;
    static constexpr TemperatureStatus TS_Ok       = TempStatus::Ok;
    static constexpr TemperatureStatus TS_Critical  = TempStatus::Critical;
    static constexpr TemperatureStatus TS_Error     = TempStatus::Error;
    static constexpr TemperatureStatus TS_Unknown   = TempStatus::Unknown;

    // Callback fired from background thread when a camera's temp status changes
    using TempAlertCallback = std::function<void(int camId, double temp, TemperatureStatus status)>;
    void registerTemperatureAlertCallback(TempAlertCallback cb) { tempAlertCallback_ = cb; }

    // Classify temperature into status using Basler GigE thresholds
    static TemperatureStatus classifyTemperature(double temp) {
        return TempStatus::classify(temp);
    }
    // Event Handler for Device Removal
    class DeviceRemovalHandler : public Pylon::CConfigurationEventHandler {
    public:
        DeviceRemovalHandler(CameraManager* manager) : manager_(manager) {}
        void OnCameraDeviceRemoved(Pylon::CInstantCamera& camera) override;
    private:
        CameraManager* manager_;
    };

    CameraManager(int numCameras = 8);
    ~CameraManager();

    // Initialize cameras (Pylon Emulated or Real).
    // suppressBlankFor: config-array indices whose tiles must NOT be blanked during
    // this call — used during hot-rebuild so surviving cameras don't flash blank.
    bool initialize(const std::set<int>& suppressBlankFor = {});

    // Start acquisition for all cameras
    void startAcquisition();

    // Stop acquisition
    void stopAcquisition();

    // Pause/Resume grab
    void pauseGrabbing(bool pause);
    bool isGrabbingPaused() const;

    // Register a callback to receive frames
    void registerCallback(FrameCallback callback);
    
    // Register a callback to receive connection status messages
    using StatusCallback = std::function<void(const std::string&)>;
    void registerStatusCallback(StatusCallback callback) { statusCallback_ = callback; }
    
    // Get camera labels
    std::vector<std::string> getCameraLabels() const;
    
    // Get Camera Model Name from Pylon
    std::string getModelName(int index);
    
    // Get Camera Device Temperature from Pylon
    double getTemperature(int index);
    
    // Get Configured Resolution
    cv::Size getResolution() const;
    
    // Get Camera IP Address
    std::string getIpAddress(int index);
    
    // Get Specific Camera Resolution
    cv::Size getCameraResolution(int index);
    
    // Get Specific Camera Resulting FPS
    double getCameraFps(int index);

    // Connection state for live diagnostic coloring
    bool isCameraConnected(int configArrayIndex) const;
    bool isCameraOpen(int configArrayIndex) const;
    bool isCameraRunning(int configArrayIndex) const;
    bool stopCamera(int configArrayIndex);
    bool startCamera(int configArrayIndex, const CameraInfo& config);
    bool applyCameraDeviceSettings(int configArrayIndex, const CameraInfo& config);
    
    // Defect Detection Control
    void setDefectDetectionEnabled(bool enabled);
    bool isDefectDetectionEnabled() const;

    // Snapshot Control
    void triggerSnapshot(int cameraIndex);

    // Global Configuration
    void setGlobalFrameRate(double fps);
    void setCameraFrameRate(int cameraIndex, double fps, bool enableFrameRate = true);
    void setGlobalResolution(int binningFactor); // 1 = Full, 2 = 2x2, etc.
    
    // Live Camera Parameter Adjustment (Pylon nodes, no restart needed)
    void setCameraGain(int cameraIndex, double gain);
    void setCameraExposure(int cameraIndex, double exposureUs);
    void setCameraGamma(int cameraIndex, double gamma);
    void setCameraContrast(int cameraIndex, double contrast);

    // Pylon Feature Persistence (Save/Load .pfs per camera)
    struct CameraParams {
        double gain        = 0.0;
        double exposureUs  = 5000.0;
        double gamma       = 1.0;
        double contrast    = 1.0;
        double fps         = 0.0;
        double wdrHigh     = std::numeric_limits<double>::quiet_NaN(); // BslDualGainHigh (NaN = not available)
        double wdrLow      = std::numeric_limits<double>::quiet_NaN(); // BslDualGainLow  (NaN = not available)
        int    outputQueueDepth = 0;   // Pylon OutputQueueSize (live queued frames)
        int    width       = 0;        // sensor width in pixels (for MB calc)
        int    height      = 0;        // sensor height in pixels
        int    bpp         = 1;        // bytes per pixel
    };
    CameraParams getCameraParams(int configArrayIndex);
    bool saveParameters(int configArrayIndex);
    bool loadParameters(int configArrayIndex);
    void saveParametersForAll(const std::vector<CameraInfo>& cameras);

    // GigE Network Configuration
    static std::vector<GigEDeviceInfo> enumerateGigEDevices();
    static bool applyIpConfiguration(const std::string& mac, const std::string& ip, const std::string& mask, const std::string& gateway);

private:
    struct CameraRuntime {
        std::unique_ptr<Pylon::CInstantCamera> camera;
        Pylon::CDeviceInfo targetDevice;
        std::thread grabThread;
        int configId = -1;
        int source = 2;
        bool connected = false;
    };

    // Helper to configure camera parameters (resolution, PTP, transport tuning)
    void configureCamera(GenApi::INodeMap& nodemap, const CameraInfo& config, bool isEmulation);
    
    // Vision Pipeline (Blur -> Threshold -> Canny)
    void processFrame(const cv::Mat& input, cv::Mat& output, int cameraIndex);

    // Per-camera acquisition and recovery helpers
    void acquisitionLoop(int configArrayIndex);
    bool attachConfiguredCamera(int configArrayIndex, const CameraInfo& camInfo,
                                const Pylon::DeviceInfoList_t& devices,
                                std::set<int>& claimedDeviceIndices,
                                bool suppressBlank);
    bool tryReconnectCamera(int configArrayIndex);
    void stopCameraRuntime(int configArrayIndex);
    void clearCameraTile(int configArrayIndex);
    Pylon::CInstantCamera* getCameraByConfigIndex(int configArrayIndex);
    const Pylon::CInstantCamera* getCameraByConfigIndex(int configArrayIndex) const;
    // Snapshot Control


    int numCameras_;
    std::atomic<bool> acquiring_; // Threading
    std::atomic<bool> paused_{false}; // Paused Grab
    
    // Device Removal Recovery
    std::atomic<bool> recovering_;
    std::thread recoveryThread_;
    void recoveryLoop();
    void startRecoveryThreadIfNeeded();
    void joinRecoveryThread();

    // Temperature monitor
    std::atomic<bool> tempMonitorRunning_{false};
    std::thread tempMonitorThread_;
    void temperatureMonitorLoop();
    TempAlertCallback tempAlertCallback_;
    // Tracks previous status per camera to avoid redundant alerts
    std::vector<TemperatureStatus> prevTempStatus_;

    FrameCallback callback_;
    std::mutex callbackMutex_;
    StatusCallback statusCallback_;

    int width_;
    int height_;
    
    // For Multi-Camera Tiled Recording
    std::vector<cv::Mat> latestFrames_;
    std::mutex latestFramesMutex_;
    cv::Mat tiledBuffer_; // Optimization: Reusable buffer for tiling
    
    // Defect detection flag (default: disabled)
    std::atomic<bool> defectDetectionEnabled_;

    // Snapshot Requests (vector of atomics is tricky, using vector of bools protected by mutex for simplicity or fixed array of atomics)
    // Since we have fixed MAX_CAMERAS or dynamic, a mutex protected vector is safer for dynamic resizing.
    std::mutex snapshotMutex_;
    std::vector<bool> snapshotRequests_;
    
    // Mutex to protect camera parameter operations (save/load) from concurrent access
    std::mutex cameraParamsMutex_;
    int fps_;

    std::vector<CameraRuntime> cameraRuntimes_;
    
    std::vector<std::string> cameraLabels_;
    std::vector<std::string> modelNames_;
    
    // Maps Pylon array index -> CameraConfig ID (1-based)
    // e.g. cameras_[0] corresponds to Config ID stored in cameraIndexToConfigId_[0]
    std::vector<int> cameraIndexToConfigId_;
    
    // Maps config array index (0-based, order from getCameras()) -> Pylon array index
    // Used to get the correct camera_ entry for a given UI slot
    std::vector<int> configArrayIndexToPylonIndex_;
    
    // Maps Pylon array index -> config array index (0-based UI slot)
    // Used in the acquisition callback to emit the correct slot index
    std::vector<int> pylonIndexToConfigArrayIndex_;
    
    // Per-camera disconnect tracking: set of config array indices that have been removed.
    // Written from DeviceRemovalHandler / acquisitionLoop, read in acquisitionLoop.
    // Protected by disconnectedMutex_.
    std::set<uint32_t> disconnectedCameras_;
    std::mutex disconnectedMutex_;
    std::mutex recoveryThreadMutex_;
    std::atomic<bool> shuttingDown_{false};

    // Preallocated buffer pools (one per camera)
    std::vector<std::unique_ptr<BufferPool>> bufferPools_;
    
    // Software-applied display parameters (applied in processFrame for visual feedback)
    // Indexed by config array index (same as UI slot)
    std::vector<double> swGain_;    // Multiplier: 1.0 = no change
    std::vector<double> swGamma_;   // Gamma exponent: 1.0 = no change
    std::vector<double> swContrast_; // Contrast multiplier: 1.0 = no change
    
    // Cached LUT per camera — invalidated when swGain/swGamma/swContrast change
    std::vector<cv::Mat> lutCache_;
    std::vector<bool> lutValid_;
    
    // Mutex protecting software parameter data (swGain/swGamma/swContrast/lutValid/lutCache)
    // Guards against race between UI thread (writer) and acquisition thread (reader)
    std::mutex paramMutex_;

    // Per-camera software frame counters (fallback when chunk data is unavailable).
    // Each slot is written exclusively by its own acquisition thread, so no
    // additional mutex is needed. Protected by snapshotMutex_ during resize in
    // initialize() which always runs while acquisition threads are stopped.
    std::vector<int64_t> softwareFrameCounters_;
};
