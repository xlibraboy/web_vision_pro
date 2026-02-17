#include "EventController.h"
#include "EventDatabase.h"
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
    postFramesRecorded_ = 0;
    running_ = true;
    saveRequested_ = false;
    
    {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        size_t totalCapacity = bufferSize_ + postTriggerLimit_;
        if (circularBuffer_.size() != totalCapacity) {
             circularBuffer_.resize(totalCapacity);
        }
        writeIndex_ = 0;
        currentFillSize_ = 0;
    }
    
    // Start worker thread
    saveThread_ = std::thread(&EventController::saveWorker, this);
    
    std::cout << "[EventController] Initialized with pre-trigger buffer: " << bufferSize_ 
              << " (" << (bufferSize_ / fps_) << "s), post-trigger: " << postTriggerLimit_ 
              << " (" << (postTriggerLimit_ / fps_) << "s), format: RAW BINARY (.bin)"
              << std::endl;
}

void EventController::addFrame(const cv::Mat& frame, int64_t timestamp, int64_t frameCounter) {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    // Ring Buffer Logic
    // 1. Copy frame into current write slot
    FrameData& target = circularBuffer_[writeIndex_];
    
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
    writeIndex_ = (writeIndex_ + 1) % circularBuffer_.size();
    
    // 4. Track fill size
    if (currentFillSize_ < circularBuffer_.size()) {
        currentFillSize_++;
    }

    // If we are currently collecting post-trigger frames
    if (triggering_) {
        int recorded = ++postFramesRecorded_;
        if (recorded >= postTriggerLimit_) {
            std::cout << "[EventController] Post-trigger capture complete. Moving to save queue." << std::endl;
            
            {
                std::lock_guard<std::mutex> saveLock(saveMutex_);
                saveQueue_.clear();
                
                size_t tail = (currentFillSize_ < circularBuffer_.size()) ? 0 : writeIndex_;
                
                for (size_t i = 0; i < currentFillSize_; ++i) {
                    size_t idx = (tail + i) % circularBuffer_.size();
                    // Deep copy for saving
                    FrameData fd;
                    fd.image = circularBuffer_[idx].image.clone();
                    fd.timestamp = circularBuffer_[idx].timestamp;
                    fd.frameCounter = circularBuffer_[idx].frameCounter;
                    saveQueue_.push_back(fd);
                }
                
                // Calculate linearized trigger index for the saved sequence
                // The sequence contains [Pre-Trigger ... Trigger ... Post-Trigger]
                // Total Frames = currentFillSize_
                // Post Frames = postFramesRecorded_
                // Trigger Position = Total - Post - 1
                linearizedTriggerIndex_ = static_cast<int>(currentFillSize_) - postFramesRecorded_ - 1;
                
                saveRequested_ = true;
            }
            
            triggering_ = false;
            saveCv_.notify_one();
        }
    }
}

void EventController::triggerEvent() {
    if (triggering_) return;
    
    std::cout << "[EventController] EVENT TRIGGERED! Starting post-trigger recording." << std::endl;
    
    std::lock_guard<std::mutex> lock(bufferMutex_);
    triggerIndex_ = static_cast<int>(currentFillSize_) - 1;
    // Add Milliseconds to Event ID for higher precision
    currentTimestamp_ = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz").toStdString();
    postFramesRecorded_ = 0;
    triggering_ = true;
}

bool EventController::isSaving() const {
    return triggering_;
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
            // Swap to local queue
            std::deque<FrameData> framesToSave;
            framesToSave.swap(saveQueue_);
            saveRequested_ = false;
            lock.unlock();
            
            if (framesToSave.empty()) {
                triggering_ = false;
                continue;
            }
            
            QString baseName = QString::fromStdString(currentTimestamp_);
            
            // Ensure directory exists
            QDir().mkpath("../data");
            
            int framesCount = static_cast<int>(framesToSave.size());
            std::cout << "[EventController] Saving " << framesCount << " frames..." << std::endl;
            
            // Save as Raw Binary
            saveAsRaw(framesToSave, baseName, linearizedTriggerIndex_);
            
            // Notify UI with CORRECT linearized index
            if (callback_) {
                callback_(currentTimestamp_, linearizedTriggerIndex_, framesCount);
            }
            
            triggering_ = false;
        }
    }
}

void EventController::saveAsRaw(const std::deque<FrameData>& frames, const QString& baseName, int triggerIndex) {
    if (frames.empty()) return;

    QString filename = QString("../data/event_%1.bin").arg(baseName);
    std::ofstream outFile(filename.toStdString(), std::ios::binary);
    
    if (!outFile) {
        std::cerr << "[EventController] Failed to open raw file for writing: " << filename.toStdString() << std::endl;
        return;
    }

    // 1. Write Global Header
    // Magic: "PVISION" (8 bytes including null)
    // Version: 1
    const char magic[] = "PVISION";
    int32_t version = 1;
    int32_t width = frames[0].image.cols;
    int32_t height = frames[0].image.rows;
    int32_t pixelType = frames[0].image.type();
    int32_t frameCount = static_cast<int32_t>(frames.size());

    outFile.write(magic, sizeof(magic));
    outFile.write(reinterpret_cast<const char*>(&version), sizeof(version));
    outFile.write(reinterpret_cast<const char*>(&width), sizeof(width));
    outFile.write(reinterpret_cast<const char*>(&height), sizeof(height));
    outFile.write(reinterpret_cast<const char*>(&pixelType), sizeof(pixelType));
    outFile.write(reinterpret_cast<const char*>(&frameCount), sizeof(frameCount));
    
    // Calculate frame size
    size_t frameSize = frames[0].image.total() * frames[0].image.elemSize();

    std::cout << "[EventController] Writing Raw Binary to " << filename.toStdString() 
              << " (" << (frameSize * frameCount / 1024 / 1024) << " MB)..." << std::endl;

    // 2. Write Frames
    for (const auto& frameData : frames) {
        // Frame Metadata
        outFile.write(reinterpret_cast<const char*>(&frameData.timestamp), sizeof(frameData.timestamp));
        outFile.write(reinterpret_cast<const char*>(&frameData.frameCounter), sizeof(frameData.frameCounter));
        
        // Pixel Data
        // Ensure continuous
        if (frameData.image.isContinuous()) {
            outFile.write(reinterpret_cast<const char*>(frameData.image.data), frameSize);
        } else {
             // Should not happen with our allocation strategy, but safety check
            cv::Mat cont = frameData.image.clone();
            outFile.write(reinterpret_cast<const char*>(cont.data), frameSize);
        }
    }
    
    outFile.close();
    std::cout << "[EventController] Raw save complete." << std::endl;

    // Register event in database
    EventDatabase::EventInfo event;
    event.timestamp = QString::fromStdString(currentTimestamp_);
    event.videoPath = filename; // Points to .bin file
    event.metadataPath = "";    // Embedded in .bin
    event.triggerIndex = triggerIndex; // Use the passed linearized index
    event.totalFrames = frameCount;
    event.fps = fps_;
    event.width = width;
    event.height = height;
    
    EventDatabase::instance().registerEvent(event);
}
