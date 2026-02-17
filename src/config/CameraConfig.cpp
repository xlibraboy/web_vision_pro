#include "CameraConfig.h"
#include <QString>
#include <QStringList>

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
