#include "EventController.h"
#include "EventDatabase.h"
#include "RawFormat.h"
#include "../config/CameraConfig.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <limits>
#include <cmath>
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

void EventController::addFrame(int cameraId, const cv::Mat& frame, int64_t timestamp, int64_t frameCounter) {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    // Initialize state if camera not seen before
    if (cameraStates_.find(cameraId) == cameraStates_.end()) {
        size_t totalCapacity = bufferSize_ + postTriggerLimit_;
        cameraStates_[cameraId].circularBuffer.resize(totalCapacity);
    }
    
    CameraBufferState& state = cameraStates_[cameraId];

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
            // Reached limit for this camera. We don't stop the whole trigger process yet,
            // we let the saveWorker handle saving all states once requested.
            // But we can check if all participating cameras have hit the limit.
            bool allDone = true;
            for (const auto& pair : cameraStates_) {
                if (groupRestricted_ && recordCameraIds_.count(pair.first) == 0) {
                    continue;
                }
                if (pair.second.captureTargetFrames >= 0
                        && pair.second.postFramesRecorded < pair.second.captureTargetFrames) {
                    allDone = false;
                    break;
                }
            }
            
            if (allDone && !saveRequested_) {
                std::cout << "[EventController] Post-trigger capture complete for the triggered camera group. Moving to save queue." << std::endl;
                
                {
                    std::lock_guard<std::mutex> saveLock(saveMutex_);
                    
                    for (auto& pair : cameraStates_) {
                        if (groupRestricted_ && recordCameraIds_.count(pair.first) == 0) {
                            continue;
                        }
                        CameraBufferState& s = pair.second;
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
                        // The ring rolls while a downstream camera waits for the
                        // defect to arrive, so the defect lands at a stable position
                        // (pre-trigger depth) in every camera's saved window.
                        s.linearizedTriggerIndex = static_cast<int>(s.currentFillSize)
                            - s.postFramesRecorded - 1 + s.captureOffsetFrames;
                        // Guard against degenerate extreme-upstream offsets that
                        // could push the index out of the saved window.
                        const int maxIndex = std::max(0, static_cast<int>(s.currentFillSize) - 1);
                        s.linearizedTriggerIndex = std::max(0, std::min(s.linearizedTriggerIndex, maxIndex));
                    }
                    saveRequested_ = true;
                }
                
                triggering_ = false;
                saveCv_.notify_one();
            }
        }
    }
}

void EventController::triggerEvent() {
    triggerEvent(TriggerContext{});
}

void EventController::triggerEvent(const TriggerContext& context) {
    if (triggering_) return;

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
            return;
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

    const bool alignmentWanted = context.triggerPositionMm > 0;
    const bool alignmentEnabled = alignmentWanted && haveSpeed;
    if (alignmentEnabled) {
        const int sign = currentTriggerContext_.positionDirectionSign >= 0 ? 1 : -1;
        // framesPerMm = fps * (60 s/min) / (speed mm/min)
        const double framesPerMm = fps_ * 60.0 / (speedMperMin * 1000.0);
        std::cout << "[EventController] Spatial alignment: speed=" << speedMperMin
                  << " m/min, trigger position=" << context.triggerPositionMm
                  << " mm, sign=" << sign << std::endl;
        for (auto& pair : cameraStates_) {
            if (groupRestricted_ && recordCameraIds_.count(pair.first) == 0) {
                continue;
            }
            const int configIndex = pair.first - 1;
            if (configIndex < 0 || configIndex >= static_cast<int>(cameras.size())) {
                pair.second.captureTargetFrames = postTriggerLimit_;
                continue;
            }
            currentEventCameraLabels_[pair.first] = CameraConfig::getCameraLabel(configIndex);
            currentEventCameraPositions_[pair.first] = cameras[static_cast<size_t>(configIndex)].machinePosition;

            const int deltaMm = (cameras[static_cast<size_t>(configIndex)].machinePosition
                                 - context.triggerPositionMm) * sign;
            int offsetFrames = static_cast<int>(std::lround(deltaMm * framesPerMm));
            // An upstream camera cannot recover a defect that already left its
            // pre-trigger buffer; clamp to the oldest recoverable frame.
            if (offsetFrames < -bufferSize_) {
                offsetFrames = -bufferSize_;
            }
            pair.second.captureOffsetFrames = offsetFrames;
            // Minimum 1 so every participating camera writes at least one frame
            // and the allDone evaluation runs (a target of 0 would early-return
            // before the ring write and could deadlock the whole event).
            pair.second.captureTargetFrames = std::max(1, postTriggerLimit_ + offsetFrames);
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
            pair.second.captureTargetFrames = postTriggerLimit_;
        }
    }

    triggering_ = true;
}

void EventController::setSpeedProvider(SpeedProvider provider) {
    speedProvider_ = std::move(provider);
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

void EventController::setEventSavedCallback(EventSavedCallback callback) {
    callback_ = callback;
}

void EventController::saveWorker() {
    while (running_) {
        std::unique_lock<std::mutex> lock(saveMutex_);
        saveCv_.wait(lock, [this] { return saveRequested_ || !running_; });
        
        if (!running_) break;
        
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
                event.fps = fps_;
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
                event.triggerGroup = triggerContext.group;
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
    header.fps = fps_;
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
