#pragma once

#include <QObject>

// Locks the type-system contract (review finding 9): the named scale is an
// ordered ladder, its families actually resolve in the runtime image, and the
// UI Preferences defaults are the scale steps rather than ad-hoc numbers.
class UiTypeScaleDefaults : public QObject {
    Q_OBJECT

private slots:
    void stepsFormAnOrderedLadder();
    void namedFamiliesAreInstalled();
    void preferenceDefaultsUseTheScale();
    void pxHelperFormatsCssValues();
};
