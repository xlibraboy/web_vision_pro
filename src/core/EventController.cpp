#include "EventController.h"
#include "EventDatabase.h"
#include "RawFormat.h"
#include "SpeedProfile.h"
#include "../config/CameraConfig.h"
#include "../processing/HistogramAnalyzer.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <limits>
#include <cmath>
#include <chrono>
#include <utility>
#include <QDateTime>
#include <QDir>
#include <opencv2/imgcodecs.hpp>

EventController& EventController::instance() {
    static EventController instance;
    return instance;
}

EventController::~EventController() {
    running_ = false;
    saveCv_.notify_all();
    if (saveThread_.joinable()) {
        saveThread_.join();
    }
}

void EventController::initialize(int bufferSize, double fps, int postTriggerFrames) {
    // 1. Stop existing thread if running
    if (running_) {
        running_ = false;
        saveCv_.notify_all();
        if (saveThread_.joinable()) {
            saveThread_.join();
        }
    }

    bufferSize_ = bufferSize;
    postTriggerLimit_ = postTriggerFrames;
    fps_ = fps;
    triggering_ = false;
    running_ = true;
    saveRequested_ = false;

    {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        cameraStates_.clear();
        currentEventCameraLabels_.clear();
        currentEventCameraPositions_.clear();
        currentTriggerContext_ = TriggerContext{};
        groupRestricted_ = false;
        recordCameraIds_.clear();
    }
    
    // Start worker thread
    saveThread_ = std::thread(&EventController::saveWorker, this);
    
    std::cout << "[EventController] Initialized with pre-trigger buffer: " << bufferSize_ 
              << " (" << (bufferSize_ / fps_) << "s), post-trigger: " << postTriggerLimit_ 
              << " (" << (postTriggerLimit_ / fps_) << "s), format: RAW BINARY (.bin)"
              << std::endl;
}

int64_t EventController::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool EventController::isCameraLive(const CameraBufferState& state, int64_t now) {
    // A camera that has never delivered a frame is not live. Otherwise it stays
    // live as long as frames keep arriving (e.g. 10 fps → a frame every 100 ms,
    // well inside the 4 s window even with hiccups).
    return state.lastFrameArrivalMs != 0
        && (now - state.lastFrameArrivalMs) <= kCameraLiveWindowMs;
}

void EventController::addFrame(int cameraId, const cv::Mat& frame, int64_t timestamp, int64_t frameCounter) {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    // Initialize state if camera not seen before
    if (cameraStates_.find(cameraId) == cameraStates_.end()) {
        // Per-camera capacity: pre/post frame counts scaled by this camera's
        // real fps (equals the global frame counts when fps matches config).
        const size_t totalCapacity =
            static_cast<size_t>(preFramesFor(cameraId) + postFramesFor(cameraId));
        cameraStates_[cameraId].circularBuffer.resize(totalCapacity);
    }
    
    CameraBufferState& state = cameraStates_[cameraId];
    state.lastFrameArrivalMs = nowMs();

    // During an active event, stop extending this camera's saved window once it has
    // collected its per-camera post-trigger target (postTriggerLimit_ + the spatial
    // alignment offset). Otherwise faster cameras keep overwriting older pre-trigger
    // frames while waiting for slower cameras. Non-participating cameras (target -1)
    // keep rolling their ring buffer normally.
    if (triggering_ && state.captureTargetFrames >= 0
            && state.postFramesRecorded >= state.captureTargetFrames) {
        return;
    }

    // Ring Buffer Logic
    // 1. Copy frame into current write slot
    FrameData& target = state.circularBuffer[state.writeIndex];
    
    // Reallocation check
    if (target.image.empty() || 
        target.image.size() != frame.size() || 
        target.image.type() != frame.type()) {
        frame.copyTo(target.image); 
    } else {
        frame.copyTo(target.image); // Fast copy
    }

    // 2. Store Metadata
    target.timestamp = timestamp;
    target.frameCounter = frameCounter;
    
    // 3. Advance index
    state.writeIndex = (state.writeIndex + 1) % state.circularBuffer.size();
    
    // 4. Track fill size
    if (state.currentFillSize < state.circularBuffer.size()) {
        state.currentFillSize++;
    }

    // If we are currently collecting post-trigger frames
    if (triggering_) {
        // Only cameras belonging to the triggered group participate in the
        // event. Non-group cameras keep rolling their ring buffer normally.
        if (groupRestricted_ && recordCameraIds_.count(cameraId) == 0) {
            return;
        }

        int recorded = ++state.postFramesRecorded;
        if (recorded >= state.captureTargetFrames) {
            // Reached limit for this camera. Try to complete the event: this only
            // succeeds when every *live* participating camera has also reached its
            // target. Cameras that stopped streaming are skipped so the trigger
            // completes with whatever live cameras remain.
            tryCompleteEventLocked(nowMs());
        }
    }
}

