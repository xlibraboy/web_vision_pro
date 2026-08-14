#pragma once

#include <QString>
#include <QStringList>

// Fixed paper-machine camera groups. Cameras are assigned to one of these on
// their Camera Card; a trigger wired to a group records only that group's
// cameras (group -1 in a TriggerContext means "all cameras").
namespace CameraGroup {
    constexpr int kUnassigned = -1;
    constexpr int kWire = 0;           // forming wire, before the press section
    constexpr int kPressPart = 1;
    constexpr int kPreDryer = 2;
    constexpr int kAfterDryer = 3;
    constexpr int kCalenderReel = 4;
    constexpr int kCount = 5;

    // Display name for a group index (kUnassigned -> "Unassigned").
    inline QString name(int group) {
        switch (group) {
        case kWire: return QStringLiteral("Wire");
        case kPressPart: return QStringLiteral("Press-Part");
        case kPreDryer: return QStringLiteral("Pre-Dryer");
        case kAfterDryer: return QStringLiteral("After-Dryer");
        case kCalenderReel: return QStringLiteral("Calender-Reel");
        default: return QStringLiteral("Unassigned");
        }
    }
}

// Machine floors a camera can be mounted on. The Machine Layout panel paints
// one lane per floor so all camera positions are visible at once.
namespace CameraFloor {
    constexpr int kFirst = 1;
    constexpr int kSecond = 2;
    constexpr int kThird = 3;
    constexpr int kCount = 3;

    // Display name for a floor index (anything else -> "Unknown").
    inline QString name(int floor) {
        switch (floor) {
        case kFirst: return QStringLiteral("1st Floor");
        case kSecond: return QStringLiteral("2nd Floor");
        case kThird: return QStringLiteral("3rd Floor");
        default: return QStringLiteral("Unknown Floor");
        }
    }

    // 0-based lane index for a floor value, clamped to the known floors.
    inline int laneIndex(int floor) {
        if (floor >= kFirst && floor <= kThird) {
            return floor - kFirst;
        }
        return 0;
    }
}

// Camera metadata structure
struct CameraInfo {
    int id = 0;              // Camera ID (starts from 1)
    int source = 1;          // 0: Emulated, 1: Real, 2: Disabled
    QString name;            // e.g., "DRYER 1"
    QString location;        // e.g., "CYLINDER 13"
    QString side;            // "DRIVE SIDE" or "OPERATOR SIDE"
    int machinePosition = 0; // e.g., 16600 (mm)
    QString ipAddress;       // e.g., "172.20.2.1"
    QString macAddress;      // e.g., "00:11:22:33:44:55"
    QString subnetMask;      // e.g., "255.255.255.0"
    QString defaultGateway;  // e.g., "0.0.0.0"
    int fps = 50;            // Default 50
    bool enableAcquisitionFps = false; // Enable/disable AcquisitionFrameRate in Pylon
    int width = 780;         // Requested sensor width
    int height = 580;        // Requested sensor height
    int offsetX = 0;         // Requested AOI X offset
    int offsetY = 0;         // Requested AOI Y offset
    QString pixelFormat = "Mono8"; // Requested camera pixel format
    double exposureTimeAbs = 5000.0; // Exposure time in microseconds
    bool enableExposureTimeBase = false; // Enables exposure time base control
    double exposureTimeBaseAbs = 20.0; // Exposure time base in microseconds
    int exposureTimeRaw = 2044; // Raw exposure value
    bool chunkModeActive = false; // Enables chunk payload data
    QStringList enabledChunks; // Enabled chunk selectors
    double temperature = 0.0; // Temperature in Celsius (Runtime, not config)
    QString model;           // Populated at runtime
    QString imageSize;       // Populated at runtime
    int group = CameraGroup::kUnassigned; // Paper-machine section (CameraGroup::k*)
    int floor = CameraFloor::kFirst;      // Machine floor (CameraFloor::k*)
};
