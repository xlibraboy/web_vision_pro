#pragma once

#include <opencv2/opencv.hpp>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <memory>

// Pylon Includes
#include <pylon/PylonIncludes.h>

// Buffer pool for optimized memory management
#include "BufferPool.h"
#include "EventController.h"

// Define callback type for new frames (cameraId, frame)
using FrameCallback = std::function<void(int, const cv::Mat&)>;

class CameraManager {
public:
    CameraManager(int numCameras = 8);
    ~CameraManager();

    // Initialize cameras (Pylon Emulated or Real)
    bool initialize();

    // Start acquisition for all cameras
    void startAcquisition();

    // Stop acquisition
    void stopAcquisition();

    // Register a callback to receive frames
    void registerCallback(FrameCallback callback);
    
    // Get camera labels
    std::vector<std::string> getCameraLabels() const;
    
    // Get Camera Model Name from Pylon
    std::string getModelName(int index) const;
    
    // Get Configured Resolution
    cv::Size getResolution() const;
    
    // Defect Detection Control
    void setDefectDetectionEnabled(bool enabled);
    bool isDefectDetectionEnabled() const;

    // Snapshot Control
    void triggerSnapshot(int cameraIndex);

    // Global Configuration
    void setGlobalFrameRate(double fps);
    void setGlobalResolution(int binningFactor); // 1 = Full, 2 = 2x2, etc.

private:
    // Helper to configure camera parameters (PTP, Packet Size)
    // Helper to configure camera parameters (PTP, Packet Size)
    void configureCamera(GenApi::INodeMap& nodemap, bool isEmulation);
    
    // Vision Pipeline (Blur -> Threshold -> Canny)
    void processFrame(const cv::Mat& input, cv::Mat& output, int cameraIndex);

    // Pylon Acquisition Loop
    void acquisitionLoop();

    // Snapshot Control


    int numCameras_;
    std::atomic<bool> acquiring_; // Threading
    std::thread acquisitionThread_;
    FrameCallback callback_;
    std::mutex callbackMutex_;

    int width_;
    int height_;
    
    // For Multi-Camera Tiled Recording
    std::vector<cv::Mat> latestFrames_;
    std::mutex latestFramesMutex_;
    cv::Mat tiledBuffer_; // Optimization: Reusable buffer for tiling
    
    // Defect detection flag (default: disabled)
    std::atomic<bool> defectDetectionEnabled_;

    // Snapshot Requests (vector of atomics is tricky, using vector of bools protected by mutex for simplicity or fixed array of atomics)
    // Since we have fixed MAX_CAMERAS or dynamic, a mutex protected vector is safer for dynamic resizing.
    std::mutex snapshotMutex_;
    std::vector<bool> snapshotRequests_;
    int fps_;

    // Pylon Objects
    Pylon::CInstantCameraArray cameras_;
    
    std::vector<std::string> cameraLabels_;
    std::vector<std::string> modelNames_;
    
    // Preallocated buffer pools (one per camera)
    std::vector<std::unique_ptr<BufferPool>> bufferPools_;
};
