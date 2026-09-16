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
#include <cstdlib>
#include <chrono>
#include <utility>
#include <QDateTime>
#include <QDir>
#include <opencv2/imgcodecs.hpp>

namespace {
// Comma-joined display names of the sections a trigger records, for the
// "trigger ignored" message and log line.
QString sectionNames(const std::set<int>& sections) {
    QStringList names;
    for (int section : sections) {
        names.append(CameraGroup::name(section));
    }
    return names.join(", ");
}
}

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

// How many of a camera's newest frames back the clock and delivered-rate
// estimate (median gap over this many frames).
static constexpr size_t kClockSampleCount = 32;

int64_t EventController::cameraClockNowNs(const CameraBufferState& state, int64_t now,
                                          double* intervalNs) const {
    if (intervalNs) *intervalNs = 0.0;
    const size_t size = state.circularBuffer.size();
    if (size == 0 || state.currentFillSize == 0 || state.lastFrameArrivalMs == 0) {
        return 0;
    }

    // Collect the newest frames' own clock readings, newest first. The sequence
    // is only trusted while it strictly decreases as we walk back: a wrapped or
    // reset counter reads forwards and must not be used for timing.
    std::vector<int64_t> recent;
    recent.reserve(kClockSampleCount);
    int64_t newest = 0;
    int64_t previous = 0;
    for (size_t i = 0; i < state.currentFillSize && recent.size() < kClockSampleCount; ++i) {
        const size_t idx = (state.writeIndex + size - 1 - i) % size;
        const int64_t ts = state.circularBuffer[idx].timestamp;
        if (ts <= 0) {
            break;
        }
        if (newest == 0) {
            newest = ts;
        } else if (ts >= previous) {
            return 0;
        }
        previous = ts;
        recent.push_back(ts);
    }
    if (recent.size() < 2) {
        return 0;
    }

    // Delivered interval = median of the observed gaps, so a camera that misses
    // frames reports the rate it really runs at, not the configured one.
    std::vector<int64_t> gaps;
    gaps.reserve(recent.size() - 1);
    for (size_t i = 0; i + 1 < recent.size(); ++i) {
        gaps.push_back(recent[i] - recent[i + 1]);
    }
    std::sort(gaps.begin(), gaps.end());
    const int64_t medianGap = gaps[gaps.size() / 2];
    if (medianGap <= 0) {
        return 0;
    }
    if (intervalNs) *intervalNs = static_cast<double>(medianGap);

    // Extrapolate from the newest frame's own reading to "now" through the wall
    // time since that frame arrived: the trigger instant on this camera's clock.
    return newest + std::max<int64_t>(0, now - state.lastFrameArrivalMs) * 1000000LL;
}

void EventController::armTimeWindow(CameraBufferState& state, double travelSeconds,
                                    double postSeconds, int64_t now) {
    state.triggerClockNs = 0;
    state.captureStopClockNs = 0;
    state.captureWindowMs = 0;
    state.captureStartMs = now;

    double intervalNs = 0.0;
    const int64_t clockNow = cameraClockNowNs(state, now, &intervalNs);
    if (clockNow <= 0 || intervalNs <= 0.0) {
        return;
    }

    // The window is the sheet's travel time to this camera PLUS the post roll —
    // both durations, so every camera covers the same seconds of paper however
    // many frames it manages to deliver. Negative travel (defect already past)
    // can make that less than one frame; the frame-count path never went below
    // one frame, and neither does this.
    const double frameSeconds = intervalNs / 1e9;
    const double windowSeconds = std::max(frameSeconds, travelSeconds + postSeconds);

    state.triggerClockNs = clockNow;
    state.captureStopClockNs =
        clockNow + static_cast<int64_t>(std::llround(windowSeconds * 1e9));
    state.captureWindowMs = static_cast<int64_t>(std::llround(windowSeconds * 1000.0));
}