bool EventController::tryCompleteEventLocked(int64_t now) {
    // Requires bufferMutex_ held. Evaluates completion and, when ready, moves
    // the participating live cameras' ring buffers into their save queues.
    if (saveRequested_ || !triggering_) {
        return false;
    }

    bool allDone = true;
    for (const auto& pair : cameraStates_) {
        if (groupRestricted_ && recordCameraIds_.count(pair.first) == 0) {
            continue;
        }
        if (pair.second.captureTargetFrames < 0) {
            continue;
        }
        // A camera that stopped streaming (disconnected, offline, or not
        // started) can never reach its target. Skip it so the trigger
        // completes with whatever live cameras remain.
        if (!isCameraLive(pair.second, now)) {
            continue;
        }
        if (pair.second.postFramesRecorded < pair.second.captureTargetFrames) {
            allDone = false;
            break;
        }
    }
    if (!allDone) {
        return false;
    }

    std::cout << "[EventController] Post-trigger capture complete for the triggered camera group. Moving to save queue." << std::endl;

    {
        std::lock_guard<std::mutex> saveLock(saveMutex_);

        for (auto& pair : cameraStates_) {
            if (groupRestricted_ && recordCameraIds_.count(pair.first) == 0) {
                continue;
            }
            CameraBufferState& s = pair.second;
            // Only save cameras that actually participated and are (or were just)
            // streaming. A camera that stopped before the trigger fires has a
            // stale buffer and must not be saved.
            if (!isCameraLive(s, now)) {
                continue;
            }
            s.saveQueue.clear();

            size_t tail = (s.currentFillSize < s.circularBuffer.size()) ? 0 : s.writeIndex;

            for (size_t i = 0; i < s.currentFillSize; ++i) {
                size_t idx = (tail + i) % s.circularBuffer.size();
                // Deep copy for saving
                FrameData fd;
                fd.image = s.circularBuffer[idx].image.clone();
                fd.timestamp = s.circularBuffer[idx].timestamp;
                fd.frameCounter = s.circularBuffer[idx].frameCounter;
                s.saveQueue.push_back(fd);
            }

            // Calculate linearized trigger index for the saved sequence.
            // The ring rolls while a downstream camera waits for the defect to
            // arrive, so the defect lands at a stable position (pre-trigger depth)
            // in every camera's saved window.
            s.linearizedTriggerIndex = static_cast<int>(s.currentFillSize)
                - s.postFramesRecorded - 1 + s.captureOffsetFrames;
            // Guard against degenerate extreme-upstream offsets that could push
            // the index out of the saved window.
            const int maxIndex = std::max(0, static_cast<int>(s.currentFillSize) - 1);
            s.linearizedTriggerIndex = std::max(0, std::min(s.linearizedTriggerIndex, maxIndex));
        }
        saveRequested_ = true;
    }

    triggering_ = false;
    saveCv_.notify_one();
    return true;
}

bool EventController::tryCompleteEvent(int64_t now) {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    return tryCompleteEventLocked(now);
}

