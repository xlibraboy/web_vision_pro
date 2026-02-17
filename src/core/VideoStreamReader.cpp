#include "VideoStreamReader.h"
#include <iostream>

#include "RawFormat.h"
#include <QFileInfo>
#include <QFileInfo>
#include <cstdio>
#include <pylon/PylonIncludes.h>
#ifdef PYLON_WIN_BUILD
#    include <pylon/PylonGUI.h>
#endif
using namespace Pylon;

VideoStreamReader::VideoStreamReader() 
    : totalFrames_(0), fps_(0), width_(0), height_(0), pixelFormat_(0), fileHandle_(nullptr), isRawMode_(false) {
}

VideoStreamReader::~VideoStreamReader() {
    close();
}

bool VideoStreamReader::open(const QString& filepath) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    close(); // Close any existing file
    
    filepath_ = filepath;
    
    // Check extension for .bin (Raw Mode)
    if (filepath.endsWith(".bin", Qt::CaseInsensitive)) {
        isRawMode_ = true;
        fileHandle_ = std::fopen(filepath.toStdString().c_str(), "rb");
        
        if (!fileHandle_) {
            std::cerr << "[VideoStreamReader] Failed to open RAW file: " << filepath.toStdString() << std::endl;
            return false;
        }
        
        // Read header
        RawFileHeader header;
        if (std::fread(&header, sizeof(header), 1, fileHandle_) != 1) {
            std::cerr << "[VideoStreamReader] Failed to read RAW header!" << std::endl;
            return false;
        }
        
        // Validate magic
        if (std::memcmp(header.magic, RAW_FILE_MAGIC, 4) != 0) {
            std::cerr << "[VideoStreamReader] Invalid RAW magic number!" << std::endl;
            return false;
        }
        
        width_ = header.width;
        height_ = header.height;
        fps_ = header.fps;
        totalFrames_ = header.totalFrames;
        pixelFormat_ = header.pixelFormat;  // Store pixel format from header
        
        std::cout << "[VideoStreamReader] Opened RAW: " << filepath.toStdString() 
                  << " (" << totalFrames_ << " frames, " << width_ << "x" << height_ 
                  << ", format=" << pixelFormat_ << ")" << std::endl;
        
    } else {
        // Video Mode (.mp4, .avi, etc)
        isRawMode_ = false;
        capture_.open(filepath.toStdString());
        
        if (!capture_.isOpened()) {
            std::cerr << "[VideoStreamReader] Failed to open: " << filepath.toStdString() << std::endl;
            return false;
        }
        
        // Read video properties
        totalFrames_ = static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_COUNT));
        fps_ = capture_.get(cv::CAP_PROP_FPS);
        width_ = static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_WIDTH));
        height_ = static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_HEIGHT));
        
        std::cout << "[VideoStreamReader] Opened Video: " << filepath.toStdString() 
                  << " (" << totalFrames_ << " frames, " << fps_ << " fps)" << std::endl;
    }
    
    frameCache_.clear();
    return true;
}

cv::Mat VideoStreamReader::getFrame(int frameIndex) {
    if (frameIndex < 0 || frameIndex >= totalFrames_) {
        return cv::Mat();  // Return empty mat for out-of-bounds
    }
    
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    // Check cache first
    auto it = frameCache_.find(frameIndex);
    if (it != frameCache_.end()) {
        return it->second.clone();  // Return copy from cache
    }
    
    // Load from disk
    cv::Mat frame = loadFrameFromDisk(frameIndex);
    if (!frame.empty()) {
        addToCache(frameIndex, frame);
    }
    
    return frame;
}

void VideoStreamReader::preloadChunk(int centerFrame, int radius) {
    int startFrame = std::max(0, centerFrame - radius);
    int endFrame = std::min(totalFrames_ - 1, centerFrame + radius);
    
    // Load frames in background (simplified - actual implementation could use thread pool)
    for (int i = startFrame; i <= endFrame; ++i) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        
        // Skip if already in cache
        if (frameCache_.find(i) != frameCache_.end()) {
            continue;
        }
        
        cv::Mat frame = loadFrameFromDisk(i);
        if (!frame.empty()) {
            addToCache(i, frame);
        }
    }
}

