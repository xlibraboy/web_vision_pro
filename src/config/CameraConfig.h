#pragma once

#include "../gui/CameraInfo.h"
#include <vector>
#include <QString>

/**
 * Theme color tokens for the current UI theme.
 * All widgets should use these instead of hardcoded hex values.
 *
 * Roles:
 *   bg/surface -> canvas, raised panels, cards, chart areas
 *   text/muted/disabled -> primary, secondary, disabled foregrounds
 *   border/btnBg/btnHover/handle/sliderBg -> chrome and controls
 *   primary/focus -> accent and keyboard focus
 *   ok/warn/critical/info -> semantic status; they carry text on theme
 *     backgrounds too, so each theme ships its own variants (the contrast suite
 *     checks every pair on both themes)
 *   onStatus -> ink for text drawn on a solid status fill
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
    QString surface;   // Raised panel / card / table-header / chart background
    QString muted;     // Secondary text: timestamps, hints, meta, captions
    QString disabled;  // Disabled foreground (labels, controls)
    QString focus;     // Keyboard focus ring / focused border
    QString ok;        // Success / healthy / online / running
    QString warn;      // Caution / degraded / stale / staged
    QString critical;  // Error / fault / danger / destructive
    QString info;      // Informational / emulated / simulated
    QString onStatus;  // Text drawn on a solid ok/warn/critical/info fill

    // True for the dark themes. Directions that are not a token (e.g. a "lift"
    // above a surface, or which way muted ink has to move) depend on this.
    bool isDark() const;
};

/**
 * Ink drawn straight onto video - camera tiles and playback frames - which get
 * their surface from LiveViewCardStyle / AnalysisViewStyle, not from the theme.
 * These deliberately do not follow ThemeMode: the surface's own polarity
 * decides which set reads, whatever the chrome is using.
 */
struct OverlayColors {
    QString ink;       // Primary overlay text
    QString muted;     // Secondary overlay text
    QString accent;    // Accent strokes and highlights drawn on video
    QString ok;
    QString warn;
    QString critical;
    QString info;
};

/**
 * Which theme the operator picked. ThemeMode::System follows the desktop colour
 * scheme (see SystemColorScheme). The values are persisted, so they are part of
 * the settings format.
 */
enum class ThemeMode {
    System = 0,
    Light = 1,
    Dark = 2
};

struct LiveViewCardStyle {
    QString gridTitleFontFamily;
    int gridTitleFontSize;
    QString detailTitleFontFamily;
    int detailTitleFontSize;
    QString detailSectionFontFamily;
    int detailSectionFontSize;
    // "auto" follows the theme's polarity; "black" / "white" and the textured
    // variants pin the surface whatever the theme does (see
    // resolveLiveViewBackgroundStyle).
    QString backgroundStyle;
};

struct AnalysisViewStyle {
    QString videoTitleFontFamily;
    int videoTitleFontSize;
    QString timestampFontFamily;
    int timestampFontSize;
    QString tabFontFamily;
    int tabFontSize;
    // "auto" follows the theme's polarity, "dark" / "light" pin it (see
    // resolvePlaybackSurfaceStyle).
    QString playbackSurfaceStyle;
    // Default metadata overlay mode shown on analysis frames. Values match the
    // AnalysisView metadata combo data: none, standard, full, timestamp,
    // framecounter, realtime, relative.
    QString defaultMetadataMode;
    // Typography for the metadata side of the analysis HUD overlay.
    QString defaultMetadataFontFamily;
    int defaultMetadataFontSize;
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
    // Camera group this trigger belongs to: a CameraGroup::k* constant, or
    // CameraGroup::kUnassigned (-1). Labels the event (EventInfo.triggerGroup);
    // which cameras record is decided by recordGroups below.
    int group = CameraGroup::kUnassigned;
    // Sections this trigger records (CameraGroup::k* values, ascending). Empty
    // or all CameraGroup::kCount sections = no filtering: every active camera
    // records, including cameras whose group is still Unassigned. A subset
    // records only cameras assigned to those sections.
    std::vector<int> recordGroups;
    // Machine position (mm) of the trigger sensor. 0 = spatial alignment
    // disabled: every camera records the same wall-clock window.
    int positionMm = 0;
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
    // Machine position (mm) where this drive's actual speed applies (e.g. the
    // sheet's speed at the drive the tag reports). Defect sync interpolates the
    // local speed between anchors, so a paper machine's draw (per-group speed
    // differences) is reflected in the frame->mm projection. 0 = position
    // unknown: the tag acts as a global speed (the legacy single-tag behavior).
    int positionMm = 0;
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
    // Machine speed anchors: one or more speed tags, each reporting the actual
    // local speed (m/min) at its machine position (drive). All are snapshotted
    // onto an event at trigger time; the defect projector linearly interpolates
    // the local speed between anchors for every frame->mm conversion.
    std::vector<OpcUaSpeedTagSettings> speedTags;
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

