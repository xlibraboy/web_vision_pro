#include "ImageBuffer.h"

ImageBuffer::ImageBuffer(size_t capacity, int width, int height) 
    : capacity_(capacity), writeIndex_(0), currentSize_(0) {
    // Pre-allocate memory to avoid runtime allocations
    buffer_.reserve(capacity);
    for (size_t i = 0; i < capacity; ++i) {
        // Assume Mono8 (1 channel) for optimization
        buffer_.emplace_back(height, width, CV_8UC1);
    }
}

ImageBuffer::~ImageBuffer() {}

void ImageBuffer::addFrame(const cv::Mat& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Copy into pre-allocated buffer
    // Ensure dimensions match, otherwise reallocation will happen automatically by OpenCV
    // causing a performance hit but not a crash.
    frame.copyTo(buffer_[writeIndex_]);
    
    writeIndex_ = (writeIndex_ + 1) % capacity_;
    if (currentSize_ < capacity_) {
        currentSize_++;
    }
}

std::vector<cv::Mat> ImageBuffer::getSnapshot(size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<cv::Mat> snapshot;
    size_t to_copy = std::min(count, currentSize_);
    snapshot.reserve(to_copy);
    
    // Reconstruct chronological order: oldest -> newest
    // Oldest is at (writeIndex_ - currentSize_ + capacity_) % capacity_
    // But easier: verify logic.
    // If full: writeIndex_ points to "Oldest" (which will be overwritten next).
    // If not full: 0 is oldest.
    
    size_t oldestIndex;
    if (currentSize_ < capacity_) {
        oldestIndex = 0;
    } else {
        oldestIndex = writeIndex_;
    }

    for (size_t i = 0; i < to_copy; ++i) {
        // We want the last N frames.
        // The NEWEST frame is at (writeIndex_ - 1 + capacity_) % capacity_
        // The frame before that is at (writeIndex_ - 2 + capacity_) % capacity_
        // ...
        // We want them effectively in chronological order for video encoding?
        // Usually file writers want Frame 0, Frame 1... where Frame 1 is older than Frame 2? 
        // No, Frame 0 is first, Frame N is last.
        // So we want Oldest -> Newest subset?
        // "getLastSeconds" implies the most recent chunk.
        // So let's grab the range [Newest - to_copy + 1] to [Newest].
        
        // Let's retrieve them in order:
        // Index of the frame (to_copy - 1 - i) steps back from current writeIndex
        
        size_t idx = (writeIndex_ - to_copy + i + capacity_) % capacity_;
        snapshot.push_back(buffer_[idx].clone());
    }
    
    return snapshot;
}

std::vector<cv::Mat> ImageBuffer::getLastSeconds(double seconds, int fps) {
    size_t frames_needed = static_cast<size_t>(seconds * fps);
    return getSnapshot(frames_needed);
}
