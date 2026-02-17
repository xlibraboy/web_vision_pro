#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <mutex>
#include <deque>

class ImageBuffer {
public:
    ImageBuffer(size_t capacity, int width, int height); 
    ~ImageBuffer();

    void addFrame(const cv::Mat& frame);
    std::vector<cv::Mat> getSnapshot(size_t count);
    
    // Returns frames from the last 'seconds' based on fps
    std::vector<cv::Mat> getLastSeconds(double seconds, int fps);

private:
    size_t capacity_;
    std::vector<cv::Mat> buffer_; // Fixed size circular buffer
    size_t writeIndex_ = 0;
    size_t currentSize_ = 0;
    
    std::mutex mutex_;
};
