#include "CameraConfig.h"
#include <QString>
#include <QStringList>
#include <QDir>
#include <QCoreApplication>
#include <QSettings>
#include <cstdlib>

namespace {
QString defaultCameraIp(int id) {
    return QString("172.20.2.%1").arg(id);
}

bool isLegacyCameraIp(const QString& ip, int id) {
    return ip.trimmed() == QString("172.17.2.%1").arg(id);
}

QString defaultEventStoragePath() {
    QDir baseDir(QCoreApplication::applicationDirPath());
    if (baseDir.dirName() == "build") {
        baseDir.cdUp();
    }
    return QDir::cleanPath(baseDir.filePath("data"));
}
std::vector<OpcUaTriggerTagSettings> defaultOpcUaTriggerTags() {
    std::vector<OpcUaTriggerTagSettings> tags(4);
    for (int i = 0; i < static_cast<int>(tags.size()); ++i) {
        tags[i].name = QString("Trigger %1").arg(i + 1);
        tags[i].nodeId = "";
        tags[i].enabled = false;
        tags[i].minimumIntervalMs = 1500;
        tags[i].simulated = false;
        tags[i].group = CameraGroup::kUnassigned;
        tags[i].positionMm = 0;
    }
    return tags;
}

OpcUaSettings defaultOpcUaSettings() {
    OpcUaSettings settings;
    settings.enabled = false;
    settings.endpointUrl = QStringLiteral("opc.tcp://127.0.0.1:4840");
    settings.useUsernamePassword = false;
    settings.publishIntervalMs = 250;
    settings.reconnectIntervalMs = 3000;
    settings.positionDirectionSign = 1;
    settings.triggerTags = defaultOpcUaTriggerTags();
    settings.speedTag.enabled = false;
    settings.speedTag.name = QStringLiteral("Machine Speed");
    settings.speedTag.nodeId = "";
    settings.speedTag.scale = 1.0;
    settings.speedTag.offset = 0.0;
    settings.speedTag.unit = QStringLiteral("m/min");
    settings.speedTag.staleTimeoutMs = 2000;
    settings.speedTag.simulated = false;
    settings.speedTag.simulatedValue = 0.0;
    return settings;
}

}


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

