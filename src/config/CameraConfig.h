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

struct LiveViewCardStyle {
    QString gridTitleFontFamily;
    int gridTitleFontSize;
    QString detailTitleFontFamily;
    int detailTitleFontSize;
    QString detailSectionFontFamily;
    int detailSectionFontSize;
    QString backgroundStyle;
};

struct AnalysisViewStyle {
    QString videoTitleFontFamily;
    int videoTitleFontSize;
    QString timestampFontFamily;
    int timestampFontSize;
    QString tabFontFamily;
    int tabFontSize;
    QString playbackSurfaceStyle;
};
struct OpcUaTriggerTagSettings {
    QString name;
    QString nodeId;
    bool enabled = false;
    // Push-hold repeat interval: while the trigger is held active (by the
    // manual button, or a Live tag reading True), a trigger fires every
    // minimumIntervalMs (0 = as fast as possible).
    int minimumIntervalMs = 1500;
    // When true the tag is not subscribed from the OPC UA server; the trigger
    // is driven only by the manual push-hold button.
    bool simulated = false;
};

struct OpcUaSpeedTagSettings {
    bool enabled = false;
    QString name;
    QString nodeId;
    double scale = 1.0;
    double offset = 0.0;
    QString unit = QStringLiteral("m/min");
    int staleTimeoutMs = 2000;
    // When true the tag is not subscribed from the OPC UA server; it instead
    // reports a fixed simulated raw value (before Scale/Offset are applied).
    bool simulated = false;
    double simulatedValue = 0.0;
};

struct OpcUaSettings {
    bool enabled = false;
    QString endpointUrl = QStringLiteral("opc.tcp://127.0.0.1:4840");
    bool useUsernamePassword = false;
    QString username;
    QString password;
    int publishIntervalMs = 250;
    int reconnectIntervalMs = 3000;
    int positionDirectionSign = 1;
    std::vector<OpcUaTriggerTagSettings> triggerTags;
    OpcUaSpeedTagSettings speedTag;
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

    // Max number of non-permanent paper break records to keep.
    static int getEventRetentionCount();
    static void setEventRetentionCount(int count);

    // Root folder used for event save/load operations.
    static QString getDefaultEventStoragePath();
    static QString getEventStoragePath();
    static void setEventStoragePath(const QString& path);

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
    static ThemeColors getThemeColors(int themePreset);

    // Live View camera tile typography/background settings.
    static LiveViewCardStyle getDefaultLiveViewCardStyle();
    static LiveViewCardStyle getLiveViewCardStyle();
    static void setLiveViewCardStyle(const LiveViewCardStyle& style);

    // Analysis View typography/playback surface settings.
    static AnalysisViewStyle getDefaultAnalysisViewStyle();
    static AnalysisViewStyle getAnalysisViewStyle();
    static void setAnalysisViewStyle(const AnalysisViewStyle& style);
    // OPC UA client trigger/speed integration settings.
    static OpcUaSettings getDefaultOpcUaSettings();
    static OpcUaSettings getOpcUaSettings();
    static void setOpcUaSettings(const OpcUaSettings& settings);

    // Initialize default cameras if empty
    static void ensureDefaultCameras();
};