bool EventController::triggerEvent() {
    return triggerEvent(TriggerContext{});
}

bool EventController::triggerEvent(const TriggerContext& context) {
    if (triggering_) return false;

    const QString reason = context.reason.isEmpty() ? QStringLiteral("Triggered") : context.reason;
    std::cout << "[EventController] EVENT TRIGGERED! Reason: " << reason.toStdString() << std::endl;

    std::lock_guard<std::mutex> lock(bufferMutex_);
    currentTimestamp_ = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz").toStdString();
    currentTriggerContext_ = context;
    currentTriggerContext_.reason = reason;
    if (currentTriggerContext_.positionDirectionSign >= 0) {
        currentTriggerContext_.positionDirectionSign = 1;
    } else {
        currentTriggerContext_.positionDirectionSign = -1;
    }

    currentEventCameraLabels_.clear();
    currentEventCameraPositions_.clear();
    const std::vector<CameraInfo> cameras = CameraConfig::getCameras();

    // Determine which cameras participate: a group-restricted trigger only
    // records cameras whose config group matches. A trigger with no group
    // restriction (group < 0) records all active cameras (legacy behavior).
    groupRestricted_ = context.group >= 0;
    recordCameraIds_.clear();
    if (groupRestricted_) {
        for (size_t i = 0; i < cameras.size(); ++i) {
            if (cameras[i].group == context.group) {
                recordCameraIds_.insert(static_cast<int>(i) + 1);
            }
        }
        if (recordCameraIds_.empty()) {
            std::cout << "[EventController] Trigger ignored: no camera is assigned to group "
                      << CameraGroup::name(context.group).toStdString() << std::endl;
            return false;
        }
    }

    // Reset every camera's capture state; targets are assigned below.
    for (auto& pair : cameraStates_) {
        pair.second.postFramesRecorded = 0;
        pair.second.captureTargetFrames = -1;
        pair.second.captureOffsetFrames = 0;
    }

    // Resolve the machine speed for spatial alignment: prefer the trigger's own
    // speed sample, else fall back to the live speed provider (e.g. the OPC UA
    // service) so defect triggers align too.
    double speedMperMin = 0.0;
    bool haveSpeed = false;
    if (context.hasSpeed && context.speedValue > 0.0) {
        speedMperMin = context.speedValue;
        haveSpeed = true;
    } else if (speedProvider_ && speedProvider_(&speedMperMin) && speedMperMin > 0.0) {
        haveSpeed = true;
    }

    // Snapshot the full machine-speed profile (position mm + actual local
    // speed per anchor) so the event records the speed at every drive, not just
    // one value. Persisted onto EventInfo.speedAnchors by the save worker.
    currentTriggerContext_.speedAnchors.clear();
    if (speedAnchorsProvider_) {
        std::vector<EventDatabase::SpeedAnchorSnapshot> anchors;
        if (speedAnchorsProvider_(&anchors)) {
            currentTriggerContext_.speedAnchors = std::move(anchors);
        }
    }

    const bool alignmentWanted = context.triggerPositionMm > 0;
    const bool alignmentEnabled = alignmentWanted && haveSpeed;
    if (alignmentEnabled) {
        const int sign = currentTriggerContext_.positionDirectionSign >= 0 ? 1 : -1;
        std::cout << "[EventController] Spatial alignment: primary speed=" << speedMperMin
                  << " m/min, anchors=" << currentTriggerContext_.speedAnchors.size()
                  << ", trigger position=" << context.triggerPositionMm
                  << " mm, sign=" << sign << std::endl;
        for (auto& pair : cameraStates_) {
            if (groupRestricted_ && recordCameraIds_.count(pair.first) == 0) {
                continue;
            }
            const int configIndex = pair.first - 1;
            if (configIndex < 0 || configIndex >= static_cast<int>(cameras.size())) {
                pair.second.captureTargetFrames = postFramesFor(pair.first);
                continue;
            }
            currentEventCameraLabels_[pair.first] = CameraConfig::getCameraLabel(configIndex);
            const int cameraPosition = cameras[static_cast<size_t>(configIndex)].machinePosition;
            currentEventCameraPositions_[pair.first] = cameraPosition;

            // framesPerMm uses the LOCAL speed at this camera's position
            // (interpolated between the recorded anchors), so the draw between
            // drive groups is reflected in the capture window.
            // framesPerMm = fps * (60 s/min) / (speed mm/min) — using THIS
            // camera's real fps so mixed-fps lines stay time-aligned.
            const double localSpeed = SpeedProfile::speedAt(
                cameraPosition, currentTriggerContext_.speedAnchors, speedMperMin);
            const double fpsCam = cameraFps(pair.first);
            const double framesPerMm = fpsCam * 60.0 / (localSpeed * 1000.0);
            const int deltaMm = (cameraPosition - context.triggerPositionMm) * sign;
            int offsetFrames = static_cast<int>(std::lround(deltaMm * framesPerMm));
            // An upstream camera cannot recover a defect that already left its
            // pre-trigger buffer; clamp to the oldest recoverable frame.
            if (offsetFrames < -preFramesFor(pair.first)) {
                offsetFrames = -preFramesFor(pair.first);
            }
            pair.second.captureOffsetFrames = offsetFrames;
            // Minimum 1 so every participating camera writes at least one frame
            // and the allDone evaluation runs (a target of 0 would early-return
            // before the ring write and could deadlock the whole event).
            pair.second.captureTargetFrames = std::max(1, postFramesFor(pair.first) + offsetFrames);
        }
    } else {
        if (alignmentWanted) {
            std::cout << "[EventController] Spatial alignment requested but no valid "
                         "speed sample - recording the wall-clock window instead." << std::endl;
        }
        for (auto& pair : cameraStates_) {
            if (groupRestricted_ && recordCameraIds_.count(pair.first) == 0) {
                continue;
            }
            const int configIndex = pair.first - 1;
            if (configIndex >= 0 && configIndex < static_cast<int>(cameras.size())) {
                currentEventCameraLabels_[pair.first] = CameraConfig::getCameraLabel(configIndex);
                currentEventCameraPositions_[pair.first] = cameras[static_cast<size_t>(configIndex)].machinePosition;
            }
            pair.second.captureTargetFrames = postFramesFor(pair.first);
        }
    }

    // If no participating camera is actually streaming frames right now (never
    // streamed, or stopped streaming recently), the event could never complete
    // and triggering_ would stay set forever, silently swallowing every later
    // trigger. Bail out cleanly instead.
    const int64_t now = nowMs();
    bool anyParticipantLive = false;
    for (const auto& pair : cameraStates_) {
        if (groupRestricted_ && recordCameraIds_.count(pair.first) == 0) {
            continue;
        }
        if (pair.second.captureTargetFrames >= 0 && isCameraLive(pair.second, now)) {
            anyParticipantLive = true;
            break;
        }
    }
    if (!anyParticipantLive) {
        std::cout << "[EventController] Trigger ignored: no active camera is "
                     "streaming frames right now." << std::endl;
        return false;
    }

    triggering_ = true;
    return true;
}