void EventController::addFrame(int cameraId, const cv::Mat& frame, int64_t timestamp, int64_t frameCounter) {
    // A camera mid-(re)configuration can emit empty grabs (observed: a
    // starting camera delivered empty Mats that filled the ring and were
    // saved as a totalFrames=N, width=0 file — corrupting the event and the
    // analysis timeline). Drop them at the single intake point.
    static std::atomic<int> emptyFrameDrops{0};
    if (frame.empty()) {
        if (emptyFrameDrops.fetch_add(1) < 10) {
            std::cerr << "[EventController] Dropped empty frame for camera " << cameraId
                      << " (camera starting/reconfiguring?)" << std::endl;
        }
        return;
    }

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

    // During an active event, stop extending this camera's saved window once it
    // has recorded its full window. Otherwise faster cameras keep overwriting
    // older pre-trigger frames while waiting for slower cameras. Non-participating
    // cameras (target -1) keep rolling their ring buffer normally.
    if (triggering_ && state.captureTargetFrames >= 0 && state.captureDone) {
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

        const int recorded = ++state.postFramesRecorded;
        const int64_t now = nowMs();
        bool done = false;
        if (state.captureStopClockNs > 0 && timestamp > 0) {
            // Time-based window: the camera stops when ITS OWN clock reaches the
            // instant the defect has passed plus the post window, so missed
            // frames only shorten the recording, never shift it against the
            // other cameras.
            done = timestamp >= state.captureStopClockNs;
            if (!done && state.captureWindowMs > 0
                    && now - state.captureStartMs > state.captureWindowMs * kCaptureRunawayFactor) {
                // Clock stopped advancing while frames keep coming: close on the
                // wall clock so this camera cannot hold the event open forever.
                done = true;
                std::cerr << "[EventController] Camera " << cameraId
                          << " exceeded its capture window on the wall clock "
                             "(frame clock stalled?) - closing its capture." << std::endl;
            }
        } else {
            // No usable frame clock (or emulation): fall back to the frame count,
            // which assumes the configured/detected rate is what arrives.
            done = recorded >= state.captureTargetFrames;
        }

        if (done) {
            state.captureDone = true;
            // Try to complete the event: this only succeeds when every *live*
            // participating camera has also finished. Cameras that stopped
            // streaming are skipped so the trigger completes with whatever live
            // cameras remain.
            tryCompleteEventLocked(now);
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
        if (!pair.second.captureDone) {
            allDone = false;
            break;
        }
    }
    if (!allDone) {
        return false;
    }

    std::cout << "[EventController] Post-trigger capture complete for the participating cameras. Moving to save queue." << std::endl;

    {
        std::lock_guard<std::mutex> saveLock(saveMutex_);

        currentEventMissingCameraIds_.clear();

        for (auto& pair : cameraStates_) {
            if (groupRestricted_ && recordCameraIds_.count(pair.first) == 0) {
                continue;
            }
            CameraBufferState& s = pair.second;
            // Only save cameras that actually participated and are (or were just)
            // streaming. A camera that stopped before the trigger fires has a
            // stale buffer and must not be saved — but it belongs to the event
            // and is recorded as missing so the gap is visible in the analysis
            // instead of the camera just silently not being there.
            if (!isCameraLive(s, now)) {
                currentEventMissingCameraIds_.push_back(pair.first);
                std::cerr << "[EventController] Camera " << pair.first
                          << " is not streaming - excluded from this event (marked missing)." << std::endl;
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
            // Preferred: the saved frame whose own clock reading is closest to
            // the trigger instant as THIS camera saw it. Frames the camera
            // missed cancel out of a clock-based mark, where the frame-count
            // arithmetic below drifts by exactly the number of frames lost.
            int triggerIndex = -1;
            if (s.triggerClockNs > 0) {
                int64_t bestDelta = 0;
                for (size_t i = 0; i < s.currentFillSize; ++i) {
                    const size_t idx = (tail + i) % s.circularBuffer.size();
                    const int64_t ts = s.circularBuffer[idx].timestamp;
                    if (ts <= 0) {
                        continue;
                    }
                    const int64_t delta = std::llabs(ts - s.triggerClockNs);
                    if (triggerIndex < 0 || delta < bestDelta) {
                        triggerIndex = static_cast<int>(i);
                        bestDelta = delta;
                    }
                }
            }
            if (triggerIndex < 0) {
                // No usable clock for this camera: the ring rolls while a
                // downstream camera waits for the defect to arrive, so the defect
                // lands at a stable position (pre-trigger depth) in every
                // camera's saved window.
                triggerIndex = static_cast<int>(s.currentFillSize)
                    - s.postFramesRecorded - 1 + s.captureOffsetFrames;
            }
            // Guard against degenerate extreme-upstream offsets that could push
            // the index out of the saved window.
            const int maxIndex = std::max(0, static_cast<int>(s.currentFillSize) - 1);
            s.linearizedTriggerIndex = std::max(0, std::min(triggerIndex, maxIndex));
        }

        // A group camera that never delivered a frame has no buffer state at
        // all, so it never passed through the loop above. The trigger expected
        // it, so it is missing from this event too.
        if (groupRestricted_) {
            for (int cameraId : recordCameraIds_) {
                if (cameraStates_.find(cameraId) == cameraStates_.end()) {
                    currentEventMissingCameraIds_.push_back(cameraId);
                    std::cerr << "[EventController] Camera " << cameraId
                              << " is in the trigger's group but never streamed - marked missing."
                              << std::endl;
                }
            }
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

bool EventController::triggerEvent(const TriggerContext& context, QString* ignoreReason) {
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

    // Determine which cameras participate: a trigger records the sections it
    // selected (recordGroups). No selection, or every section selected, records
    // all cameras (legacy behavior) - including cameras whose group is still
    // unassigned; a narrower selection records only cameras assigned to it.
    const std::set<int> scope(context.recordGroups.begin(), context.recordGroups.end());
    const bool scopeCoversAll = scope.empty()
        || static_cast<int>(scope.size()) >= CameraGroup::kCount;
    groupRestricted_ = !scopeCoversAll;
    recordCameraIds_.clear();
    if (groupRestricted_) {
        for (size_t i = 0; i < cameras.size(); ++i) {
            if (scope.count(cameras[i].group) > 0) {
                recordCameraIds_.insert(static_cast<int>(i) + 1);
            }
        }
        if (recordCameraIds_.empty()) {
            const QString sections = sectionNames(scope);
            std::cout << "[EventController] Trigger ignored: no camera is assigned to "
                      << sections.toStdString() << std::endl;
            if (ignoreReason) {
                *ignoreReason = QString("no camera is assigned to %1").arg(sections);
            }
            return false;
        }
    }

    // Reset every camera's capture state; targets are assigned below.
    for (auto& pair : cameraStates_) {
        pair.second.postFramesRecorded = 0;
        pair.second.captureTargetFrames = -1;
        pair.second.captureOffsetFrames = 0;
        pair.second.triggerClockNs = 0;
        pair.second.captureStopClockNs = 0;
        pair.second.captureWindowMs = 0;
        pair.second.captureStartMs = 0;
        pair.second.captureDone = false;
    }
    currentEventMissingCameraIds_.clear();
    const int64_t triggerMs = nowMs();

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
                armTimeWindow(pair.second, 0.0, postWindowSeconds(), triggerMs);
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
            // The same travel distance as a duration — the value the camera's own
            // clock has to cover, which stays right no matter how many frames it
            // manages to deliver on the way.
            const double travelSeconds = (localSpeed > 0.0)
                ? static_cast<double>(deltaMm) / (localSpeed * 1000.0) * 60.0
                : 0.0;
            armTimeWindow(pair.second, travelSeconds, postWindowSeconds(), triggerMs);
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
            // No spatial alignment: the window is the plain post-trigger window,
            // still clocked by the camera itself so missed frames only shorten it.
            armTimeWindow(pair.second, 0.0, postWindowSeconds(), triggerMs);
        }
    }

    // If no participating camera is actually streaming frames right now (never
    // streamed, or stopped streaming recently), the event could never complete
    // and triggering_ would stay set forever, silently swallowing every later
    // trigger. Bail out cleanly instead.
    const int64_t now = triggerMs;
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
        if (ignoreReason) {
            *ignoreReason = QStringLiteral("no active camera is streaming frames");
        }
        return false;
    }

    triggering_ = true;
    if (triggeredCallback_) triggeredCallback_(currentTimestamp_, reason);
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

    // Rebuild the ring preserving the newest keepCount frames. Copy (refcount)
    // instead of moving out of the live buffer: the acquisition thread feeds
    // these slots every frame, so keep this path simple and obviously safe.
    std::vector<FrameData> fresh;
    fresh.reserve(newCapacity);
    const size_t oldSize = it->second.circularBuffer.size();
    // Never claim slots that were never written: keepCount must not exceed the
    // actual number of delivered frames (currentFillSize). A camera that just
    // started (few frames in a ring sized for its free-run rate) would
    // otherwise have its "newest keepCount" window wrap into never-written
    // default slots; currentFillSize was then over-declared and a trigger
    // during the refill captured those empty slots, saving a header-only
    // 0-width .bin (observed: "Recording…" events where cam1 shows nothing).
    const size_t keepCount =
        std::min({newCapacity, oldSize, it->second.currentFillSize});
    for (size_t i = 0; i < keepCount; ++i) {
        const size_t idx = (it->second.writeIndex + oldSize - keepCount + i) % oldSize;
        fresh.push_back(it->second.circularBuffer[idx]);
    }
    fresh.resize(newCapacity);
    it->second.circularBuffer.swap(fresh);
    it->second.writeIndex = keepCount % newCapacity;
    it->second.currentFillSize = keepCount;
    it->second.postFramesRecorded = 0;
}

void EventController::setEventSavedCallback(EventSavedCallback callback) {
    callback_ = callback;
}

void EventController::setEventTriggeredCallback(EventTriggeredCallback callback) {
    triggeredCallback_ = std::move(callback);
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
            std::vector<int> missingCameraIds;
            TriggerContext triggerContext;

            {
                std::lock_guard<std::mutex> bufferLock(bufferMutex_);
                for (auto& pair : cameraStates_) {
                    framesToSave[pair.first].swap(pair.second.saveQueue);
                    triggerIndices[pair.first] = pair.second.linearizedTriggerIndex;
                }
                eventCameraLabels = currentEventCameraLabels_;
                eventCameraPositions = currentEventCameraPositions_;
                missingCameraIds = currentEventMissingCameraIds_;
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
                
                // Save as Raw Binary with camera suffix. A camera that captured
                // no usable pixels (mid-startup ring with unwritten slots) is
                // skipped so the event is not corrupted by a 0-width file; the
                // primary camera then falls back to the first healthy one.
                if (!saveAsRaw(frames, baseName, triggerIndex, cameraId)) {
                    std::cout << "[EventController] Skipping Camera " << cameraId
                              << ": no usable frames captured." << std::endl;
                    missingCameraIds.push_back(cameraId);
                    continue;
                }

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
                event.missingCameraIds = missingCameraIds;
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

// Effective fps of one saved sequence: the median gap between the frames' own
// clock readings, so missed frames lower the rate instead of averaging out.
// `fallbackFps` (the configured/detected rate) is kept whenever the clock is
// missing, wrapped, or implausibly far from it — a stalled camera must not be
// allowed to stretch the event's time axis.
static double measuredSequenceFps(const std::deque<EventController::FrameData>& frames,
                                  double fallbackFps) {
    if (!(fallbackFps > 0.0) || frames.size() < 3) {
        return fallbackFps;
    }

    std::vector<int64_t> gaps;
    gaps.reserve(frames.size() - 1);
    int64_t previous = 0;
    for (const EventController::FrameData& frame : frames) {
        if (frame.timestamp <= 0) {
            return fallbackFps;
        }
        if (previous > 0) {
            const int64_t gap = frame.timestamp - previous;
            if (gap <= 0) {
                return fallbackFps;  // wrapped/reset camera clock
            }
            gaps.push_back(gap);
        }
        previous = frame.timestamp;
    }
    if (gaps.empty()) {
        return fallbackFps;
    }

    std::sort(gaps.begin(), gaps.end());
    const int64_t medianGap = gaps[gaps.size() / 2];
    if (medianGap <= 0) {
        return fallbackFps;
    }
    const double measured = 1e9 / static_cast<double>(medianGap);
    if (measured < fallbackFps * 0.25 || measured > fallbackFps * 1.1) {
        return fallbackFps;
    }
    return measured;
}

bool EventController::saveAsRaw(const std::deque<FrameData>& frames, const QString& baseName, int triggerIndex, int cameraId) {
    // Defense in depth: never write a file with no pixels. A header-only
    // 0-width recording (frames captured from never-written ring slots) would
    // show as a dead camera in review and its bogus frame count could wreck
    // the timeline, so refuse to produce one here.
    if (frames.empty() || frames.front().image.empty()
            || frames.front().image.cols <= 0 || frames.front().image.rows <= 0) {
        return false;
    }

    QString filename = QDir(CameraConfig::getEventStoragePath()).filePath(
        QString("event_%1_cam%2.bin").arg(baseName).arg(cameraId));
    std::ofstream outFile(filename.toStdString(), std::ios::binary);
    
    if (!outFile) {
        std::cerr << "[EventController] Failed to open raw file for writing: " << filename.toStdString() << std::endl;
        return false;
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
    // The configured/detected rate is only the fallback — when the saved frames
    // carry a usable clock, their own median gap says what really arrived, so a
    // camera that missed frames converts frame index -> seconds with the rate it
    // really delivered instead of drifting against the other cameras.
    header.fps = measuredSequenceFps(frames, cameraFps(cameraId));
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
    return true;
}
