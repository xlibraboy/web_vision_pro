#include "CameraConfig.h"
#include <QString>
#include <QStringList>
#include <QSettings>

// --- Configuration Implementation ---

CameraConfig::CameraSource CameraConfig::getCameraSource() {
    QSettings settings("PaperVision", "SystemConfig");
    int val = settings.value("CameraSource", 0).toInt(); // Default 0 (Emulation)
    return static_cast<CameraSource>(val);
}

void CameraConfig::setCameraSource(CameraSource source) {
    QSettings settings("PaperVision", "SystemConfig");
    settings.setValue("CameraSource", static_cast<int>(source));
}

int CameraConfig::getFps() {
    QSettings settings("PaperVision", "SystemConfig");
    return settings.value("Fps", 10).toInt();
}

void CameraConfig::setFps(int fps) {
    QSettings settings("PaperVision", "SystemConfig");
    settings.setValue("Fps", fps);
}

int CameraConfig::getPostTriggerSeconds() {
    QSettings settings("PaperVision", "SystemConfig");
    return settings.value("PostTriggerSeconds", 5).toInt(); // Default 5s
}

void CameraConfig::setPostTriggerSeconds(int seconds) {
    QSettings settings("PaperVision", "SystemConfig");
    settings.setValue("PostTriggerSeconds", seconds);
}

int CameraConfig::getPreTriggerSeconds() {
    QSettings settings("PaperVision", "SystemConfig");
    return settings.value("PreTriggerSeconds", 10).toInt(); // Default 10s
}

void CameraConfig::setPreTriggerSeconds(int seconds) {
    QSettings settings("PaperVision", "SystemConfig");
    settings.setValue("PreTriggerSeconds", seconds);
}

bool CameraConfig::isDefectDetectionEnabled() {
    QSettings settings("PaperVision", "SystemConfig");
    return settings.value("DefectDetection", false).toBool();
}

void CameraConfig::setDefectDetectionEnabled(bool enabled) {
    QSettings settings("PaperVision", "SystemConfig");
    settings.setValue("DefectDetection", enabled);
}

int CameraConfig::getThemePreset() {
    QSettings settings("PaperVision", "SystemConfig");
    return settings.value("ThemePreset", 0).toInt(); // Default 0
}

void CameraConfig::setThemePreset(int themeIndex) {
    QSettings settings("PaperVision", "SystemConfig");
    settings.setValue("ThemePreset", themeIndex);
}

ThemeColors CameraConfig::getThemeColors() {
    ThemeColors c;
    switch(getThemePreset()) {
        case 1: // Classic Dark - Blue
            c.bg = "#1A1D20"; c.border = "#30363D"; c.btnBg = "#24292E"; c.btnHover = "#30363D";
            c.primary = "#0078D4"; c.sliderBg = "#0A84FF"; c.handle = "#FFFFFF"; c.text = "#E3E3E3";
            break;
        case 2: // High Contrast - Orange
            c.bg = "#121212"; c.border = "#333333"; c.btnBg = "#1E1E1E"; c.btnHover = "#2D2D2D";
            c.primary = "#FF9900"; c.sliderBg = "#FF9900"; c.handle = "#FFFFFF"; c.text = "#FFFFFF";
            break;
        case 3: // Warning State - Yellow
            c.bg = "#1A1A1A"; c.border = "#403D00"; c.btnBg = "#2B2A20"; c.btnHover = "#3D3A20";
            c.primary = "#FFD700"; c.sliderBg = "#FFCC00"; c.handle = "#000000"; c.text = "#FFD700";
            break;
        case 4: // Precision - Green
            c.bg = "#101815"; c.border = "#203028"; c.btnBg = "#1A2620"; c.btnHover = "#283C32";
            c.primary = "#00FF66"; c.sliderBg = "#00CC44"; c.handle = "#FFFFFF"; c.text = "#E8F0EA";
            break;
        case 5: // Visionary - Purple
            c.bg = "#161020"; c.border = "#271E38"; c.btnBg = "#1D152C"; c.btnHover = "#2E2142";
            c.primary = "#9D00FF"; c.sliderBg = "#B233FF"; c.handle = "#FFFFFF"; c.text = "#E6D9F2";
            break;
        case 6: // Alert - Deep Red
            c.bg = "#1A0F0F"; c.border = "#331818"; c.btnBg = "#241313"; c.btnHover = "#3D1F1F";
            c.primary = "#FF2A2A"; c.sliderBg = "#FF4040"; c.handle = "#FFFFFF"; c.text = "#F2E6E6";
            break;
        case 0: // Industrial Dark - Cyan
        default:
            c.bg = "#1A1D20"; c.border = "#30363D"; c.btnBg = "#24292E"; c.btnHover = "#30363D";
            c.primary = "#00E5FF"; c.sliderBg = "#0A84FF"; c.handle = "#FFFFFF"; c.text = "#E3E3E3";
            break;
    }
    return c;
}

const std::vector<CameraInfo>& CameraConfig::getCameraMetadata() {
    static std::vector<CameraInfo> cameraMetadata = {
        {
            1,  // ID starts from 01
            "Paper Machine #1",
            "OS",  // Operator Side
            "Headbox monitoring",
            "Basler acA1920-40gm",
            "Not Connected / Simulated",  // IP for emulated camera
            "640 x 480",
            55.0,  // FPS
            42.5   // Temperature
        },
        {
            2,  // ID 02
            "Paper Machine #1",
            "DS",  // Drive Side
            "Dryer section monitoring",
            "Basler acA1920-40gm",
            "Not Connected / Simulated",
            "640 x 480",
            55.0,
            43.2
        }
    };
    return cameraMetadata;
}

CameraInfo CameraConfig::getCameraInfo(int cameraId) {
    const auto& metadata = getCameraMetadata();
    if (cameraId >= 0 && cameraId < static_cast<int>(metadata.size())) {
        return metadata[cameraId];
    }
    // Return first camera as default if ID is out of range
    return metadata.empty() ? CameraInfo{} : metadata[0];
}

QString CameraConfig::getCameraLabel(int cameraId) {
    const auto& metadata = getCameraMetadata();
    QString label = QString("CAM-%1").arg(cameraId + 1, 2, 10, QChar('0'));
    
    if (cameraId >= 0 && cameraId < static_cast<int>(metadata.size())) {
        label += ": " + metadata[cameraId].description.split(" ").first();  // First word of description
    }
    
    return label;
}

QString CameraConfig::getCameraName(int cameraId) {
    const auto& metadata = getCameraMetadata();
    if (cameraId >= 0 && cameraId < static_cast<int>(metadata.size())) {
        // Return first word of description as short name
        return metadata[cameraId].description.split(" ").first();
    }
    return QString();
}

int CameraConfig::getCameraCount() {
    return static_cast<int>(getCameraMetadata().size());
}
