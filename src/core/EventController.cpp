#include "EventController.h"
#include "EventDatabase.h"
#include "RawFormat.h"
#include <iostream>
#include <fstream>
#include <cstring>
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
        int recorded = ++state.postFramesRecorded;
        if (recorded >= postTriggerLimit_) {
            // Reached limit for this camera. We don't stop the whole trigger process yet,
            // we let the saveWorker handle saving all states once requested.
            // But we can check if all active cameras have hit the limit.
            bool allDone = true;
            for (const auto& pair : cameraStates_) {
                if (pair.second.postFramesRecorded < postTriggerLimit_) {
                    allDone = false;
                    break;
                }
            }
            
            if (allDone && !saveRequested_) {
                std::cout << "[EventController] Post-trigger capture complete for all cameras. Moving to save queue." << std::endl;
                
                {
                    std::lock_guard<std::mutex> saveLock(saveMutex_);
                    
                    for (auto& pair : cameraStates_) {
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
                        
                        // Calculate linearized trigger index for the saved sequence
                        s.linearizedTriggerIndex = static_cast<int>(s.currentFillSize) - s.postFramesRecorded - 1;
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
    if (triggering_) return;
    
    std::cout << "[EventController] EVENT TRIGGERED! Starting post-trigger recording." << std::endl;
    
    std::lock_guard<std::mutex> lock(bufferMutex_);
    // Add Milliseconds to Event ID for higher precision
    currentTimestamp_ = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz").toStdString();
    
    for (auto& pair : cameraStates_) {
        pair.second.postFramesRecorded = 0;
    }
    
    triggering_ = true;
}

bool EventController::isSaving() const {
    return triggering_ || saveRequested_;
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
            
            {
                std::lock_guard<std::mutex> bufferLock(bufferMutex_);
                for (auto& pair : cameraStates_) {
                    framesToSave[pair.first].swap(pair.second.saveQueue);
                    triggerIndices[pair.first] = pair.second.linearizedTriggerIndex;
                }
            }
            
            saveRequested_ = false;
            lock.unlock();
            
            QString baseName = QString::fromStdString(currentTimestamp_);
            
            // Ensure directory exists
            QDir().mkpath("../data");
            
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
                    primaryFilename = QString("../data/event_%1_cam%2.bin").arg(baseName).arg(cameraId);
                    primarySaved = true;
                    primaryCameraId = cameraId;
                }
            }
            
            // Register event in database for the primary camera to prevent duplicates
            if (primarySaved) {
                EventDatabase::EventInfo event;
                event.timestamp = QString::fromStdString(currentTimestamp_); 
                event.videoPath = primaryFilename; // Points to the primary .bin file
                event.metadataPath = "";    // Embedded in .bin
                event.triggerIndex = primaryTriggerIndex; 
                event.totalFrames = primaryFramesCount;
                event.fps = fps_;
                event.width = primaryWidth;
                event.height = primaryHeight;
                
                EventDatabase::instance().registerEvent(event);

                // Notify UI with CORRECT linearized index from the primary camera
                if (callback_) {
                    callback_(currentTimestamp_, primaryTriggerIndex, primaryFramesCount);
                }
            }
            
            triggering_ = false;
        }
    }
}

void EventController::saveAsRaw(const std::deque<FrameData>& frames, const QString& baseName, int triggerIndex, int cameraId) {
    if (frames.empty()) return;

    QString filename = QString("../data/event_%1_cam%2.bin").arg(baseName).arg(cameraId);
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
    for (const auto& frameData : frames) {
        // Pixel Data MUST be written FIRST according to VideoStreamReader
        if (frameData.image.isContinuous()) {
            outFile.write(reinterpret_cast<const char*>(frameData.image.data), frameSize);
        } else {
            cv::Mat cont = frameData.image.clone();
            outFile.write(reinterpret_cast<const char*>(cont.data), frameSize);
        }

        // Frame Metadata MUST be written SECOND (appended after image)
        FrameMetadata meta = {};
        meta.timestamp = 0; // The frameData.timestamp is a double now in struct FrameData, but Unix timestamp in string etc, we could pass 0 or mock it
        meta.frameId = frameData.frameCounter;
        meta.flags = 0;
        
        outFile.write(reinterpret_cast<const char*>(&meta), sizeof(FrameMetadata));
    }
    
    outFile.close();
    std::cout << "[EventController] Raw save complete for camera " << cameraId << std::endl;
}