void EventController::setSpeedProvider(SpeedProvider provider) {
    speedProvider_ = std::move(provider);
}

void EventController::setCameraFpsProvider(CameraFpsProvider provider) {
    cameraFpsProvider_ = std::move(provider);
}

double EventController::cameraFps(int cameraId) const {
    const double detected = cameraFpsProvider_ ? cameraFpsProvider_(cameraId) : 0.0;
    return (detected > 0.0) ? detected : fps_;
}

int EventController::preFramesFor(int cameraId) const {
    const double scale = (fps_ > 0.0) ? cameraFps(cameraId) / fps_ : 1.0;
    return std::max(1, static_cast<int>(std::lround(bufferSize_ * scale)));
}

int EventController::postFramesFor(int cameraId) const {
    const double scale = (fps_ > 0.0) ? cameraFps(cameraId) / fps_ : 1.0;
    return std::max(1, static_cast<int>(std::lround(postTriggerLimit_ * scale)));
}

void EventController::setSpeedAnchorsProvider(SpeedAnchorsProvider provider) {
    speedAnchorsProvider_ = std::move(provider);
}

bool EventController::isSaving() const {
    return triggering_ || saveRequested_;
}

size_t EventController::getBufferedFrameCount(int cameraId) {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    auto it = cameraStates_.find(cameraId);
    if (it == cameraStates_.end()) {
        return 0;
    }
    return it->second.currentFillSize;
}

