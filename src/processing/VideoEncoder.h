#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <future>

class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder();

    // Saves the provided frames to a video file asynchronously
    void saveVideoAsync(const std::vector<cv::Mat>& frames, const std::string& filename, int fps);

private:
    void encodeLoop(std::vector<cv::Mat> frames, std::string filename, int fps);
};