cv::Mat VideoStreamReader::loadFrameFromDisk(int frameIndex) {
    if (isRawMode_) {
        if (!fileHandle_) return cv::Mat();
        
        // Calculate bytes per pixel based on pixel format
        // 0 = Mono8 (1 byte), 1 = BGR8 (3 bytes), 2 = RGB8 (3 bytes)
        int bytesPerPixel = (pixelFormat_ == 0) ? 1 : 3;
        
        // Calculate offset
        // Header + (FrameSize + MetadataSize) * index
        long frameSize = width_ * height_ * bytesPerPixel; 
        long entrySize = frameSize + sizeof(FrameMetadata);
        long offset = sizeof(RawFileHeader) + (frameIndex * entrySize);
        
        if (std::fseek(fileHandle_, offset, SEEK_SET) != 0) {
            return cv::Mat();
        }
        
        // Create frame based on pixel format
        cv::Mat frame;
        if (pixelFormat_ == 0) {
            // Mono8
            frame = cv::Mat(height_, width_, CV_8UC1);
        } else {
            // BGR8 or RGB8
            frame = cv::Mat(height_, width_, CV_8UC3);
        }
        
        if (std::fread(frame.data, frameSize, 1, fileHandle_) != 1) {
            return cv::Mat();
        }
        
        // Note: We skip the metadata chunk here for image display, 
        // but it's available on disk if needed for analysis.
        
        return frame;
        
    } else {
        if (!capture_.isOpened()) {
            return cv::Mat();
        }
        
        // Seek to frame
        capture_.set(cv::CAP_PROP_POS_FRAMES, frameIndex);
        
        cv::Mat frame;
        capture_ >> frame;
        
        return frame.clone();
    }
}

void VideoStreamReader::addToCache(int frameIndex, const cv::Mat& frame) {
    // Check if cache is full
    if (frameCache_.size() >= static_cast<size_t>(cacheMaxSize_)) {
        evictOldestFromCache();
    }
    
    frameCache_[frameIndex] = frame.clone();
}

void VideoStreamReader::evictOldestFromCache() {
    // Simple eviction: remove first element (oldest insertion order isn't tracked perfectly here,
    // but for simplicity we just remove the first in the map)
    if (!frameCache_.empty()) {
        frameCache_.erase(frameCache_.begin());
    }
}

void VideoStreamReader::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    frameCache_.clear();
}

void VideoStreamReader::close() {
    if (capture_.isOpened()) {
        capture_.release();
    }
    if (fileHandle_) {
        std::fclose(fileHandle_);
        fileHandle_ = nullptr;
    }
    frameCache_.clear();
    frameCache_.clear();
}

bool VideoStreamReader::exportToMp4(const QString& outputPath, int fps) {
    if (!isRawMode_ || !fileHandle_) {
        std::cerr << "[VideoStreamReader] Export only supported for open RAW files." << std::endl;
        return false;
    }

    try {
        if (!CVideoWriter::IsSupported()) {
             std::cerr << "[VideoStreamReader] Pylon VideoWriter not supported!" << std::endl;
             return false;
        }

        CVideoWriter videoWriter;
        
        // Map pixel type
        EPixelType pylonPixelType = PixelType_Mono8;
        if (pixelFormat_ == 1) pylonPixelType = PixelType_BGR8packed;
        else if (pixelFormat_ == 2) pylonPixelType = PixelType_RGB8packed;

        // Set parameters
        videoWriter.SetParameter(
            (uint32_t)width_,
            (uint32_t)height_,
            pylonPixelType,
            fps,
            90 // Quality
        );

        videoWriter.Open(outputPath.toStdString().c_str());

        std::cout << "[VideoStreamReader] Exporting to " << outputPath.toStdString() << "..." << std::endl;
        
        // Loop through all frames
        for (int i = 0; i < totalFrames_; ++i) {
            cv::Mat frame = loadFrameFromDisk(i);
            if (frame.empty()) continue;

            // Wrap cv::Mat data in CPylonImage
            CPylonImage image;
            image.AttachUserBuffer(
                frame.data,
                frame.total() * frame.elemSize(),
                pylonPixelType,
                width_,
                height_,
                0 // PaddingX
            );

            videoWriter.Add(image);
            
            if (i % 50 == 0) {
                 std::cout << "[VideoStreamReader] Export progress: " << i << "/" << totalFrames_ << std::endl;
            }
        }

        videoWriter.Close();
        std::cout << "[VideoStreamReader] Export complete." << std::endl;
        return true;

    } catch (const GenericException& e) {
        std::cerr << "[VideoStreamReader] Pylon Error: " << e.GetDescription() << std::endl;
        return false;
    }
}
