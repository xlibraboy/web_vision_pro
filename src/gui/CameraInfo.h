#pragma once

#include <QString>

// Camera metadata structure
struct CameraInfo {
    int id;              // Camera ID (starts from 01)
    QString location;    // Installation location
    QString side;        // "OS" (Operator Side) or "DS" (Drive Side)
    QString description; // Short description
    QString model;       // Camera model
    QString ipAddress;   // IP address or "Not Connected / Simulated"
    QString imageSize;   // e.g., "1920x1080"
    double fps;          // Frames per second
    double temperature;  // Temperature in Celsius
};
