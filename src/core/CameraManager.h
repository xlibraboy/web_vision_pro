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
    std::string ipConfigMode;        // "Static", "DHCP", or "AutoIP"
    bool supportsPersistentIp = false;
    bool supportsDhcp = false;
    bool supportsAutoIp = false;
};

// Result of an IP configuration write; lets callers distinguish a camera that
// is absent from discovery from a write that was attempted and failed.
enum class IpConfigResult {
    Success,
    DeviceNotFound,
    WriteFailed,
};

class CameraManager {
public:
    // Serializes ALL pylon CTlFactory/GenApi usage (enumeration, attach).
    // Pylon 6.2's factory is not safe under concurrent enumeration from the
    // GUI (ConfigDialog refresh) and the lifecycle worker — concurrent use
    // corrupts GenApi state and crashes with SIGBUS (#SS).
    static std::recursive_mutex& pylonApiMutex();

    // The fallback fps the RUNNING system was started with (frozen at app
    // start; see effectiveFrameRate). ConfigDialog uses it to tell the user
    // when a newly saved fallback needs an app restart to take effect.
    static double appStartFallbackFps();

    // Server Offline->Online is a "system restart" for the timing settings:
    // clear the frozen fallback so the next rate write re-reads the saved
    // System Configuration value (see MainWindow's serverToggled handler).
    static void resetAppStartFallbackFps();

    // Self-heal: if the camera is delivering MORE than its configured rate it
    // is free-running (rate control got lost, e.g. after a user set reload);
    // force the rate registers back on. Called periodically from MainWindow.
    void ensureConfiguredFrameRate(int configArrayIndex);

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

    // True while the acquisition runtime is streaming at least one camera.
    bool isAcquiring() const { return acquiring_; }

    // Ask the background recovery thread to reconnect the given camera slot
    // (config array index) as soon as it reappears on the network. Used after
    // an IP-config change, when the camera restarts its network stack and
    // briefly disappears from discovery.
    void requestCameraReconnect(int configArrayIndex);

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
    
    // Get specific camera acquisition frame rate in Hz.
    double getCameraAcquisitionFps(int index);

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

    // Software detection ROI for one camera (normalized 0..1 vertices relative
    // to the delivered frame). An empty polygon means NO region defined: the
    // camera's live defect scan is paused until a region is drawn or the whole
    // frame is chosen. Config-array-indexed; safe to call from the UI thread.
    void setCameraDetectionRoi(int configArrayIndex, const QVector<QPointF>& roi);
    bool hasCameraDetectionRoi(int configArrayIndex);

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

    // Raw AOI (region of interest) node write on an open camera. Per Basler
    // docs the camera must be idle for the change to take effect reliably, so
    // prefer applyCameraAOI() from UI paths. Returns true on success.
    bool setCameraAOI(int cameraIndex, int width, int height, int offsetX, int offsetY);

    // Apply a new AOI the way Basler documents it: stop acquisition, write
    // Width/Height/OffsetX/OffsetY, then restart the camera if it was running.
    // Config is updated so the AOI also survives the next start. Returns true
    // when the nodes were written successfully.
    bool applyCameraAOI(int cameraIndex, int width, int height, int offsetX, int offsetY);

    // Maximum AOI geometry the camera sensor supports (WidthMax/HeightMax,
    // falling back to SensorWidth/SensorHeight, then current Width/Height).
    struct AOILimits {
        int maxWidth = 0;
        int maxHeight = 0;
    };
    AOILimits getCameraAOILimits(int configArrayIndex);

    // Pylon Feature Persistence (Save/Load .pfs per camera)
    struct CameraParams {
        double gain        = 0.0;
        double gainMin     = 0.0;
        double gainMax     = 24.0;
        bool gainIsRaw     = false;
        QString gainDisplayName = "Gain";
        double exposureUs  = 5000.0;
        double exposureMinUs = 100.0;
        double exposureMaxUs = 1000000.0;
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

    // PTP (IEEE 1588) clock state read from a camera. available=false when the
    // camera exposes no IEEE 1588 nodes (e.g. emulation); enabled=false when
    // GevIEEE1588/PtpEnable reads false. state holds the latched port-state
    // symbolic (Slave, Master, Initializing, Listening, ...).
    struct PtpStatus {
        bool available = false;          // camera exposes IEEE 1588 nodes
        bool enabled = false;            // PTP clock synchronization switched on
        bool locked = false;             // servo locked (PtpServoStatus=Locked)
        QString state;                   // GevIEEE1588Status symbolic (e.g. "Slave")
        int64_t offsetFromMasterNs = -1; // |offset| from the master (ns), -1 = n/a
        QString clockId;                 // this camera's PTP clock id (hex)
        QString parentClockId;           // grandmaster clock id (hex), empty when master
    };
    using PtpStatusCallback = std::function<void(int camId, const PtpStatus& status)>;
    void registerPtpStatusCallback(PtpStatusCallback cb) { ptpStatusCallback_ = cb; }

