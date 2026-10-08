#pragma once

#include <QObject>

// Regression tests for the screen-reader access layer added by review finding 4:
// the painted EventDashboard / MachineLayoutPanel canvases expose a plain-text
// summary through QAccessible, and the event-deletion ToggleSwitch announces
// its label and draws a visible keyboard-focus ring.
class AccessibilitySummary : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void dashboardSummaryDescribesEvent();
    void dashboardSummaryStatesMissingRecording();
    void machineLayoutSummaryCountsMarkers();
    void deletionToggleAnnouncesItsName();
    void focusRingRendersOnTheDeletionToggle();
};
