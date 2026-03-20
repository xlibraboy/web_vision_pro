#include "CameraConfig.h"
#include <QString>
#include <QStringList>
#include <QSettings>

// --- Configuration Implementation ---

CameraConfig::CameraSource CameraConfig::getCameraSource() {
    QSettings settings("PaperVision", "SystemConfig");
    int val = settings.value("CameraSource", 1).toInt(); // Default 1 (RealCamera)
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

std::vector<CameraInfo> CameraConfig::getCameras() {
    QSettings settings("PaperVision", "SystemConfig");
    int count = settings.beginReadArray("Cameras");
    std::vector<CameraInfo> cameras;
    
    if (count == 0) {
        settings.endArray();
        ensureDefaultCameras();
        return getCameras(); // recursive call after ensuring defaults
    }
    
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        CameraInfo cam;
        cam.id = settings.value("id", i + 1).toInt();
        cam.source = settings.value("source", 1).toInt(); // Default 1 (Real)
        cam.name = settings.value("name", QString("Camera %1").arg(i + 1)).toString();
        cam.location = settings.value("location", "Unknown Location").toString();
        cam.side = settings.value("side", "DRIVE SIDE").toString();
        cam.machinePosition = settings.value("machinePosition", 0).toInt();
        cam.ipAddress = settings.value("ipAddress", QString("172.17.2.%1").arg(i + 1)).toString();
        cam.macAddress = settings.value("macAddress", "").toString();
        cam.subnetMask = settings.value("subnetMask", "255.255.255.0").toString();
        cam.defaultGateway = settings.value("defaultGateway", "0.0.0.0").toString();
        cam.gain = settings.value("gain", 428).toInt();
        cam.exposureTime = settings.value("exposureTime", 541).toInt();
        cam.gamma = settings.value("gamma", 23).toInt();
        cam.contrast = settings.value("contrast", 4).toInt();
        cam.fps = settings.value("fps", 50).toInt();
        cam.temperature = 0.0; // Runtime value
        cameras.push_back(cam);
    }
    settings.endArray();
    return cameras;
}

void CameraConfig::saveCameras(const std::vector<CameraInfo>& cameras) {
    QSettings settings("PaperVision", "SystemConfig");
    settings.beginWriteArray("Cameras", cameras.size());
    for (int i = 0; i < cameras.size(); ++i) {
        settings.setArrayIndex(i);
        const auto& cam = cameras[i];
        settings.setValue("id", cam.id);
        settings.setValue("source", cam.source);
        settings.setValue("name", cam.name);
        settings.setValue("location", cam.location);
        settings.setValue("side", cam.side);
        settings.setValue("machinePosition", cam.machinePosition);
        settings.setValue("ipAddress", cam.ipAddress);
        settings.setValue("macAddress", cam.macAddress);
        settings.setValue("subnetMask", cam.subnetMask);
        settings.setValue("defaultGateway", cam.defaultGateway);
        settings.setValue("gain", cam.gain);
        settings.setValue("exposureTime", cam.exposureTime);
        settings.setValue("gamma", cam.gamma);
        settings.setValue("contrast", cam.contrast);
        settings.setValue("fps", cam.fps);
    }
    settings.endArray();
}

void CameraConfig::ensureDefaultCameras() {
    QSettings settings("PaperVision", "SystemConfig");
    int count = settings.beginReadArray("Cameras");
    settings.endArray();
    
    if (count > 0) return;
    
    std::vector<CameraInfo> defaults = {
        {
            1,  // ID starts from 1
            1,  // source (1 = Real Camera)
            "DRYER 1", // name
            "CYLINDER 13", // location
            "OPERATOR SIDE", // side
            16600, // machinePosition
            "172.17.2.1", // ipAddress
            "", // macAddress
            "255.255.255.0", // subnetMask
            "0.0.0.0", // defaultGateway
            428, 541, 23, 4, 50, 0.0 // defaults
        },
        {
            2,
            1,  // source (1 = Real Camera)
            "DRYER 2",
            "CYLINDER 14",
            "DRIVE SIDE",
            17200,
            "172.17.2.2",
            "",
            "255.255.255.0",
            "0.0.0.0",
            428, 541, 23, 4, 50, 0.0
        }
    };
    saveCameras(defaults);
}

CameraInfo CameraConfig::getCameraInfo(int index) {
    auto cameras = getCameras();
    if (index >= 0 && index < cameras.size()) {
        return cameras[index];
    }
    return CameraInfo{};
}

QString CameraConfig::getCameraLabel(int index) {
    auto cameras = getCameras();
    if (index >= 0 && index < cameras.size()) {
        const CameraInfo& info = cameras[index];
        return QString("CAM-%1: %2").arg(info.id, 2, 10, QChar('0')).arg(info.name);
    }
    return QString("CAM-??: Unknown");
}

QString CameraConfig::getCameraName(int index) {
    auto cameras = getCameras();
    if (index >= 0 && index < cameras.size()) {
        return cameras[index].name;
    }
    return "Unknown";
}

int CameraConfig::getCameraCount() {
    return getCameras().size();
}
