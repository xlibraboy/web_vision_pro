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
#include <QString>
#include <cstdint>

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

    // Singleton access
    static EventController& instance();

    // Initialize buffer size (fps * seconds)
    void initialize(int bufferSize = 550, double fps = 55.0, int postTriggerFrames = 110);

    // Add frame to circular buffer with metadata
    void addFrame(const cv::Mat& frame, int64_t timestamp, int64_t frameCounter);

    // Trigger an event (Paper Break)
    void triggerEvent();

    // Check if currently saving
    bool isSaving() const;

    // Callback for when event is saved (timestamp, triggerIndex, totalFrames)
    using EventSavedCallback = std::function<void(const std::string&, int, int)>;
    void setEventSavedCallback(EventSavedCallback callback);

private:
    EventController() : running_(false), triggering_(false), saveRequested_(false) {}
    ~EventController();
    
    // Copy construction deleted
    EventController(const EventController&) = delete;
    EventController& operator=(const EventController&) = delete;

    // Worker thread for saving
    void saveWorker();

    // Save as Raw Binary File (.bin)
    void saveAsRaw(const std::deque<FrameData>& frames, const QString& baseName, int triggerIndex);

    // Configuration
    int bufferSize_;
    int postTriggerLimit_; // Frames to capture AFTER trigger
    double fps_;

    // Buffer state
    std::vector<FrameData> circularBuffer_;
    size_t writeIndex_;
    size_t currentFillSize_;
    std::mutex bufferMutex_;
    
    // Save state
    std::atomic<bool> triggering_;
    std::atomic<int> postFramesRecorded_;
    int triggerIndex_; 
    int linearizedTriggerIndex_; // Correct index for saved sequence
    std::deque<FrameData> saveQueue_;
    std::string currentTimestamp_;

    // Threading
    std::thread saveThread_;
    std::atomic<bool> running_;
    std::condition_variable saveCv_;
    std::mutex saveMutex_;
    bool saveRequested_; 

    EventSavedCallback callback_;
};
