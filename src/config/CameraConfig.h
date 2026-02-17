#pragma once

#include "../gui/CameraInfo.h"
#include <vector>

/**
 * Centralized camera configuration - single source of truth for all camera information
 */
class CameraConfig {
public:
    // Get full camera information for a specific camera ID (0-based)
    static CameraInfo getCameraInfo(int cameraId);
    
    // Get formatted camera label (e.g., "CAM-01: Headbox")
    static QString getCameraLabel(int cameraId);
    
    // Get short camera name (e.g., "Headbox")
    static QString getCameraName(int cameraId);
    
    // Get total number of configured cameras
    static int getCameraCount();

private:
    // Centralized camera metadata
    static const std::vector<CameraInfo>& getCameraMetadata();
};
