#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <mutex>
#include <atomic>

/**
 * Thread-safe circular buffer pool for preallocated cv::Mat frames.
 * Eliminates per-frame memory allocation overhead.
 */
class BufferPool {
public:
    /**
     * Create a buffer pool with specified capacity and frame dimensions.
     * @param poolSize Number of buffers in the pool (typically 3-5)
     * @param width Frame width
     * @param height Frame height
     * @param type OpenCV type (e.g., CV_8UC3)
     */
    BufferPool(size_t poolSize, int width, int height, int type = CV_8UC3)
        : poolSize_(poolSize), writeIndex_(0) {
        
        // Preallocate all buffers
        buffers_.reserve(poolSize);
        for (size_t i = 0; i < poolSize; ++i) {
            buffers_.emplace_back(height, width, type);
        }
    }
    
    /**
     * Get the next available buffer for writing.
     * Uses circular indexing - oldest buffer gets overwritten.
     * @return Reference to the next writable cv::Mat buffer
     */
    cv::Mat& getNextBuffer() {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t index = writeIndex_.fetch_add(1) % poolSize_;
        return buffers_[index];
    }
    
    /**
     * Get a copy of the buffer at specified index.
     * Safe for reading while other threads write.
     */
    cv::Mat getBufferCopy(size_t index) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index >= buffers_.size()) {
            return cv::Mat();
        }
        return buffers_[index].clone();
    }
    
    size_t size() const { return poolSize_; }
    
private:
    size_t poolSize_;
    std::vector<cv::Mat> buffers_;
    std::atomic<size_t> writeIndex_;
    mutable std::mutex mutex_;
};
