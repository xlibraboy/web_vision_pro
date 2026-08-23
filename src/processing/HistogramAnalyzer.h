#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <cstdint>

/**
 * HistogramAnalyzer - Computes grayscale pixel intensity histograms.
 *
 * Used to fingerprint event trigger frames: one histogram per camera at the
 * moment the trigger fires, stored as 256-bin uint32 counts in event metadata.
 */
class HistogramAnalyzer {
public:
    static constexpr int kBins = 256;

    struct Histogram {
        std::vector<uint32_t> bins;  // 256 bins, one per intensity level [0..255]
        uint32_t totalPixels = 0;    // sum of all bins (for normalization)
    };

    /// Compute a 256-bin grayscale histogram from a BGR or grayscale frame.
    /// Returns an empty Histogram when the frame is empty.
    static Histogram compute(const cv::Mat& frame);

    /// Convenience: compute and normalize to [0.0 .. 1.0] per-bin fractions.
    static std::vector<double> normalized(const cv::Mat& frame);
};
