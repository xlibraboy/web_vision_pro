#include "ThemeContrast.h"

#include "config/CameraConfig.h"
#include "config/SystemColorScheme.h"
#include "gui/AnalysisView.h"
#include "gui/ConfigDialog.h"
#include "gui/widgets/EventDashboard.h"
#include "gui/widgets/MachineLayoutPanel.h"

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QMap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
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

// The fill a stylesheet paints a control with before it is interacted with, and
// the label ink that fill carries. "color:" inside "background-color:" must not
// count as the ink, hence the leading separator.
QString sheetBaseFill(const QString& sheet) {
    const QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral("background(-color)?:\\s*(#[0-9A-Fa-f]{6})")).match(sheet);
    return match.hasMatch() ? match.captured(2) : QString();
}

QString sheetInk(const QString& sheet) {
    const QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral("[;{\\s]color:\\s*(#[0-9A-Fa-f]{6})")).match(sheet);
    return match.hasMatch() ? match.captured(1) : QString();
}

// The ink a rendered glyph is drawn in: the dominant near-opaque pixel of its
// pixmap, which is what the eye reads as the label colour. Invalid when the
// pixmap paints nothing.
QColor glyphInk(const QIcon& icon, const QSize& size) {
    if (icon.isNull() || size.isEmpty()) {
        return QColor();
    }
    const QImage image = icon.pixmap(size).toImage().convertToFormat(QImage::Format_ARGB32);
    QMap<QRgb, int> coverage;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            if (qAlpha(pixel) >= 250) {
                coverage[pixel & 0x00FFFFFF] += 1;
            }
        }
    }
    QRgb dominant = 0;
    int best = 0;
    for (auto it = coverage.constBegin(); it != coverage.constEnd(); ++it) {
        if (it.value() > best) {
            best = it.value();
            dominant = it.key();
        }
    }
    return best > 0 ? QColor(dominant) : QColor();
}

// The fill the :hover rule swaps in. Absent when the sheet only shifts a border.
QString sheetHoverFill(const QString& sheet) {
    const int at = sheet.indexOf(QStringLiteral(":hover"));
    if (at < 0) {
        return QString();
    }
    const QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral("background(-color)?:\\s*(#[0-9A-Fa-f]{6})")).match(sheet, at);
    return match.hasMatch() ? match.captured(2) : QString();
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

void ThemeContrast::accentFilledControlHoversKeepTheirInk()
{
    // Accent-filled controls (primary actions, the active playback toggle) are
    // painted with the accent and carry the canvas colour as ink. Their hover
    // has to shift the accent shade: swapping in the neutral hover fill left
    // "Refresh Network" and the Save buttons at ~1.1:1 on the light theme.
    constexpr double kTextBar = 4.5;

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    CameraConfig::clearThemePreview();

    for (const ThemeMode theme : kThemes) {
        const ThemeColors tc = CameraConfig::getThemeColors(theme);
        const QString name = CameraConfig::themeModeName(theme);

        const QVector<QPair<QString, QString>> fills = {
            {QStringLiteral("accent"), tc.primary},
            {QStringLiteral("accent hover"), tc.primaryHover()},
            {QStringLiteral("accent active"), tc.primaryActive()},
            {QStringLiteral("active toggle hover"), QColor(tc.primary).darker(150).name()},
        };
        for (const auto& [fillName, fill] : fills) {
            const double ratio = contrastRatio(QColor(tc.bg), QColor(fill));
            QVERIFY2(ratio >= kTextBar, qPrintable(QStringLiteral(
                         "%1 theme: the canvas ink on the %2 fill is only %3:1")
                         .arg(name, fillName).arg(ratio, 0, 'f', 2)));
        }

        // The same pair on the widgets that wear it: every accent-filled button
        // in the System Configuration dialog has to keep its label readable in
        // the state hover puts it in, not only at rest.
        CameraConfig::setThemePreview(theme);
        ConfigDialog dialog(nullptr);
        dialog.updateTheme();

        int accentButtons = 0;
        for (QPushButton* button : dialog.findChildren<QPushButton*>()) {
            const QString sheet = button->styleSheet();
            if (sheetBaseFill(sheet) != tc.primary) {
                continue;
            }
            ++accentButtons;

            const QString ink = sheetInk(sheet);
            const QString hoverFill = sheetHoverFill(sheet);
            QVERIFY2(!ink.isEmpty() && !hoverFill.isEmpty(),
                     qPrintable(QStringLiteral("\"%1\" must state its ink and its hover fill: %2")
                                    .arg(button->text(), sheet)));

            const double ratio = contrastRatio(QColor(ink), QColor(hoverFill));
            QVERIFY2(ratio >= kTextBar, qPrintable(QStringLiteral(
                         "%1 theme: hovering \"%2\" leaves its label at %3:1")
                         .arg(name, button->text()).arg(ratio, 0, 'f', 2)));
        }
        QVERIFY2(accentButtons >= 3, qPrintable(QStringLiteral(
                     "%1 theme: only %2 accent-filled actions were found in the dialog")
                     .arg(name).arg(accentButtons)));
    }

    CameraConfig::clearThemePreview();
}

