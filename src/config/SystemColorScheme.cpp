#include "SystemColorScheme.h"

#include <QColor>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPalette>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QStyleHints>
#include <QtGlobal>

#include <algorithm>

namespace {

constexpr int kProbeTimeoutMs = 400;

// The ladder, in priority order. Kept as indices so a refresh can resume at the
// probe that answered last time instead of re-running the ones before it.
enum Probe {
    ProbeOverride = 0,            // PAPERVISION_COLOR_SCHEME
    ProbeQtStyleHints,            // Qt 6.5+ QStyleHints::colorScheme()
    ProbeGtkThemeEnv,             // GTK_THEME
    ProbeGsettingsColorScheme,    // GNOME org.gnome.desktop.interface color-scheme
    ProbeGsettingsGtkTheme,       // GNOME org.gnome.desktop.interface gtk-theme
    ProbeKdeGlobals,              // ~/.config/kdeglobals
    ProbeQtPalette,               // dark platform palette
    ProbeCount
};

ThemeMode& cachedMode() {
    static ThemeMode mode = ThemeMode::Dark;
    return mode;
}

QString& cachedSource() {
    static QString source;
    return source;
}

int& cachedProbe() {
    static int probe = -1;
    return probe;
}

bool& cachedValid() {
    static bool valid = false;
    return valid;
}

bool containsDarkScheme(const QString& name) {
    return name.contains(QStringLiteral("dark"), Qt::CaseInsensitive);
}

QString runCommand(const QString& program, const QStringList& arguments) {
    QProcess process;
    process.start(program, arguments, QIODevice::ReadOnly);
    if (!process.waitForFinished(kProbeTimeoutMs)) {
        process.kill();
        process.waitForFinished(100);
        return QString();
    }
    return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
}

// Empty when the desktop has no gsettings, which is the case in the Docker
// image (Ubuntu base without the GNOME settings tools).
const QString& gsettingsBinary() {
    static const QString binary = QStandardPaths::findExecutable(QStringLiteral("gsettings"));
    return binary;
}

// Reads one org.gnome.desktop.interface key. gsettings prints GVariant text, so
// the answer arrives quoted ('prefer-dark').
QString gsettingsValue(const QString& key) {
    const QString binary = gsettingsBinary();
    if (binary.isEmpty()) {
        return QString();
    }
    QString value = runCommand(binary, {QStringLiteral("get"),
                                        QStringLiteral("org.gnome.desktop.interface"),
                                        key});
    value.remove(QLatin1Char('\''));
    return value.trimmed();
}

} // namespace

