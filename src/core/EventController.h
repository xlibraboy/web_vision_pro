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
#include "EventDatabase.h"

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
        // All machine-speed anchors snapshotted when the trigger fired
        // (position mm + actual local speed), persisted onto the event so the
        // defect projector can reproduce the machine's speed profile.
        std::vector<EventDatabase::SpeedAnchorSnapshot> speedAnchors;
    };


    // Singleton access
    static EventController& instance();

    // Initialize buffer size (fps * seconds)
    void initialize(int bufferSize = 550, double fps = 55.0, int postTriggerFrames = 110);

    // Add frame to a specific camera's circular buffer with metadata
    void addFrame(int cameraId, const cv::Mat& frame, int64_t timestamp, int64_t frameCounter);

    // Trigger an event (Paper Break) - captures post-trigger for ALL active cameras
    // Returns true when the trigger was accepted (recording armed). Returns false
    // when it was ignored (e.g. no streaming camera or empty group) so callers can
    // surface the reason to the user.
    bool triggerEvent();
    bool triggerEvent(const TriggerContext& context);

    // Check if currently saving
    bool isSaving() const;

    // Live RAM buffer diagnostics (cameraId is 1-based config ID)
    size_t getBufferedFrameCount(int cameraId);
    size_t getBufferCapacity(int cameraId);

    // Callback for when event is saved
    // (timestamp, triggerIndex, totalFrames, primaryCameraId)
    using EventSavedCallback = std::function<void(const std::string&, int, int, int)>;
    void setEventSavedCallback(EventSavedCallback callback);

    // Provides the live machine speed (m/min) used to spatially align a trigger
    // across cameras: each camera's saved window is centered on when the defect
    // passes it (offset = (P_cam - P_trigger) / V). Returns false when no valid
    // speed is available (alignment is then disabled).
    using SpeedProvider = std::function<bool(double* mPerMin)>;
    void setSpeedProvider(SpeedProvider provider);

    // Provides the full machine-speed profile (position mm + actual local
    // speed per anchor) captured at trigger time and persisted onto the event.
    // Returns false when no anchors are usable.
    using SpeedAnchorsProvider = std::function<bool(std::vector<EventDatabase::SpeedAnchorSnapshot>* anchors)>;
    void setSpeedAnchorsProvider(SpeedAnchorsProvider provider);

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
        // Wall-clock (steady) arrival time of the most recent frame, in ms.
        // Used to detect cameras that stopped streaming so a trigger with at
        // least one live camera still completes without waiting on dead ones.
        int64_t lastFrameArrivalMs = 0;
    };

    // Buffer state per camera (using 1-based indexing passed from CameraManager's config ID resolving)
    // A camera is considered live if a frame arrived within this window. Used to
    // let a trigger complete with only the cameras that are actually streaming
    // (a disconnected camera must not block allDone forever).
    static constexpr int64_t kCameraLiveWindowMs = 4000;
    // How often the save worker re-evaluates an armed event while no camera is
    // delivering frames. Self-heals the case where every live camera dies
    // mid-event (stale ring buffers are dropped, the event completes with
    // nothing to save, and triggering_ clears so future triggers work again).
    static constexpr int64_t kEventWatchdogIntervalMs = 1000;

    static int64_t nowMs();
    static bool isCameraLive(const CameraBufferState& state, int64_t now);

    // Evaluates whether the armed event can complete (all *live* participants
    // reached their target) and, if so, moves their ring buffers into the save
    // queue and signals the save worker. Returns true when it completed.
    // The _Locked variant requires bufferMutex_ to be held; the public wrapper
    // takes it itself (used by the saveWorker watchdog).
    bool tryCompleteEventLocked(int64_t now);
    bool tryCompleteEvent(int64_t now);

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
    SpeedAnchorsProvider speedAnchorsProvider_;
};
