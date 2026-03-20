#pragma once

// Temperature status enum shared across CameraManager and UI components.
// Thresholds per Basler Application Note AW00138003000 (GigE cameras):
//   Critical Temperature threshold: 72 °C
//   Over-Temperature (Error) threshold: 78 °C
namespace TempStatus {
    enum Status {
        Unknown  = 0,   // Camera not connected or temperature unavailable
        Ok       = 1,   // < 72 °C — normal operation
        Critical = 2,   // >= 72 °C — approaching sensor limit (warning)
        Error    = 3    // >= 78 °C — sensor powered down to prevent damage
    };

    inline Status classify(double temp) {
        if (temp < 0)     return Unknown;
        if (temp >= 78.0) return Error;
        if (temp >= 72.0) return Critical;
        return Ok;
    }
}
