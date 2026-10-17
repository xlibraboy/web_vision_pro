#include "ThemeContrast.h"

#include "config/CameraConfig.h"
#include "config/SystemColorScheme.h"
#include "gui/widgets/EventDashboard.h"
#include "gui/widgets/MachineLayoutPanel.h"

#include <QColor>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <cmath>

namespace {

// The themes a surface can be switched between, plus the modes the picker
// offers; ThemeMode::System resolves to one of the themes.
const ThemeMode kThemes[] = {ThemeMode::Light, ThemeMode::Dark};

double channelLuminance(double value) {
    value /= 255.0;
    return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

// WCAG relative luminance.
double luminance(const QColor& color) {
    return 0.2126 * channelLuminance(color.red())
         + 0.7152 * channelLuminance(color.green())
         + 0.0722 * channelLuminance(color.blue());
}

double contrastRatio(const QColor& first, const QColor& second) {
    const double a = luminance(first);
    const double b = luminance(second);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

struct Oklab {
    double l;
    double a;
    double b;
};

Oklab toOklab(const QColor& color) {
    const auto linear = [](double value) {
        value /= 255.0;
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    const double r = linear(color.red());
    const double g = linear(color.green());
    const double b = linear(color.blue());
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

// Distance in OKLab*100, the unit the palette comments state their separation in.
double oklabDistance(const QColor& first, const QColor& second) {
    const Oklab a = toOklab(first);
    const Oklab b = toOklab(second);
    return 100.0 * std::sqrt(std::pow(a.l - b.l, 2) + std::pow(a.a - b.a, 2)
                             + std::pow(a.b - b.b, 2));
}

} // namespace

void ThemeContrast::tokensMeetContrastOnEveryTheme()
{
    // Body-size bars. Text and status ink sit on the canvas, on cards and on
    // button faces at ~45 call sites, so all three backgrounds count.
    constexpr double kTextBar = 4.5;
    constexpr double kDisabledBar = 2.0;
    constexpr double kDividerBar = 1.2;

    for (const ThemeMode theme : kThemes) {
        const ThemeColors tc = CameraConfig::getThemeColors(theme);
        const QString name = CameraConfig::themeModeName(theme);
        const QVector<QPair<QString, QString>> backgrounds = {
            {QStringLiteral("background"), tc.bg},
            {QStringLiteral("surface"), tc.surface},
            {QStringLiteral("button face"), tc.btnBg},
        };
        const QVector<QPair<QString, QString>> inks = {
            {QStringLiteral("text"), tc.text},
            {QStringLiteral("muted"), tc.muted},
            {QStringLiteral("primary"), tc.primary},
            {QStringLiteral("ok"), tc.ok},
            {QStringLiteral("warn"), tc.warn},
            {QStringLiteral("critical"), tc.critical},
            {QStringLiteral("info"), tc.info},
        };

        for (const auto& [inkName, ink] : inks) {
            for (const auto& [backgroundName, background] : backgrounds) {
                const double ratio = contrastRatio(QColor(ink), QColor(background));
                QVERIFY2(ratio >= kTextBar, qPrintable(QStringLiteral(
                             "%1 theme: %2 on the %3 is only %4:1")
                             .arg(name, inkName, backgroundName).arg(ratio, 0, 'f', 2)));
            }
        }

        const double disabled = contrastRatio(QColor(tc.disabled), QColor(tc.bg));
        QVERIFY2(disabled >= kDisabledBar, qPrintable(QStringLiteral(
                     "%1 theme: disabled ink on the background is only %2:1")
                     .arg(name).arg(disabled, 0, 'f', 2)));

        const double divider = contrastRatio(QColor(tc.border), QColor(tc.bg));
        QVERIFY2(divider >= kDividerBar, qPrintable(QStringLiteral(
                     "%1 theme: the border does not separate from the background (%2:1)")
                     .arg(name).arg(divider, 0, 'f', 2)));

        const double handle = contrastRatio(QColor(tc.handle), QColor(tc.sliderBg));
        QVERIFY2(handle >= 3.0, qPrintable(QStringLiteral(
                     "%1 theme: the slider handle does not read on its groove (%2:1)")
                     .arg(name).arg(handle, 0, 'f', 2)));
    }
}

void ThemeContrast::statusInkIsReadableOnEveryFill()
{
    // Status chrome is a solid fill carrying one ink (badges, pills, chips).
    constexpr double kOnFillBar = 4.5;

    for (const ThemeMode theme : kThemes) {
        const ThemeColors tc = CameraConfig::getThemeColors(theme);
        const QString name = CameraConfig::themeModeName(theme);
        const QVector<QPair<QString, QString>> fills = {
            {QStringLiteral("ok"), tc.ok},
            {QStringLiteral("warn"), tc.warn},
            {QStringLiteral("critical"), tc.critical},
            {QStringLiteral("info"), tc.info},
            {QStringLiteral("primary"), tc.primary},
        };
        for (const auto& [fillName, fill] : fills) {
            const double ratio = contrastRatio(QColor(tc.onStatus), QColor(fill));
            QVERIFY2(ratio >= kOnFillBar, qPrintable(QStringLiteral(
                         "%1 theme: the status ink on the %2 fill is only %3:1")
                         .arg(name, fillName).arg(ratio, 0, 'f', 2)));
        }
    }
}

void ThemeContrast::sectionPaletteStaysDistinguishable()
{
    // Machine sections are told apart by color (position and the legend label
    // stay the primary encoding): every pair has to remain >= 10 OKLab*100
    // apart, on both themes.
    const int groups[] = {CameraGroup::kWire, CameraGroup::kPressPart,
                          CameraGroup::kPreDryer, CameraGroup::kAfterDryer,
                          CameraGroup::kCalenderReel, CameraGroup::kUnassigned};

    for (const ThemeMode theme : kThemes) {
        // groupColor() reads the effective theme, so the check runs through the
        // preview override.
        CameraConfig::setThemePreview(theme);
        for (const int left : groups) {
            for (const int right : groups) {
                if (left == right) {
                    continue;
                }
                const double distance = oklabDistance(MachineLayoutPanel::groupColor(left),
                                                     MachineLayoutPanel::groupColor(right));
                QVERIFY2(distance >= 10.0, qPrintable(QStringLiteral(
                             "%1 theme: machine sections %2 and %3 are only %4 OKLab*100 apart")
                             .arg(CameraConfig::themeModeName(theme))
                             .arg(left).arg(right).arg(distance, 0, 'f', 1)));
            }
        }
    }
    CameraConfig::clearThemePreview();
}

void ThemeContrast::eventSeriesStayDistinguishable()
{
    // The three event series (brightness / SPOTS % / CONTRAST) carry identity by
    // color: >= 22 OKLab*100 apart, and each has to read on the plot area it is
    // drawn on.
    for (const ThemeMode theme : kThemes) {
        const QString name = CameraConfig::themeModeName(theme);
        EventDashboard dashboard;
        dashboard.applyTheme(CameraConfig::getThemeColors(theme));

        const QVector<QPair<QString, QColor>> series = {
            {QStringLiteral("defect"), dashboard.defectSeriesColor()},
            {QStringLiteral("local"), dashboard.localSeriesColor()},
            {QStringLiteral("contrast"), dashboard.contrastSeriesColor()},
        };
        for (const auto& [leftName, left] : series) {
            for (const auto& [rightName, right] : series) {
                if (leftName == rightName) {
                    continue;
                }
                const double distance = oklabDistance(left, right);
                QVERIFY2(distance >= 22.0, qPrintable(QStringLiteral(
                             "%1 theme: series %2 and %3 are only %4 OKLab*100 apart")
                             .arg(name, leftName, rightName).arg(distance, 0, 'f', 1)));
            }
            const double ratio = contrastRatio(left, QColor(CameraConfig::getThemeColors(theme).surface));
            QVERIFY2(ratio >= 3.0, qPrintable(QStringLiteral(
                         "%1 theme: series %2 on the plot area is only %3:1")
                         .arg(name, leftName).arg(ratio, 0, 'f', 2)));
        }
    }
}

void ThemeContrast::systemModeFollowsTheDesktopOverride()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    CameraConfig::clearThemePreview();
    CameraConfig::setThemeMode(ThemeMode::System);
    QCOMPARE(CameraConfig::effectiveThemeMode(), ThemeMode::System);

    // The override is the one probe that works everywhere (the Docker image has
    // no gsettings and no desktop session), so it is also how this suite drives
    // the follow-system path deterministically.
    qputenv("PAPERVISION_COLOR_SCHEME", "light");
    SystemColorScheme::refresh();
    QVERIFY2(CameraConfig::resolveThemeMode(ThemeMode::System) == ThemeMode::Light,
             "PAPERVISION_COLOR_SCHEME=light must select the light theme");
    QCOMPARE(CameraConfig::getThemeColors().bg, CameraConfig::getThemeColors(ThemeMode::Light).bg);

    qputenv("PAPERVISION_COLOR_SCHEME", "dark");
    SystemColorScheme::refresh();
    QVERIFY2(CameraConfig::resolveThemeMode(ThemeMode::System) == ThemeMode::Dark,
             "PAPERVISION_COLOR_SCHEME=dark must select the dark theme");
    QCOMPARE(CameraConfig::getThemeColors().bg, CameraConfig::getThemeColors(ThemeMode::Dark).bg);

    // A value that is not an answer falls through to the rest of the ladder,
    // which still has to land on a real theme.
    qputenv("PAPERVISION_COLOR_SCHEME", "sepia");
    SystemColorScheme::refresh();
    const ThemeMode resolved = CameraConfig::resolveThemeMode(ThemeMode::System);
    QVERIFY2(resolved == ThemeMode::Light || resolved == ThemeMode::Dark,
             "an unrecognised override must not leave the theme unresolved");
    QCOMPARE(CameraConfig::getThemeColors().bg, CameraConfig::getThemeColors(resolved).bg);

    qunsetenv("PAPERVISION_COLOR_SCHEME");
    SystemColorScheme::refresh();
}
