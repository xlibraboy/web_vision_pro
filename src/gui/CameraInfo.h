#pragma once

#include <QString>

// Camera metadata structure
struct CameraInfo {
    int id;              // Camera ID (starts from 1)
    int source;          // 0: Emulated, 1: Real
    QString name;        // e.g., "DRYER 1"
    QString location;    // e.g., "CYLINDER 13"
    QString side;        // "DRIVE SIDE" or "OPERATOR SIDE"
    int machinePosition; // e.g., 16600 (mm)
    QString ipAddress;   // e.g., "172.20.2.1"
    QString macAddress;  // e.g., "00:11:22:33:44:55"
    QString subnetMask;  // e.g., "255.255.255.0"
    QString defaultGateway; // e.g., "0.0.0.0"
    int fps;             // Default 50
    bool enableAcquisitionFps; // Enable/disable AcquisitionFrameRate in Pylon
    double temperature;  // Temperature in Celsius (Runtime, not config)
    QString model;       // Populated at runtime
    QString imageSize;   // Populated at runtime
};
