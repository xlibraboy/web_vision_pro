#include "HistogramAnalyzer.h"

HistogramAnalyzer::Histogram HistogramAnalyzer::compute(const cv::Mat& frame) {
    Histogram hist;
    hist.bins.resize(static_cast<size_t>(kBins), 0);

    if (frame.empty()) {
        return hist;
    }

    // Convert to grayscale if needed
    cv::Mat gray;
    if (frame.channels() == 3) {
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    } else if (frame.channels() == 4) {
        cv::cvtColor(frame, gray, cv::COLOR_BGRA2GRAY);
    } else {
        gray = frame;
    }

    // Compute 256-bin histogram [0..255]
    cv::Mat histMat;
    int histSize = kBins;
    float range[] = {0.f, 256.f};
    const float* histRange = {range};
    bool uniform = true;
    bool accumulate = false;
    cv::calcHist(&gray, 1, nullptr, cv::Mat(), histMat, 1, &histSize, &histRange, uniform, accumulate);

    // Copy to vector
    hist.totalPixels = static_cast<uint32_t>(gray.total());
    for (int i = 0; i < kBins; ++i) {
        hist.bins[static_cast<size_t>(i)] = static_cast<uint32_t>(histMat.at<float>(i));
    }

    return hist;
}

std::vector<double> HistogramAnalyzer::normalized(const cv::Mat& frame) {
    Histogram hist = compute(frame);
    std::vector<double> result(static_cast<size_t>(kBins), 0.0);

    if (hist.totalPixels == 0) {
        return result;
    }

    const double inv = 1.0 / static_cast<double>(hist.totalPixels);
    for (int i = 0; i < kBins; ++i) {
        result[static_cast<size_t>(i)] = static_cast<double>(hist.bins[static_cast<size_t>(i)]) * inv;
    }

    return result;
}
