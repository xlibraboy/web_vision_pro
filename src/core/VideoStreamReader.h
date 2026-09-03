#pragma once

#include <opencv2/opencv.hpp>
#include <QString>
#include <map>
#include <mutex>
#include "RawFormat.h"

/**
 * VideoStreamReader - On-demand video frame loader
 * Loads frames from disk video files with LRU caching
 */
class VideoStreamReader {
public:
    VideoStreamReader();
    ~VideoStreamReader();
    
    // Open video file
    bool open(const QString& filepath);
    
    // Get specific frame (loads on-demand, uses cache)
    cv::Mat getFrame(int frameIndex);
    
    // Preload chunk of frames around center position
    void preloadChunk(int centerFrame, int radius = 25);
    
    // Get video properties
    int getTotalFrames() const { return totalFrames_; }
    double getFps() const { return fps_; }
    // Trigger frame index in this file's own numbering (RAW header; 0 for video).
    int getTriggerIndex() const { return triggerIndex_; }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    bool getFrameMetadata(int frameIndex, FrameMetadata& metadata);
    
    // Clear cache
    void clearCache();
    
    // Close video
    void close();

    // Export to MP4
    bool exportToMp4(const QString& outputPath, int fps = 20);

private:
    cv::VideoCapture capture_;
    QString filepath_;
    
    // Video properties
    int totalFrames_;
    double fps_;
    int triggerIndex_;  // Trigger frame index from the RAW header
    int width_;

    int height_;
    uint32_t pixelFormat_;  // Pixel format from RAW file header (0=Mono8, 1=BGR8, 2=RGB8)
    
    // Raw mode support
    bool isRawMode_;
    FILE* fileHandle_;
    
    // Frame cache (LRU)
    std::map<int, cv::Mat> frameCache_;
    std::mutex cacheMutex_;
    int cacheMaxSize_ = 100;  // Keep up to 100 frames in memory
    
    // Internal helpers
    cv::Mat loadFrameFromDisk(int frameIndex);
    void addToCache(int frameIndex, const cv::Mat& frame);
    void evictOldestFromCache();
};
