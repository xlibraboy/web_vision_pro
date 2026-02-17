#include "VideoEncoder.h"
#include <thread>
#include <iostream>

VideoEncoder::VideoEncoder() {}

VideoEncoder::~VideoEncoder() {}

void VideoEncoder::saveVideoAsync(const std::vector<cv::Mat>& frames, const std::string& filename, int fps) {
    // Launch a detached thread or use std::async to write video
    std::thread([this, frames, filename, fps]() {
        this->encodeLoop(frames, filename, fps);
    }).detach();
}

void VideoEncoder::encodeLoop(std::vector<cv::Mat> frames, std::string filename, int fps) {
    if (frames.empty()) return;

    cv::Size frameSize = frames[0].size();
    // Using MJPG for simplicity and speed. In production, might use H264/HEVC with hardware accel.
    cv::VideoWriter writer(filename, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, frameSize, true);

    if (!writer.isOpened()) {
        std::cerr << "Error: Could not open video writer for " << filename << std::endl;
        return;
    }

    std::cout << "Saving video to " << filename << " (" << frames.size() << " frames)..." << std::endl;
    for (const auto& frame : frames) {
        writer.write(frame);
    }
    std::cout << "Finished saving " << filename << std::endl;
    writer.release();
}