void ThemeContrast::floatingHandlesKeepTheirInkOnEveryTheme()
{
    // The Analysis View floats three controls over the frame area: the IMAGE and
    // TOOLS drawer handles - canvas-filled chips with a theme border - and the
    // TRACKS wave chip, which is glyph-only on the dashboard's canvas-coloured
    // top edge. Their label is a rendered glyph, not a stylesheet color, so the
    // pair is read off the widget: the pixmap ink against the fill the chip
    // paints. The white idle label on the light theme's near-white chip is what
    // this guards.
    constexpr double kLabelBar = 4.5;  // IMAGE / TOOLS carry text
    constexpr double kGlyphBar = 3.0;  // the TRACKS wave is a thin stroke

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir dataDir;
    QVERIFY(dataDir.isValid());
    CameraConfig::clearThemePreview();
    CameraConfig::setEventStoragePath(dataDir.path());

    for (const ThemeMode theme : kThemes) {
        CameraConfig::setThemePreview(theme);
        const ThemeColors tc = CameraConfig::getThemeColors(theme);
        const QString name = CameraConfig::themeModeName(theme);

        AnalysisView view(2);
        view.resize(1280, 800);
        view.show();
        view.activateWindow();
        view.updateTheme();
        QCoreApplication::processEvents();

        const QVector<QPair<QString, double>> handles = {
            {QStringLiteral("imageEdgeTab"), kLabelBar},
            {QStringLiteral("toolsEdgeTab"), kLabelBar},
            {QStringLiteral("tracksEdgeTab"), kGlyphBar},
        };
        for (const auto& [objectName, bar] : handles) {
            QToolButton* tab = view.findChild<QToolButton*>(objectName);
            QVERIFY2(tab, qPrintable(QStringLiteral("the Analysis View has no %1").arg(objectName)));

            // Idle: the chip's own fill, or the canvas behind a transparent one
            // (the TRACKS chip stays glyph-only until it is hovered).
            const QString idleSheet = tab->styleSheet();
            const QString idleFill = sheetBaseFill(idleSheet);
            const QColor idleBackground = idleFill.isEmpty() ? QColor(tc.bg) : QColor(idleFill);
            const QColor idleInk = glyphInk(tab->icon(), tab->iconSize());
            QVERIFY2(idleInk.isValid() && idleBackground.isValid(),
                     qPrintable(QStringLiteral("%1 theme: %2 paints no label: \"%3\"")
                                    .arg(name, objectName, idleSheet)));
            const double idleRatio = contrastRatio(idleInk, idleBackground);
            QVERIFY2(idleRatio >= bar, qPrintable(QStringLiteral(
                         "%1 theme: the idle %2 label is only %3:1 on its chip")
                         .arg(name, objectName).arg(idleRatio, 0, 'f', 2)));

            // Hover / focus / pinned: the fill the restyler swaps in, read back
            // from the widget so the active ink is the one that is painted. The
            // handle is hidden outside the single-camera tab, and a hidden
            // widget takes no focus, so show it for this check.
            tab->show();
            tab->setFocus(Qt::TabFocusReason);
            QCoreApplication::processEvents();
            if (!tab->hasFocus()) {
                continue;  // the offscreen platform gave no focus; idle is all this run can read
            }
            const QString activeFill = sheetBaseFill(tab->styleSheet());
            const QColor activeBackground = activeFill.isEmpty() ? QColor(tc.bg) : QColor(activeFill);
            const QColor activeInk = glyphInk(tab->icon(), tab->iconSize());
            QVERIFY2(activeInk.isValid(), qPrintable(QStringLiteral(
                         "%1 theme: the focused %2 paints no label").arg(name, objectName)));
            const double activeRatio = contrastRatio(activeInk, activeBackground);
            QVERIFY2(activeRatio >= bar, qPrintable(QStringLiteral(
                         "%1 theme: the focused %2 label is only %3:1 on its active chip")
                         .arg(name, objectName).arg(activeRatio, 0, 'f', 2)));
        }

        view.hide();
    }

    CameraConfig::clearThemePreview();
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