bool CameraConfig::isEmulationActive() {
    return std::getenv("PYLON_CAMEMU") != nullptr
        || getCameraSource() == CameraSource::Emulation;
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

int CameraConfig::getEventRetentionCount() {
    QSettings settings("PaperVision", "SystemConfig");
    return settings.value("EventRetentionCount", 200).toInt();
}

void CameraConfig::setEventRetentionCount(int count) {
    QSettings settings("PaperVision", "SystemConfig");
    settings.setValue("EventRetentionCount", count);
}

int CameraConfig::getLowDiskWarningPct() {
    QSettings settings("PaperVision", "SystemConfig");
    return qBound(1, settings.value("LowDiskWarningPct", 10).toInt(), 99);
}

void CameraConfig::setLowDiskWarningPct(int pct) {
    QSettings settings("PaperVision", "SystemConfig");
    settings.setValue("LowDiskWarningPct", qBound(1, pct, 99));
}

QString CameraConfig::getEventStoragePath() {
    QSettings settings("PaperVision", "SystemConfig");
    return QDir::cleanPath(settings.value("EventStoragePath", defaultEventStoragePath()).toString());
}

QString CameraConfig::getDefaultEventStoragePath() {
    return defaultEventStoragePath();
}

void CameraConfig::setEventStoragePath(const QString& path) {
    QSettings settings("PaperVision", "SystemConfig");
    settings.setValue("EventStoragePath", QDir::cleanPath(path));
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
    return getThemeColors(getThemePreset());
}

ThemeColors CameraConfig::getThemeColors(int themePreset) {
    ThemeColors c;
    switch(themePreset) {
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
        case 7: // Contrast Mono - Black & White
            c.bg = "#111111"; c.border = "#555555"; c.btnBg = "#1A1A1A"; c.btnHover = "#333333";
            c.primary = "#FFFFFF"; c.sliderBg = "#CCCCCC"; c.handle = "#FFFFFF"; c.text = "#FFFFFF";
            break;
        case 0: // Industrial Dark - Cyan
        default:
            c.bg = "#1A1D20"; c.border = "#30363D"; c.btnBg = "#24292E"; c.btnHover = "#30363D";
            c.primary = "#00E5FF"; c.sliderBg = "#0A84FF"; c.handle = "#FFFFFF"; c.text = "#E3E3E3";
            break;
    }
    return c;
}

LiveViewCardStyle CameraConfig::getLiveViewCardStyle() {
    QSettings settings("PaperVision", "SystemConfig");
    LiveViewCardStyle style = getDefaultLiveViewCardStyle();
    const QString legacyFontFamily = settings.value("LiveViewCard/FontFamily", style.gridTitleFontFamily).toString();
    const int legacyFontSize = settings.value("LiveViewCard/FontSize", style.gridTitleFontSize).toInt();
    style.gridTitleFontFamily = settings.value("LiveViewCard/GridTitleFontFamily", legacyFontFamily).toString();
    style.gridTitleFontSize = settings.value("LiveViewCard/GridTitleFontSize", legacyFontSize).toInt();
    style.detailTitleFontFamily = settings.value("LiveViewCard/DetailTitleFontFamily", style.gridTitleFontFamily).toString();
    style.detailTitleFontSize = settings.value("LiveViewCard/DetailTitleFontSize", style.gridTitleFontSize).toInt();
    style.detailSectionFontFamily = settings.value("LiveViewCard/DetailSectionFontFamily", style.detailTitleFontFamily).toString();
    style.detailSectionFontSize = settings.value("LiveViewCard/DetailSectionFontSize", style.detailSectionFontSize).toInt();
    style.backgroundStyle = settings.value("LiveViewCard/BackgroundStyle", style.backgroundStyle).toString();
    return style;
}

LiveViewCardStyle CameraConfig::getDefaultLiveViewCardStyle() {
    return {
        QStringLiteral("Noto Sans"),
        14,
        QStringLiteral("Noto Sans"),
        14,
        QStringLiteral("Noto Sans"),
        13,
        QStringLiteral("black")
    };
}

void CameraConfig::setLiveViewCardStyle(const LiveViewCardStyle& style) {
    QSettings settings("PaperVision", "SystemConfig");
    settings.setValue("LiveViewCard/GridTitleFontFamily", style.gridTitleFontFamily);
    settings.setValue("LiveViewCard/GridTitleFontSize", style.gridTitleFontSize);
    settings.setValue("LiveViewCard/DetailTitleFontFamily", style.detailTitleFontFamily);
    settings.setValue("LiveViewCard/DetailTitleFontSize", style.detailTitleFontSize);
    settings.setValue("LiveViewCard/DetailSectionFontFamily", style.detailSectionFontFamily);
    settings.setValue("LiveViewCard/DetailSectionFontSize", style.detailSectionFontSize);
    settings.setValue("LiveViewCard/BackgroundStyle", style.backgroundStyle);
}

AnalysisViewStyle CameraConfig::getAnalysisViewStyle() {
    QSettings settings("PaperVision", "SystemConfig");
    AnalysisViewStyle style = getDefaultAnalysisViewStyle();
    style.videoTitleFontFamily = settings.value("AnalysisView/VideoTitleFontFamily", style.videoTitleFontFamily).toString();
    style.videoTitleFontSize = settings.value("AnalysisView/VideoTitleFontSize", style.videoTitleFontSize).toInt();
    style.timestampFontFamily = settings.value("AnalysisView/TimestampFontFamily", style.timestampFontFamily).toString();
    style.timestampFontSize = settings.value("AnalysisView/TimestampFontSize", style.timestampFontSize).toInt();
    style.tabFontFamily = settings.value("AnalysisView/TabFontFamily", style.tabFontFamily).toString();
    style.tabFontSize = settings.value("AnalysisView/TabFontSize", style.tabFontSize).toInt();
    style.playbackSurfaceStyle = settings.value("AnalysisView/PlaybackSurfaceStyle", style.playbackSurfaceStyle).toString();
    return style;
}

AnalysisViewStyle CameraConfig::getDefaultAnalysisViewStyle() {
    return {
        QStringLiteral("Noto Sans"),
        10,
        QStringLiteral("Consolas"),
        8,
        QStringLiteral("Noto Sans"),
        12,
        QStringLiteral("dark")
    };
}

void CameraConfig::setAnalysisViewStyle(const AnalysisViewStyle& style) {
    QSettings settings("PaperVision", "SystemConfig");
    settings.setValue("AnalysisView/VideoTitleFontFamily", style.videoTitleFontFamily);
    settings.setValue("AnalysisView/VideoTitleFontSize", style.videoTitleFontSize);
    settings.setValue("AnalysisView/TimestampFontFamily", style.timestampFontFamily);
    settings.setValue("AnalysisView/TimestampFontSize", style.timestampFontSize);
    settings.setValue("AnalysisView/TabFontFamily", style.tabFontFamily);
    settings.setValue("AnalysisView/TabFontSize", style.tabFontSize);
    settings.setValue("AnalysisView/PlaybackSurfaceStyle", style.playbackSurfaceStyle);
}
OpcUaSettings CameraConfig::getDefaultOpcUaSettings() {
    return defaultOpcUaSettings();
}

OpcUaSettings CameraConfig::getOpcUaSettings() {
    const OpcUaSettings defaults = getDefaultOpcUaSettings();
    OpcUaSettings result = defaults;

    QSettings settings("PaperVision", "SystemConfig");
    result.enabled = settings.value("OpcUa/Enabled", defaults.enabled).toBool();
    result.endpointUrl = settings.value("OpcUa/EndpointUrl", defaults.endpointUrl).toString().trimmed();
    result.useUsernamePassword = settings.value("OpcUa/UseUsernamePassword", defaults.useUsernamePassword).toBool();
    result.username = settings.value("OpcUa/Username", defaults.username).toString();
    result.password = settings.value("OpcUa/Password", defaults.password).toString();
    result.publishIntervalMs = settings.value("OpcUa/PublishIntervalMs", defaults.publishIntervalMs).toInt();
    result.reconnectIntervalMs = settings.value("OpcUa/ReconnectIntervalMs", defaults.reconnectIntervalMs).toInt();
    result.positionDirectionSign = settings.value("OpcUa/PositionDirectionSign", defaults.positionDirectionSign).toInt();
    if (result.positionDirectionSign >= 0) {
        result.positionDirectionSign = 1;
    } else {
        result.positionDirectionSign = -1;
    }

    const int triggerCount = settings.beginReadArray("OpcUa/TriggerTags");
    if (triggerCount > 0) {
        result.triggerTags.clear();
        result.triggerTags.reserve(triggerCount);
        for (int i = 0; i < triggerCount; ++i) {
            settings.setArrayIndex(i);
            OpcUaTriggerTagSettings tag = (i < static_cast<int>(defaults.triggerTags.size()))
                ? defaults.triggerTags[static_cast<size_t>(i)]
                : OpcUaTriggerTagSettings{};
            tag.name = settings.value("name", tag.name).toString();
            tag.nodeId = settings.value("nodeId", tag.nodeId).toString().trimmed();
            tag.enabled = settings.value("enabled", tag.enabled).toBool();
            tag.minimumIntervalMs = settings.value("minimumIntervalMs", tag.minimumIntervalMs).toInt();
            tag.simulated = settings.value("simulated", tag.simulated).toBool();
            tag.group = settings.value("group", tag.group).toInt();
            tag.positionMm = settings.value("positionMm", tag.positionMm).toInt();
            result.triggerTags.push_back(tag);
        }
    }
    settings.endArray();

    if (result.triggerTags.empty()) {
        result.triggerTags = defaults.triggerTags;
    }

    result.speedTag.enabled = settings.value("OpcUa/SpeedTag/Enabled", defaults.speedTag.enabled).toBool();
    result.speedTag.name = settings.value("OpcUa/SpeedTag/Name", defaults.speedTag.name).toString();
    result.speedTag.nodeId = settings.value("OpcUa/SpeedTag/NodeId", defaults.speedTag.nodeId).toString().trimmed();
    result.speedTag.scale = settings.value("OpcUa/SpeedTag/Scale", defaults.speedTag.scale).toDouble();
    result.speedTag.offset = settings.value("OpcUa/SpeedTag/Offset", defaults.speedTag.offset).toDouble();
    result.speedTag.unit = settings.value("OpcUa/SpeedTag/Unit", defaults.speedTag.unit).toString().trimmed();
    result.speedTag.staleTimeoutMs = settings.value("OpcUa/SpeedTag/StaleTimeoutMs", defaults.speedTag.staleTimeoutMs).toInt();
    result.speedTag.simulated = settings.value("OpcUa/SpeedTag/Simulated", defaults.speedTag.simulated).toBool();
    result.speedTag.simulatedValue = settings.value("OpcUa/SpeedTag/SimulatedValue", defaults.speedTag.simulatedValue).toDouble();
    if (result.speedTag.unit.isEmpty()) {
        result.speedTag.unit = defaults.speedTag.unit;
    }

    return result;
}

void CameraConfig::setOpcUaSettings(const OpcUaSettings& opcUaSettings) {
    const OpcUaSettings defaults = getDefaultOpcUaSettings();
    QSettings settings("PaperVision", "SystemConfig");
    settings.setValue("OpcUa/Enabled", opcUaSettings.enabled);
    settings.setValue("OpcUa/EndpointUrl", opcUaSettings.endpointUrl.trimmed());
    settings.setValue("OpcUa/UseUsernamePassword", opcUaSettings.useUsernamePassword);
    settings.setValue("OpcUa/Username", opcUaSettings.username);
    settings.setValue("OpcUa/Password", opcUaSettings.password);
    settings.setValue("OpcUa/PublishIntervalMs", opcUaSettings.publishIntervalMs);
    settings.setValue("OpcUa/ReconnectIntervalMs", opcUaSettings.reconnectIntervalMs);
    settings.setValue("OpcUa/PositionDirectionSign", opcUaSettings.positionDirectionSign >= 0 ? 1 : -1);

    settings.beginWriteArray("OpcUa/TriggerTags", static_cast<int>(opcUaSettings.triggerTags.size()));
    for (int i = 0; i < static_cast<int>(opcUaSettings.triggerTags.size()); ++i) {
        settings.setArrayIndex(i);
        const OpcUaTriggerTagSettings& tag = opcUaSettings.triggerTags[static_cast<size_t>(i)];
        settings.setValue("name", tag.name);
        settings.setValue("nodeId", tag.nodeId.trimmed());
        settings.setValue("enabled", tag.enabled);
        settings.setValue("minimumIntervalMs", tag.minimumIntervalMs);
        settings.setValue("simulated", tag.simulated);
        settings.setValue("group", tag.group);
        settings.setValue("positionMm", tag.positionMm);
    }
    settings.endArray();

    settings.setValue("OpcUa/SpeedTag/Enabled", opcUaSettings.speedTag.enabled);
    settings.setValue("OpcUa/SpeedTag/Name", opcUaSettings.speedTag.name);
    settings.setValue("OpcUa/SpeedTag/NodeId", opcUaSettings.speedTag.nodeId.trimmed());
    settings.setValue("OpcUa/SpeedTag/Scale", opcUaSettings.speedTag.scale);
    settings.setValue("OpcUa/SpeedTag/Offset", opcUaSettings.speedTag.offset);
    settings.setValue("OpcUa/SpeedTag/Unit", opcUaSettings.speedTag.unit.trimmed().isEmpty()
        ? defaults.speedTag.unit
        : opcUaSettings.speedTag.unit.trimmed());
    settings.setValue("OpcUa/SpeedTag/StaleTimeoutMs", opcUaSettings.speedTag.staleTimeoutMs);
    settings.setValue("OpcUa/SpeedTag/Simulated", opcUaSettings.speedTag.simulated);
    settings.setValue("OpcUa/SpeedTag/SimulatedValue", opcUaSettings.speedTag.simulatedValue);
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
        cam.ipAddress = settings.value("ipAddress", defaultCameraIp(i + 1)).toString();
        if (cam.ipAddress.isEmpty() || isLegacyCameraIp(cam.ipAddress, cam.id)) {
            cam.ipAddress = defaultCameraIp(cam.id);
        }
        cam.macAddress = settings.value("macAddress", "").toString();
        cam.subnetMask = settings.value("subnetMask", "255.255.255.0").toString();
        cam.defaultGateway = settings.value("defaultGateway", "0.0.0.0").toString();
        cam.fps = settings.value("fps", 50).toInt();
        cam.enableAcquisitionFps = settings.value("enableAcquisitionFps", false).toBool();
        cam.width = settings.value("width", 780).toInt();
        cam.height = settings.value("height", 580).toInt();
        cam.offsetX = settings.value("offsetX", 0).toInt();
        cam.offsetY = settings.value("offsetY", 0).toInt();
        cam.pixelFormat = settings.value("pixelFormat", "Mono8").toString();
        cam.exposureTimeAbs = settings.value("exposureTimeAbs", 5000.0).toDouble();
        cam.enableExposureTimeBase = settings.value("enableExposureTimeBase", false).toBool();
        cam.exposureTimeBaseAbs = settings.value("exposureTimeBaseAbs", 20.0).toDouble();
        cam.exposureTimeRaw = settings.value("exposureTimeRaw", 2044).toInt();
        cam.chunkModeActive = settings.value("chunkModeActive", false).toBool();
        cam.enabledChunks = settings.value("enabledChunks").toStringList();
        cam.temperature = 0.0; // Runtime value
        cam.group = settings.value("group", cam.group).toInt();
        cam.floor = settings.value("floor", cam.floor).toInt();
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
        settings.setValue("fps", cam.fps);
        settings.setValue("enableAcquisitionFps", cam.enableAcquisitionFps);
        settings.setValue("width", cam.width);
        settings.setValue("height", cam.height);
        settings.setValue("offsetX", cam.offsetX);
        settings.setValue("offsetY", cam.offsetY);
        settings.setValue("pixelFormat", cam.pixelFormat);
        settings.setValue("exposureTimeAbs", cam.exposureTimeAbs);
        settings.setValue("enableExposureTimeBase", cam.enableExposureTimeBase);
        settings.setValue("exposureTimeBaseAbs", cam.exposureTimeBaseAbs);
        settings.setValue("exposureTimeRaw", cam.exposureTimeRaw);
        settings.setValue("chunkModeActive", cam.chunkModeActive);
        settings.setValue("enabledChunks", cam.enabledChunks);
        settings.setValue("group", cam.group);
        settings.setValue("floor", cam.floor);
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
            "172.20.2.1", // ipAddress
            "", // macAddress
             "255.255.255.0", // subnetMask
             "0.0.0.0", // defaultGateway
             50, // fps
             false, // enableAcquisitionFps
             780, // width
             580, // height
             0, // offsetX
             0, // offsetY
             "Mono8", // pixelFormat
             5000.0, // exposureTimeAbs
             false, // enableExposureTimeBase
             20.0, // exposureTimeBaseAbs
             2044, // exposureTimeRaw
             false, // chunkModeActive
             {}, // enabledChunks
             0.0 // temperature
         },
         {
             2,
             1,  // source (1 = Real Camera)
            "DRYER 2",
            "CYLINDER 14",
            "DRIVE SIDE",
            17200,
            "172.20.2.2",
            "",
             "255.255.255.0",
             "0.0.0.0",
             50, // fps
             false, // enableAcquisitionFps
             780, // width
             580, // height
             0, // offsetX
             0, // offsetY
             "Mono8", // pixelFormat
             5000.0, // exposureTimeAbs
             false, // enableExposureTimeBase
             20.0, // exposureTimeBaseAbs
             2044, // exposureTimeRaw
             false, // chunkModeActive
             {}, // enabledChunks
             0.0 // temperature
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