size_t EventController::getBufferCapacity(int cameraId) {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    auto it = cameraStates_.find(cameraId);
    if (it == cameraStates_.end()) {
        return 0;
    }
    return it->second.circularBuffer.size();
}

// Called when a camera's acquisition fps changes at runtime (Device Settings).
// Resizes the camera's ring buffer to the new capacity, preserving the most
// recent frames when shrinking.
void EventController::updateCameraFps(int cameraId) {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    // Never yank buffers mid-capture: the resize resets postFramesRecorded and
    // would corrupt an event that is currently being collected.
    if (triggering_ || saveRequested_) {
        return;
    }
    auto it = cameraStates_.find(cameraId);
    if (it == cameraStates_.end()) {
        return;
    }
    const size_t newCapacity =
        static_cast<size_t>(preFramesFor(cameraId) + postFramesFor(cameraId));
    if (newCapacity == it->second.circularBuffer.size()) {
        return;
    }

    // Extract the newest min(oldSize, newSize) frames in arrival order.
    std::vector<FrameData> kept;
    const size_t oldSize = it->second.circularBuffer.size();
    const size_t keepCount = std::min(newCapacity, oldSize);
    kept.reserve(keepCount);
    for (size_t i = 0; i < keepCount; ++i) {
        const size_t idx = (it->second.writeIndex + oldSize - keepCount + i) % oldSize;
        kept.push_back(std::move(it->second.circularBuffer[idx]));
    }

    it->second.circularBuffer.assign(kept.begin(), kept.end());
    it->second.writeIndex = keepCount % newCapacity;
    it->second.currentFillSize = keepCount;
    it->second.postFramesRecorded = 0;
}

void EventController::setEventSavedCallback(EventSavedCallback callback) {
    callback_ = callback;
}

