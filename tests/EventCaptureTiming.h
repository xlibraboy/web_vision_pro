#pragma once

#include <QObject>

// Regression tests for the event recorder's cross-camera timing.
//
// 1. A camera that misses frames must still record the same post-trigger
//    window in SECONDS as a healthy one. The window used to be sized in frames
//    from the rate the camera REPORTED (ResultingFrameRateAbs), which stays at
//    the configured value while the host drops frames — so the dropping
//    camera's window stretched and its trigger mark drifted by exactly the
//    frames it lost.
//
// 2. A participating camera that stops streaming must be recorded on the event
//    as missing (EventInfo::missingCameraIds) instead of just not being there.
class EventCaptureTiming : public QObject {
    Q_OBJECT

private slots:
    void droppingCameraRecordsTheSamePostWindowInSeconds();
    void silentCameraIsRecordedAsMissing();
};
