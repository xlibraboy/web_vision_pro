#pragma once

#include <QObject>

// Regression tests for Help > About.
//
// The About dialog is the surface support reads when a machine misbehaves, so
// its version line must report the pylon runtime the app is actually linked
// against (queried at runtime from Pylon::VersionInfo) instead of a hard-coded
// number that silently drifts when the SDK is swapped. The render test also
// writes about-dialog-preview.png next to the test binary for visual review.
class AboutDialogTest : public QObject {
    Q_OBJECT

private slots:
    void aboutTextReportsTheRunningPylonVersion();
    void aboutDialogRendersTheVersionLine();
};