    // True when emulation is active: the PYLON_CAMEMU env var (docker/CI) is
    // set OR the persisted Camera Mode selector is Emulated. Used to gate the
    // per-card "Emulated" Source setting and to label Real cards that cannot
    // attach because no real hardware is present.
    static bool isEmulationActive();
    
    // Global FPS
    static int getFps();
    static void setFps(int fps);
    
    // Post-Trigger Duration (Seconds)
    static int getPostTriggerSeconds();
    static void setPostTriggerSeconds(int seconds);

    // Max number of non-permanent paper break records to keep.
    static int getEventRetentionCount();
    static void setEventRetentionCount(int count);

    // Number of most recent non-permanent records to keep when the user
    // triggers an "instant clear" from the Paper Break Log sidebar.
    static int getInstantClearKeepCount();
    static void setInstantClearKeepCount(int count);

    // Low-disk warning threshold: free space below this %% of volume is amber.
    static int getLowDiskWarningPct();
    static void setLowDiskWarningPct(int pct);

    // Root folder used for event save/load operations.
    static QString getDefaultEventStoragePath();
    static QString getEventStoragePath();
    static void setEventStoragePath(const QString& path);

    // Pre-Trigger Duration (Seconds)
    static int getPreTriggerSeconds();
    static void setPreTriggerSeconds(int seconds);

    // UI theme. An installation that saved one of the retired colour presets
    // ("ThemePreset") resolves to Dark: every one of them was a dark theme.
    static ThemeMode getThemeMode();
    static void setThemeMode(ThemeMode mode);
    // Display name: "Light", "Dark", "Follow system".
    static QString themeModeName(ThemeMode mode);
    // ThemeMode::System resolved to the concrete Light/Dark theme in force.
    static ThemeMode resolveThemeMode(ThemeMode mode);

    // Live preview of a mode the operator has picked but not saved yet (the
    // Theme dropdown). While a preview is set every getThemeColors() caller -
    // including widgets that resolve colors at paint time - shows the candidate
    // theme; saving or abandoning clears it (see clearThemePreview).
    static void setThemePreview(ThemeMode mode);
    static void clearThemePreview();
    static int getThemePreview(); // -1 when no preview is active

    // The theme mode in force: the preview when one is active, otherwise the
    // saved mode. Still possibly ThemeMode::System.
    static ThemeMode effectiveThemeMode();

    // Returns the full set of color tokens for the effective theme: the preview
    // mode when one is active, otherwise the saved mode.
    static ThemeColors getThemeColors();
    static ThemeColors getThemeColors(ThemeMode mode);

    // Ink painted onto video: the bright set for a dark surface (the default),
    // the daylight set for a light one.
    static OverlayColors getVideoOverlayColors(bool lightSurface = false);

    // Live View camera tile typography/background settings.
    static LiveViewCardStyle getDefaultLiveViewCardStyle();
    static LiveViewCardStyle getLiveViewCardStyle();
    static void setLiveViewCardStyle(const LiveViewCardStyle& style);

    // Analysis View typography/playback surface settings.
    static AnalysisViewStyle getDefaultAnalysisViewStyle();
    static AnalysisViewStyle getAnalysisViewStyle();
    static void setAnalysisViewStyle(const AnalysisViewStyle& style);

    // The video surfaces default to "auto", the plain surface that matches the
    // theme's polarity (so a light theme gets light tiles, not a black hole in
    // the middle of the window). These turn a stored style into the one that is
    // painted: "auto" resolves against the theme in force - including a preview
    // - and an explicit style passes through unchanged.
    static QString resolveLiveViewBackgroundStyle(const QString& storedStyle);
    static QString resolvePlaybackSurfaceStyle(const QString& storedStyle);
    // OPC UA client trigger/speed integration settings.
    static OpcUaSettings getDefaultOpcUaSettings();
    static OpcUaSettings getOpcUaSettings();
    static void setOpcUaSettings(const OpcUaSettings& settings);

    // Initialize default cameras if empty
    static void ensureDefaultCameras();
};
