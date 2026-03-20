#pragma once

#include "../gui/CameraInfo.h"
#include <vector>
#include <QString>

/**
 * Theme color tokens for the current UI preset.
 * All widgets should use these instead of hardcoded hex values.
 */
struct ThemeColors {
    QString bg;        // Main window background
    QString border;    // Borders and dividers
    QString btnBg;     // Button / panel background
    QString btnHover;  // Button hover state
    QString primary;   // Accent / primary highlight color
    QString sliderBg;  // Slider filled portion
    QString handle;    // Slider handle color
    QString text;      // Primary text color
};

/**
 * Centralized camera configuration - single source of truth for all camera information
 */
class CameraConfig {
public:
    // Get full camera information for a specific camera index (0-based array index)
    static CameraInfo getCameraInfo(int index);
    
    // Get formatted camera label (e.g., "CAM-01: DRYER 1")
    static QString getCameraLabel(int index);
    
    // Get short camera name (e.g., "DRYER 1")
    static QString getCameraName(int index);
    
    // Get total number of configured cameras
    static int getCameraCount();

    // Get all configured cameras
    static std::vector<CameraInfo> getCameras();
    
    // Save configured cameras
    static void saveCameras(const std::vector<CameraInfo>& cameras);

    // Configuration Enums
    enum class CameraSource {
        Emulation,
        RealCamera
    };

    // --- Configuration Getters/Setters (Persistent) ---
    
    // Camera Source
    static CameraSource getCameraSource();
    static void setCameraSource(CameraSource source);
    
    // Global FPS
    static int getFps();
    static void setFps(int fps);
    
    // Post-Trigger Duration (Seconds)
    static int getPostTriggerSeconds();
    static void setPostTriggerSeconds(int seconds);
    
    // Pre-Trigger Duration (Seconds)
    static int getPreTriggerSeconds();
    static void setPreTriggerSeconds(int seconds);

    // Defect Detection
    static bool isDefectDetectionEnabled();
    static void setDefectDetectionEnabled(bool enabled);

    // UI Theme
    static int getThemePreset();
    static void setThemePreset(int themeIndex);

    // Returns the full set of color tokens for the current theme preset.
    static ThemeColors getThemeColors();

    // Initialize default cameras if empty
    static void ensureDefaultCameras();
};