void EventController::saveWorker() {
    while (running_) {
        std::unique_lock<std::mutex> lock(saveMutex_);
        // Wait for a save request or, while an event is armed, a watchdog
        // tick. The watchdog re-evaluates completion so a trigger whose
        // cameras all stopped streaming mid-event still clears triggering_
        // instead of wedging it forever (blocking all later triggers).
        bool watchdogTick = false;
        while (!saveRequested_ && running_) {
            if (saveCv_.wait_for(lock,
                    std::chrono::milliseconds(kEventWatchdogIntervalMs))
                    == std::cv_status::timeout && triggering_) {
                watchdogTick = true;
                break;
            }
        }

        if (!running_) break;

        if (watchdogTick) {
            // Do not hold saveMutex_ while taking bufferMutex_ (addFrame takes
            // bufferMutex_ then saveMutex_, so the reverse order would deadlock).
            lock.unlock();
            tryCompleteEvent(nowMs());
            lock.lock();
            continue;
        }

        if (saveRequested_) {
            // Swap to local queues per camera
            std::map<int, std::deque<FrameData>> framesToSave;
            std::map<int, int> triggerIndices;
            std::map<int, QString> eventCameraLabels;
            std::map<int, int> eventCameraPositions;
            TriggerContext triggerContext;

            {
                std::lock_guard<std::mutex> bufferLock(bufferMutex_);
                for (auto& pair : cameraStates_) {
                    framesToSave[pair.first].swap(pair.second.saveQueue);
                    triggerIndices[pair.first] = pair.second.linearizedTriggerIndex;
                }
                eventCameraLabels = currentEventCameraLabels_;
                eventCameraPositions = currentEventCameraPositions_;
                triggerContext = currentTriggerContext_;
            }
            
            saveRequested_ = false;
            lock.unlock();
            
            QString baseName = QString::fromStdString(currentTimestamp_);
            
            const QString eventStoragePath = CameraConfig::getEventStoragePath();

            // Ensure directory exists
            QDir().mkpath(eventStoragePath);
            
            int primaryCameraId = 1; 
            int primaryFramesCount = 0;
            int primaryTriggerIndex = 0;
            int primaryWidth = 0;
            int primaryHeight = 0;
            bool primarySaved = false;
            QString primaryFilename;

            for (auto& pair : framesToSave) {
                int cameraId = pair.first;
                std::deque<FrameData>& frames = pair.second;
                int triggerIndex = triggerIndices[cameraId];

                if (frames.empty()) continue;
                
                int framesCount = static_cast<int>(frames.size());
                std::cout << "[EventController] Saving " << framesCount << " frames for Camera " << cameraId << "..." << std::endl;
                
                // Save as Raw Binary with camera suffix
                saveAsRaw(frames, baseName, triggerIndex, cameraId);

                if (cameraId == 1 || !primarySaved) {
                    primaryFramesCount = framesCount;
                    primaryTriggerIndex = triggerIndex;
                    primaryWidth = frames[0].image.cols;
                    primaryHeight = frames[0].image.rows;
                    primaryFilename = QDir(eventStoragePath).filePath(QString("event_%1_cam%2.bin").arg(baseName).arg(cameraId));
                    primarySaved = true;
                    primaryCameraId = cameraId;
                }
            }
            
            // Register event in database for the primary camera to prevent duplicates
            if (primarySaved) {
                EventDatabase::EventInfo event;
                event.timestamp = QString::fromStdString(currentTimestamp_);
                event.videoPath = primaryFilename;
                event.metadataPath = "";
                event.triggerIndex = primaryTriggerIndex;
                event.totalFrames = primaryFramesCount;
                event.fps = cameraFps(primaryCameraId);
                event.width = primaryWidth;
                event.height = primaryHeight;
                event.triggerReason = triggerContext.reason;
                event.triggerSource = triggerContext.source;
                event.triggerTagName = triggerContext.triggerTagName;
                event.triggerTagNodeId = triggerContext.triggerTagNodeId;
                event.speedTagName = triggerContext.speedTagName;
                event.speedTagNodeId = triggerContext.speedTagNodeId;
                event.speedValue = triggerContext.hasSpeed
                    ? triggerContext.speedValue
                    : std::numeric_limits<double>::quiet_NaN();
                event.speedUnit = triggerContext.hasSpeed ? triggerContext.speedUnit : QString();
                event.speedSampleTimeUtc = triggerContext.speedSampleTimeUtc;
                event.speedStale = triggerContext.speedStale;
                event.positionDirectionSign = triggerContext.positionDirectionSign;
                event.triggerPositionMm = triggerContext.triggerPositionMm;
                event.triggerGroup = triggerContext.group;
                event.speedAnchors = triggerContext.speedAnchors;
                int highestCameraId = 0;
                for (const auto& pair : framesToSave) {
                    if (!pair.second.empty()) {
                        highestCameraId = std::max(highestCameraId, pair.first);
                    }
                }
                if (highestCameraId > 0) {
                    event.cameraLabels.reserve(highestCameraId);
                    event.cameraPositionsMm.reserve(static_cast<size_t>(highestCameraId));
                    for (int cameraId = 1; cameraId <= highestCameraId; ++cameraId) {
                        event.cameraLabels.append(eventCameraLabels.count(cameraId)
                            ? eventCameraLabels[cameraId]
                            : QString());
                        event.cameraPositionsMm.push_back(eventCameraPositions.count(cameraId)
                            ? eventCameraPositions[cameraId]
                            : 0);
                    }
                }

                // Compute trigger-frame histogram per camera
                for (auto& pair : framesToSave) {
                    const int camId = pair.first;
                    const std::deque<FrameData>& frames = pair.second;
                    const int trigIdx = triggerIndices[camId];
                    if (frames.empty() || trigIdx < 0 || trigIdx >= static_cast<int>(frames.size())) {
                        continue;
                    }
                    HistogramAnalyzer::Histogram hist = HistogramAnalyzer::compute(frames[trigIdx].image);
                    if (!hist.bins.empty()) {
                        event.histograms[camId] = std::move(hist.bins);
                    }
                }

                EventDatabase::instance().registerEvent(event);

                // Notify UI with CORRECT linearized index from the primary camera
                if (callback_) {
                    callback_(currentTimestamp_, primaryTriggerIndex, primaryFramesCount, primaryCameraId);
                }
            }
            
            triggering_ = false;
        }
    }
}

