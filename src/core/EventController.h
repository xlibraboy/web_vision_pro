#pragma once

#include <opencv2/opencv.hpp>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <map>
#include <set>
#include <QString>
#include <cstdint>
#include "../gui/CameraInfo.h"

/**
 * EventController - Manages circular buffering and event recording.
 * Saves data in a custom Raw Binary Format (.bin) for speed and metadata persistence.
 */
class EventController {
public:
    struct FrameData {
        cv::Mat image;       // Pixel data
        int64_t timestamp;   // Hardware timestamp (ns)
        int64_t frameCounter;
    };
    struct TriggerContext {
        QString reason = QStringLiteral("Triggered");
        QString source = QStringLiteral("unknown");
        QString triggerTagName;
        QString triggerTagNodeId;
        QString speedTagName;
        QString speedTagNodeId;
        double speedValue = 0.0;
        QString speedUnit = QStringLiteral("m/min");
        QString speedSampleTimeUtc;
        bool hasSpeed = false;
        bool speedStale = false;
        int positionDirectionSign = 1;
        // Camera group to record (CameraGroup::k*). A negative value means ALL
        // active cameras are recorded (legacy behavior).
        int group = CameraGroup::kUnassigned;
        // Machine position (mm) where the trigger fired (sensor / detecting
        // camera). 0 = spatial alignment disabled.
        int triggerPositionMm = 0;
    };


    // Singleton access
    static EventController& instance();

    // Initialize buffer size (fps * seconds)
    void initialize(int bufferSize = 550, double fps = 55.0, int postTriggerFrames = 110);

    // Add frame to a specific camera's circular buffer with metadata
    void addFrame(int cameraId, const cv::Mat& frame, int64_t timestamp, int64_t frameCounter);

    // Trigger an event (Paper Break) - captures post-trigger for ALL active cameras
    void triggerEvent();
    void triggerEvent(const TriggerContext& context);

    // Check if currently saving
    bool isSaving() const;

    // Live RAM buffer diagnostics (cameraId is 1-based config ID)
    size_t getBufferedFrameCount(int cameraId);
    size_t getBufferCapacity(int cameraId);

    // Callback for when event is saved (timestamp, triggerIndex, totalFrames)
    using EventSavedCallback = std::function<void(const std::string&, int, int)>;
    void setEventSavedCallback(EventSavedCallback callback);

    // Provides the live machine speed (m/min) used to spatially align a trigger
    // across cameras: each camera's saved window is centered on when the defect
    // passes it (offset = (P_cam - P_trigger) / V). Returns false when no valid
    // speed is available (alignment is then disabled).
    using SpeedProvider = std::function<bool(double* mPerMin)>;
    void setSpeedProvider(SpeedProvider provider);

private:
    EventController() : running_(false), triggering_(false), saveRequested_(false) {}
    ~EventController();
    
    // Copy construction deleted
    EventController(const EventController&) = delete;
    EventController& operator=(const EventController&) = delete;

    // Worker thread for saving
    void saveWorker();

    // Save a queue of frames as Raw Binary File (.bin)
    void saveAsRaw(const std::deque<FrameData>& frames, const QString& baseName, int triggerIndex, int cameraId);

    // Configuration
    int bufferSize_;
    int postTriggerLimit_; // Frames to capture AFTER trigger
    double fps_;

    struct CameraBufferState {
        std::vector<FrameData> circularBuffer;
        size_t writeIndex = 0;
        size_t currentFillSize = 0;
        int postFramesRecorded = 0;
        std::deque<FrameData> saveQueue;
        int linearizedTriggerIndex = 0;
        // Spatial alignment: how many frames the defect passes this camera
        // AFTER the wall-clock trigger moment (negative = already passed).
        int captureOffsetFrames = 0;
        // Per-camera post-trigger frame target (postTriggerLimit_ + offset).
        // -1 = not participating in the current event.
        int captureTargetFrames = -1;
    };

    // Buffer state per camera (using 1-based indexing passed from CameraManager's config ID resolving)
    std::map<int, CameraBufferState> cameraStates_;
    std::mutex bufferMutex_;

    // True while the active trigger records only a specific camera group.
    bool groupRestricted_ = false;
    // When groupRestricted_, this holds the 1-based camera IDs whose config
    // group matched the trigger's group.
    std::set<int> recordCameraIds_;

    // Save state
    std::atomic<bool> triggering_;
    
    std::string currentTimestamp_;
    std::map<int, QString> currentEventCameraLabels_;
    std::map<int, int> currentEventCameraPositions_;
    TriggerContext currentTriggerContext_;

    // Threading
    std::thread saveThread_;
    std::atomic<bool> running_;
    std::condition_variable saveCv_;
    std::mutex saveMutex_;
    bool saveRequested_; 

    EventSavedCallback callback_;
    SpeedProvider speedProvider_;
};
