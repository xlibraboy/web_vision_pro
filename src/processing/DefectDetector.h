#pragma once

#include <opencv2/opencv.hpp>

class DefectDetector {
public:
    DefectDetector();
    ~DefectDetector();

    // Returns true if a defect/paper break is detected
    bool detect(const cv::Mat& frame);

private:
    double last_average_brightness_;
    bool first_frame_;
    const double THRESHOLD_CHANGE = 30.0; // Arbitrary threshold for brightness change
    
    // Memory optimization: Reuse member matrix
    cv::Mat grayBound_;
    cv::Mat gray_; // Added gray_ member
};
