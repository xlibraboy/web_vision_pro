#pragma once

#include <QString>
#include <QStringList>

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
    double exposureTimeAbs = 40880.0; // Exposure time in microseconds
    bool enableExposureTimeBase = false; // Enables exposure time base control
    double exposureTimeBaseAbs = 20.0; // Exposure time base in microseconds
    int exposureTimeRaw = 2044; // Raw exposure value
    bool chunkModeActive = false; // Enables chunk payload data
    QStringList enabledChunks; // Enabled chunk selectors
    double temperature = 0.0; // Temperature in Celsius (Runtime, not config)
    QString model;           // Populated at runtime
    QString imageSize;       // Populated at runtime
};
