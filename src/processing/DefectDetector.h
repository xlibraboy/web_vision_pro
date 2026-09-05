#pragma once

#include <opencv2/opencv.hpp>

class DefectDetector {
public:
    DefectDetector();
    ~DefectDetector();

    // Returns true if a defect/paper break is detected. When a non-empty mask
    // (CV_8U, same size as frame) is given, the brightness average is computed
    // only over the masked pixels (ROI-restricted detection).
    bool detect(const cv::Mat& frame, const cv::Mat& mask = cv::Mat());

private:
    double last_average_brightness_;
    bool first_frame_;
    const double THRESHOLD_CHANGE = 30.0; // Arbitrary threshold for brightness change
    
    // Memory optimization: Reuse member matrix
    cv::Mat grayBound_;
    cv::Mat gray_; // Added gray_ member
};
