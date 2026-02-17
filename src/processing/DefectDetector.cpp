#include "DefectDetector.h"

DefectDetector::DefectDetector() : last_average_brightness_(0.0), first_frame_(true) {}

DefectDetector::~DefectDetector() {}

bool DefectDetector::detect(const cv::Mat& frame) {
    if (frame.empty()) return false;

    // Convert to grayscale if necessary
    // Convert to grayscale if necessary
    // Use member variable to avoid reallocation
    if (frame.channels() == 3) {
        cv::cvtColor(frame, grayBound_, cv::COLOR_BGR2GRAY);
    } else {
        // If already gray, just reference it? 
        // No, mean() works on const ref.
        // But if we want to store it or process...
        // For mean(), we don't need a copy if it's already gray.
        if (frame.data != grayBound_.data) {
             // If we need a copy, use copyTo. But for just 'mean', we can use input 'frame'.
             // However, to keep logic simple and consistent:
             // grayBound_ = frame; // This is a ref count copy (cheap)
        }
    }

    // Optimization: Calculate mean directly on frame if 1 channel
    cv::Scalar mean_scalar;
    if (frame.channels() == 1) {
        mean_scalar = cv::mean(frame);
    } else {
        mean_scalar = cv::mean(grayBound_);
    }
    double current_brightness = mean_scalar[0];

    if (first_frame_) {
        last_average_brightness_ = current_brightness;
        first_frame_ = false;
        return false;
    }

    double diff = std::abs(current_brightness - last_average_brightness_);
    
    // Update average with a simple running average or just set it
    // For "sudden change", we compare to previous frame.
    // For robust detection, we might want a longer term average, but let's stick to simple "Step 1" requirements.
    last_average_brightness_ = current_brightness;

    if (diff > THRESHOLD_CHANGE) {
        // Brightness jump detected (e.g. black web -> white background or vice versa)
        return true;
    }

    return false;
}
