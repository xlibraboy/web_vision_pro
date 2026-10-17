#pragma once

#include <QObject>

// The theme token tables have to keep their contrast on every theme. The light
// theme is the first one whose ink is dark on a light background, so the pairs
// are checked arithmetically instead of by eye: ink on every background the
// chrome paints, ink on every solid status fill, and the separation of the
// identity palettes that carry meaning by color.
class ThemeContrast : public QObject {
    Q_OBJECT

private slots:
    void tokensMeetContrastOnEveryTheme();
    void statusInkIsReadableOnEveryFill();
    void sectionPaletteStaysDistinguishable();
    void eventSeriesStayDistinguishable();
    void systemModeFollowsTheDesktopOverride();
};