    // Live device settings read from the camera itself (Basler scout nodes).
    // Prefers the attached runtime camera; falls back to a direct GigE open
    // by the configured MAC/IP when the camera is on the network but not
    // attached to the acquisition runtime.
    struct LiveDeviceSettings {
        bool ok = false;              // read succeeded (camera reachable)
        bool fromRuntime = false;     // read from attached runtime vs direct open
        QString pixelFormat;
        int width = 0;
        int height = 0;
        int offsetX = 0;
        int offsetY = 0;
        double exposureUs = 0.0;
        double exposureTimeBaseAbs = 0.0;
        int exposureTimeRaw = 0;
        bool acquisitionFrameRateEnable = false;
        double acquisitionFrameRate = 0.0;  // AcquisitionFrameRateAbs (requested)
        double resultingFrameRate = 0.0;    // ResultingFrameRateAbs (actual)
        bool chunkModeActive = false;
        QStringList enabledChunks;
        double temperature = 0.0;
        QString vendorName;
        QString modelName;
        QString manufacturerInfo;
        QString deviceVersion;
        QString firmwareVersion;
        QString deviceId;
        QString ipAddress;
        PtpStatus ptp;                 // PTP clock state (latched read)
    };
    LiveDeviceSettings readLiveDeviceSettings(int configArrayIndex, bool allowDirectOpen = true);

    // Runtime-attached PTP clock state for a camera (IEEE 1588). Never opens
    // the device directly: used by the low-frequency background sampler so a
    // polling cycle cannot block on a network open.
    PtpStatus readPtpStatus(int configArrayIndex);

    // Live exposure/rate writes (no acquisition stop required). Uses the
    // attached runtime camera, or opens the configured device directly when
    // it is online but not attached.
    void applyLiveExposureRate(int configArrayIndex, const CameraInfo& info);

    // GigE Network / Stream Health
    uint64_t getIncompleteGrabCount(int configArrayIndex) const;
    uint64_t getConsecutiveIncompleteGrabCount(int configArrayIndex) const;

    // Negotiated NIC link speed (Mbps) for the interface that carries the
    // camera's subnet, or -1 when the link/camera is unavailable. Reads the
    // sysfs 'speed' node (e.g. 1000, 100, 10).
    static int getLinkSpeedMbpsForIp(const QString& ipAddress);
    int getCameraLinkSpeedMbps(int configArrayIndex) const;
    static std::vector<GigEDeviceInfo> enumerateGigEDevices(bool forceRefresh = false);
    static IpConfigResult configureIpConfiguration(const std::string& mac, const std::string& mode, const std::string& ip, const std::string& mask, const std::string& gateway);

private:
    struct CameraRuntime {
        std::unique_ptr<Pylon::CInstantCamera> camera;
        Pylon::CDeviceInfo targetDevice;
        std::thread grabThread;
        int configId = -1;
        int source = 2;
        bool connected = false;
        // Drop counters. Written by the acquisition (grab) thread, reset on
        // (re)connect by the attach/recovery thread, read by the UI. Existing
        // code already accesses these unsynchronized from the UI thread, so a
        // benign race on a 3s-polled diagnostic is accepted.
        uint64_t incompleteGrabCount = 0;
        uint64_t consecutiveIncompleteGrabCount = 0;
    };

    // Helper to configure camera parameters (resolution, PTP, transport tuning)
    void configureCamera(GenApi::INodeMap& nodemap, const CameraInfo& config, bool isEmulation, bool preserveStartupUserSet = false);
    
    // Vision Pipeline (Blur -> Threshold -> Canny)
    void processFrame(const cv::Mat& input, cv::Mat& output, int cameraIndex);

    // Per-camera acquisition and recovery helpers
    void acquisitionLoop(int configArrayIndex);
    bool attachConfiguredCamera(int configArrayIndex, const CameraInfo& camInfo,
                                const Pylon::DeviceInfoList_t& devices,
                                std::set<int>& claimedDeviceIndices,
                                bool suppressBlank, bool warnOnMiss = true);
    bool tryReconnectCamera(int configArrayIndex);
    void stopCameraRuntime(int configArrayIndex);
    void clearCameraTile(int configArrayIndex);
    Pylon::CInstantCamera* getCameraByConfigIndex(int configArrayIndex);
    const Pylon::CInstantCamera* getCameraByConfigIndex(int configArrayIndex) const;
    // Snapshot Control


    int numCameras_;
    std::atomic<bool> acquiring_; // Threading
    std::atomic<bool> paused_{false}; // Paused Grab

    // True when this manager owns the PYLON_CAMEMU env var (set/unset by us)
    // rather than it being provided externally (docker/CI). Lets initialize()
    // re-evaluate emulation on lifecycle restarts without clobbering an
    // externally-supplied value.
    bool emulationEnvManaged_ = false;

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
    PtpStatusCallback ptpStatusCallback_;

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

    // Per-camera software detection ROI polygon, normalized to the delivered
    // frame (0..1 vertices). Indexed by config array index (same as UI slot).
    // An empty polygon = no region defined -> that camera's live defect scan
    // is PAUSED (no triggers/contours) until a region is drawn or the whole
    // frame is chosen. Written by the UI thread, read by the grab threads via
    // setCameraDetectionRoi()/snapshot in processFrame (paramMutex_ guarded).
    std::vector<std::vector<cv::Point2f>> detectionRoi_;
    
    // Mutex protecting software parameter data (swGain/swGamma/swContrast/lutValid/lutCache)
    // Guards against race between UI thread (writer) and acquisition thread (reader)
    std::mutex paramMutex_;

    // Per-camera software frame counters (fallback when chunk data is unavailable).
    // Each slot is written exclusively by its own acquisition thread, so no
    // additional mutex is needed. Protected by snapshotMutex_ during resize in
    // initialize() which always runs while acquisition threads are stopped.
    std::vector<int64_t> softwareFrameCounters_;
};