SystemColorScheme::Answer SystemColorScheme::probe(const int index) {
    Answer answer;
    switch (index) {
    case ProbeOverride: {
        const QString value = qEnvironmentVariable("PAPERVISION_COLOR_SCHEME").trimmed().toLower();
        if (value == QLatin1String("dark")) {
            answer = {true, ThemeMode::Dark, QStringLiteral("PAPERVISION_COLOR_SCHEME"), index};
        } else if (value == QLatin1String("light")) {
            answer = {true, ThemeMode::Light, QStringLiteral("PAPERVISION_COLOR_SCHEME"), index};
        }
        break;
    }
    case ProbeQtStyleHints: {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        if (QGuiApplication::instance()) {
            switch (QGuiApplication::styleHints()->colorScheme()) {
            case Qt::ColorScheme::Dark:
                answer = {true, ThemeMode::Dark, QStringLiteral("Qt color scheme"), index};
                break;
            case Qt::ColorScheme::Light:
                answer = {true, ThemeMode::Light, QStringLiteral("Qt color scheme"), index};
                break;
            case Qt::ColorScheme::Unknown:
                break;
            }
        }
#endif
        break;
    }
    case ProbeGtkThemeEnv: {
        const QString theme = qEnvironmentVariable("GTK_THEME").trimmed();
        if (!theme.isEmpty()) {
            answer = {true, containsDarkScheme(theme) ? ThemeMode::Dark : ThemeMode::Light,
                      QStringLiteral("GTK_THEME"), index};
        }
        break;
    }
    case ProbeGsettingsColorScheme: {
        const QString value = gsettingsValue(QStringLiteral("color-scheme"));
        if (value == QLatin1String("prefer-dark")) {
            answer = {true, ThemeMode::Dark, QStringLiteral("gsettings color-scheme"), index};
        } else if (value == QLatin1String("prefer-light")) {
            answer = {true, ThemeMode::Light, QStringLiteral("gsettings color-scheme"), index};
        }
        // "default" is GNOME's "no preference": the GTK theme below decides.
        break;
    }
    case ProbeGsettingsGtkTheme: {
        const QString value = gsettingsValue(QStringLiteral("gtk-theme"));
        if (!value.isEmpty()) {
            answer = {true, containsDarkScheme(value) ? ThemeMode::Dark : ThemeMode::Light,
                      QStringLiteral("gsettings gtk-theme"), index};
        }
        break;
    }
    case ProbeKdeGlobals: {
        const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                             + QStringLiteral("/kdeglobals");
        if (QFileInfo::exists(config)) {
            QSettings kde(config, QSettings::IniFormat);
            const QString scheme = kde.value(QStringLiteral("General/ColorScheme")).toString();
            if (!scheme.isEmpty()) {
                answer = {true, containsDarkScheme(scheme) ? ThemeMode::Dark : ThemeMode::Light,
                          QStringLiteral("kdeglobals ColorScheme"), index};
                break;
            }
            // Older KDE config: the window background colour decides.
            const QStringList channels =
                kde.value(QStringLiteral("Colors:Window/BackgroundNormal")).toString()
                    .split(QLatin1Char(','));
            if (channels.size() == 3) {
                const QColor window(channels.at(0).toInt(), channels.at(1).toInt(),
                                    channels.at(2).toInt());
                if (window.isValid()) {
                    answer = {true, window.lightness() < 128 ? ThemeMode::Dark : ThemeMode::Light,
                              QStringLiteral("kdeglobals window color"), index};
                }
            }
        }
        break;
    }
    case ProbeQtPalette: {
        // A dark platform palette is an answer; a light one is not, because
        // main.cpp forces the Fusion style and its stock palette is light even
        // on a desktop that asked for dark.
        if (QGuiApplication::instance()
            && QGuiApplication::palette().color(QPalette::Window).lightness() < 128) {
            answer = {true, ThemeMode::Dark, QStringLiteral("dark Qt palette"), index};
        }
        break;
    }
    default:
        break;
    }
    return answer;
}

SystemColorScheme::Answer SystemColorScheme::probeLadder(const int firstProbe) {
    for (int index = firstProbe; index < ProbeCount; ++index) {
        const Answer answer = probe(index);
        if (answer.answered) {
            return answer;
        }
    }
    return Answer{};
}

ThemeMode SystemColorScheme::resolved() {
    if (!cachedValid()) {
        refresh();
    }
    return cachedMode();
}

bool SystemColorScheme::refresh() {
    // The override costs nothing (an environment lookup) and may be set after a
    // previous refresh, so it is always re-read; the forking probes resume at
    // whichever one answered last time.
    Answer answer = probe(ProbeOverride);
    if (!answer.answered) {
        answer = probeLadder(std::max(ProbeOverride + 1, cachedProbe()));
    }

    const bool wasFirstProbe = !cachedValid();
    const ThemeMode previous = cachedMode();
    cachedValid() = true;
    cachedMode() = answer.answered ? answer.mode : ThemeMode::Dark;
    cachedSource() = answer.source;
    cachedProbe() = answer.probe;
    return !wasFirstProbe && cachedMode() != previous;
}

QString SystemColorScheme::source() {
    return cachedValid() ? cachedSource() : QString();
}
