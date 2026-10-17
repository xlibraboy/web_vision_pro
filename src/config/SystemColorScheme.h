#pragma once

#include "CameraConfig.h"

#include <QString>

/**
 * Which colour scheme the desktop is running, for ThemeMode::System.
 *
 * Qt 5 has no colour-scheme API of its own (QStyleHints::colorScheme() only
 * arrived in Qt 6.5) and the app does not depend on a desktop portal, so the
 * scheme is probed explicitly: an operator override first, then the GTK/GNOME
 * and KDE settings, then a dark Qt palette. Nothing that answers falls back to
 * Dark, which is the theme the app shipped with.
 *
 * The answer is cached: getThemeColors() is called from paint paths and must
 * never block on a probe, so MainWindow refreshes the cache on a timer while
 * ThemeMode::System is selected.
 */
class SystemColorScheme {
public:
    // Last known desktop scheme - Light or Dark, never System. Runs the probe
    // ladder (bounded by a short process timeout) the first time it is asked.
    static ThemeMode resolved();

    // Fresh probe; returns true when the answer changed since the last one.
    static bool refresh();

    // How the current answer was obtained, e.g. "gsettings color-scheme"; empty
    // when no probe answered and the dark fallback is in use.
    static QString source();

private:
    struct Answer {
        bool answered = false;
        ThemeMode mode = ThemeMode::Dark;
        QString source;
        int probe = -1;
    };

    // Runs the ladder from firstProbe down, returning the first probe that has
    // an answer (nothing answered -> `answered == false`).
    static Answer probeLadder(int firstProbe);

    // One rung of the ladder, by index; `answered == false` when that probe has
    // nothing to say about the desktop.
    static Answer probe(int index);
};