void EventController::saveAsRaw(const std::deque<FrameData>& frames, const QString& baseName, int triggerIndex, int cameraId) {
    if (frames.empty()) return;

    QString filename = QDir(CameraConfig::getEventStoragePath()).filePath(
        QString("event_%1_cam%2.bin").arg(baseName).arg(cameraId));
    std::ofstream outFile(filename.toStdString(), std::ios::binary);
    
    if (!outFile) {
        std::cerr << "[EventController] Failed to open raw file for writing: " << filename.toStdString() << std::endl;
        return;
    }

    // 1. Write Global Header using RawFormat.h logic
    RawFileHeader header = {};
    std::memcpy(header.magic, RAW_FILE_MAGIC, 4);
    header.version = RAW_FILE_VERSION;
    header.width = frames[0].image.cols;
    header.height = frames[0].image.rows;
    // Map OpenCV type to generic pixelFormat: 0=Mono8, 1=BGR8, 2=RGB8
    if (frames[0].image.channels() == 1) header.pixelFormat = 0;
    else header.pixelFormat = 1; // Assume BGR8 for 3-channel default
    // Per-camera truth: each .bin records the fps its camera actually ran at,
    // so playback time axes and slow-motion are correct for mixed-fps lines.
    header.fps = cameraFps(cameraId);
    header.totalFrames = static_cast<uint32_t>(frames.size());
    header.triggerIndex = triggerIndex;

    outFile.write(reinterpret_cast<const char*>(&header), sizeof(RawFileHeader));
    
    // Calculate frame size
    size_t frameSize = frames[0].image.total() * frames[0].image.elemSize();

    std::cout << "[EventController] Writing Raw Binary to " << filename.toStdString() 
              << " (" << (frameSize * header.totalFrames / 1024 / 1024) << " MB)..." << std::endl;

    // 2. Write Frames
    for (size_t i = 0; i < frames.size(); ++i) {
        const auto& frameData = frames[i];
        // Pixel Data MUST be written FIRST according to VideoStreamReader
        if (frameData.image.isContinuous()) {
            outFile.write(reinterpret_cast<const char*>(frameData.image.data), frameSize);
        } else {
            cv::Mat cont = frameData.image.clone();
            outFile.write(reinterpret_cast<const char*>(cont.data), frameSize);
        }

        // Frame Metadata MUST be written SECOND (appended after image)
        FrameMetadata meta = {};
        meta.timestamp = static_cast<uint64_t>(std::max<int64_t>(0, frameData.timestamp));
        meta.frameId = static_cast<uint64_t>(std::max<int64_t>(0, frameData.frameCounter));
        meta.flags = (static_cast<int>(i) == triggerIndex) ? 1u : 0u;
        
        outFile.write(reinterpret_cast<const char*>(&meta), sizeof(FrameMetadata));
    }
    
    outFile.close();
    std::cout << "[EventController] Raw save complete for camera " << cameraId << std::endl;
}
