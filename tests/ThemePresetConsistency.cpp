#include "ThemePresetConsistency.h"

#include "config/CameraConfig.h"
#include "gui/AnalysisView.h"
#include "gui/ConfigDialog.h"
#include "gui/DetailView.h"
#include "gui/LiveDashboard.h"
#include "gui/widgets/CameraCard.h"
#include "gui/widgets/ChangelogDialog.h"
#include "gui/widgets/DocsDialog.h"
#include "gui/widgets/IconManager.h"
#include "gui/widgets/LiveViewWindow.h"
#include "gui/widgets/MachineLayoutPanel.h"
#include "gui/widgets/NetworkSummaryHeader.h"
#include "gui/widgets/OpcUaLiveStatusWindow.h"
#include "gui/widgets/EventDashboard.h"

#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QPixmap>
#include <QSet>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// The themes a surface can be switched between. ThemeMode::System resolves to
// one of them, so it is covered by the same assertions.
const ThemeMode kThemes[] = {ThemeMode::Light, ThemeMode::Dark};
constexpr int kThemeCount = int(sizeof(kThemes) / sizeof(kThemes[0]));

// A stale-color failure lists this many offenders, so one run reports a whole
// surface's worth of missed re-applies instead of only the first one.
constexpr int kMaxStaleHits = 8;

QStringList tokenValues(const ThemeColors& tc) {
    return {tc.bg, tc.border, tc.btnBg, tc.btnHover, tc.primary, tc.sliderBg,
            tc.handle, tc.text, tc.surface, tc.muted, tc.disabled, tc.focus,
            tc.ok, tc.warn, tc.critical, tc.info, tc.onStatus};
}

// Colors that identify one theme: they appear in that theme's tokens and in no
// other theme's. Anything still painted in them after a switch is stale.
QStringList exclusiveColors(ThemeMode theme) {
    const QStringList mine = tokenValues(CameraConfig::getThemeColors(theme));
    QStringList exclusive;
    for (const QString& value : mine) {
        bool sharedWithAnotherTheme = false;
        for (const ThemeMode other : kThemes) {
            if (other == theme) {
                continue;
            }
            if (tokenValues(CameraConfig::getThemeColors(other)).contains(value)) {
                sharedWithAnotherTheme = true;
                break;
            }
        }
        // Pure black/white are scrims, shadows and ink-on-video surfaces:
        // intentionally theme-independent, so they identify nothing.
        const QString lowered = value.toLower();
        if (!sharedWithAnotherTheme && lowered != "#000000" && lowered != "#ffffff") {
            exclusive.append(lowered);
        }
    }
    exclusive.removeDuplicates();
    return exclusive;
}

// Stylesheets spell colors as "#rrggbb" or as "rgba(r, g, b, a)"; a stale value
// is a hit in either spelling.
QStringList colorSpellings(const QString& hex) {
    const QColor c(hex);
    return {hex.toLower(),
            QString("rgba(%1, %2, %3,").arg(c.red()).arg(c.green()).arg(c.blue()),
            QString("rgb(%1, %2, %3)").arg(c.red()).arg(c.green()).arg(c.blue())};
}

// Stylesheet of the widget itself plus every descendant, tagged with the widget
// they belong to so a stale color names its owner.
QVector<QPair<QWidget*, QString>> stylesheetsIn(QWidget& root) {
    QVector<QPair<QWidget*, QString>> styles;
    const auto append = [&styles](QWidget* widget) {
        const QString sheet = widget->styleSheet();
        if (!sheet.isEmpty()) {
            styles.append({widget, sheet});
        }
    };
    append(&root);
    for (QWidget* child : root.findChildren<QWidget*>()) {
        append(child);
    }
    return styles;
}

// Names the offending widget (class, object name, button text) so a failure
// points at the code that has to re-apply the style.
QString describeWidget(QWidget* widget) {
    QString description = widget->metaObject()->className();
    if (!widget->objectName().isEmpty()) {
        description += QStringLiteral("#%1").arg(widget->objectName());
    }
    if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
        if (!button->text().isEmpty()) {
            description += QStringLiteral(" \"%1\"").arg(button->text());
        }
    }
    QString chain;
    for (QWidget* parent = widget->parentWidget(); parent; parent = parent->parentWidget()) {
        chain += QStringLiteral("/%1").arg(parent->metaObject()->className());
    }
    return QStringLiteral("%1 [%2]").arg(description, chain);
}

// The rule a stale color appears in, so a failure points at the style that has
// to be rebuilt and not just at the widget that carries it.
QString ruleAround(const QString& sheet, const QString& needle) {
    const int at = sheet.indexOf(needle, 0, Qt::CaseInsensitive);
    if (at < 0) {
        return QString();
    }
    return sheet.mid(qMax(0, at - 70), needle.size() + 90).simplified();
}

// Every stale color found in the sheets, named for the failure message: the
// whole list at once, so one run shows everything that has to be re-applied.
QString staleColorIn(const QVector<QPair<QWidget*, QString>>& stylesheets, ThemeMode activeTheme) {
    QStringList hits;
    for (const ThemeMode other : kThemes) {
        if (other == activeTheme) {
            continue;
        }
        for (const QString& color : exclusiveColors(other)) {
            for (const QString& spelling : colorSpellings(color)) {
                for (const auto& [widget, sheet] : stylesheets) {
                    if (!sheet.contains(spelling, Qt::CaseInsensitive)) {
                        continue;
                    }
                    const QString hit = QStringLiteral("%1 in %2 (%3): \"%4\"")
                        .arg(color, describeWidget(widget),
                             CameraConfig::themeModeName(other), ruleAround(sheet, spelling));
                    if (!hits.contains(hit)) {
                        hits.append(hit);
                    }
                    if (hits.size() >= kMaxStaleHits) {
                        return hits.join(QStringLiteral("; "));
                    }
                }
            }
        }
    }
    return hits.join(QStringLiteral("; "));
}

// Icons and pixmaps are not stylesheets: a duotone glyph is rendered from the
// theme tokens the moment it is asked for, so its ink is what a theme switch can
// leave behind. Every button icon, sidebar/list row icon, tab icon and label
// pixmap is collected with its opaque pixels.
QVector<QPair<QString, QSet<QRgb>>> glyphsIn(QWidget& root) {
    QVector<QPair<QString, QSet<QRgb>>> glyphs;

    const auto addIcon = [&glyphs](const QString& owner, const QIcon& icon) {
        if (icon.isNull()) {
            return;
        }
        const QImage image = icon.pixmap(24, 24).toImage().convertToFormat(QImage::Format_ARGB32);
        QSet<QRgb> pixels;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QRgb pixel = image.pixel(x, y);
                if (qAlpha(pixel) >= 250) {
                    pixels.insert(pixel & 0x00FFFFFF);
                }
            }
        }
        if (!pixels.isEmpty()) {
            glyphs.append({owner, pixels});
        }
    };
    const auto addPixmap = [&glyphs](const QString& owner, const QPixmap& pixmap) {
        if (pixmap.isNull()) {
            return;
        }
        const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
        QSet<QRgb> pixels;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QRgb pixel = image.pixel(x, y);
                if (qAlpha(pixel) >= 250) {
                    pixels.insert(pixel & 0x00FFFFFF);
                }
            }
        }
        if (!pixels.isEmpty()) {
            glyphs.append({owner, pixels});
        }
    };

    for (QAbstractButton* button : root.findChildren<QAbstractButton*>()) {
        addIcon(QStringLiteral("%1[button]").arg(button->metaObject()->className()), button->icon());
    }
    for (QLabel* label : root.findChildren<QLabel*>()) {
        if (const QPixmap* pixmap = label->pixmap()) {
            addPixmap(QStringLiteral("QLabel[label]"), *pixmap);
        }
    }
    // Sidebar rows (the System Configuration nav) and tab labels carry their
    // glyph in the model, not in a widget.
    for (QListWidget* list : root.findChildren<QListWidget*>()) {
        for (int row = 0; row < list->count(); ++row) {
            addIcon(QStringLiteral("%1[%2]").arg(list->metaObject()->className(),
                                                 list->item(row)->text()),
                    list->item(row)->icon());
        }
    }
    for (QTabWidget* tabs : root.findChildren<QTabWidget*>()) {
        for (int index = 0; index < tabs->count(); ++index) {
            addIcon(QStringLiteral("%1[%2]").arg(tabs->metaObject()->className(),
                                                 tabs->tabText(index)),
                    tabs->tabBar()->tabIcon(index));
        }
    }

    return glyphs;
}

QString staleGlyphColorIn(QWidget& root, ThemeMode activeTheme) {
    const QVector<QPair<QString, QSet<QRgb>>> glyphs = glyphsIn(root);
    for (const ThemeMode other : kThemes) {
        if (other == activeTheme) {
            continue;
        }
        for (const QString& color : exclusiveColors(other)) {
            const QColor target(color);
            const QRgb wanted = (static_cast<QRgb>(target.red()) << 16)
                              | (static_cast<QRgb>(target.green()) << 8)
                              | static_cast<QRgb>(target.blue());
            for (const auto& [owner, pixels] : glyphs) {
                if (pixels.contains(wanted)) {
                    return QStringLiteral("%1 is drawn in %2 (%3)")
                        .arg(owner, color, CameraConfig::themeModeName(other));
                }
            }
        }
    }
    return QString();
}

QString artifactPath(const QString& name) {
    return QDir(QCoreApplication::applicationDirPath()).filePath(name);
}

} // namespace

void ThemePresetConsistency::previewOverrideDrivesTheEffectiveTheme()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    CameraConfig::clearThemePreview();
    CameraConfig::setThemeMode(ThemeMode::Dark);

    // Picking a theme in the dropdown must reach every getThemeColors() caller
    // before anything is saved - that is what makes the selector feel live.
    CameraConfig::setThemePreview(ThemeMode::Light);
    QCOMPARE(CameraConfig::getThemeColors().primary, CameraConfig::getThemeColors(ThemeMode::Light).primary);
    QCOMPARE(CameraConfig::getThemeColors().bg, CameraConfig::getThemeColors(ThemeMode::Light).bg);

    // Abandoning the pick restores the saved theme.
    CameraConfig::clearThemePreview();
    QCOMPARE(CameraConfig::getThemeColors().primary, CameraConfig::getThemeColors(ThemeMode::Dark).primary);
    QCOMPARE(CameraConfig::getThemeMode(), ThemeMode::Dark);
}

void ThemePresetConsistency::themeModesResolveAndMigrate()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    // An installation from before the theme modes saved a colour preset
    // instead. Every one of them was dark, so it must land on Dark - not on a
    // preset index that no longer exists, and not on Light.
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "PaperVision", "SystemConfig");
        settings.clear();
        settings.setValue("ThemePreset", 7);
    }
    QCOMPARE(CameraConfig::getThemeMode(), ThemeMode::Dark);

    // A fresh installation follows the desktop, which resolves to one of the
    // two themes and drives the tokens.
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "PaperVision", "SystemConfig");
        settings.clear();
    }
    QCOMPARE(CameraConfig::getThemeMode(), ThemeMode::System);
    const ThemeMode resolved = CameraConfig::resolveThemeMode(ThemeMode::System);
    QVERIFY(resolved == ThemeMode::Light || resolved == ThemeMode::Dark);
    QCOMPARE(CameraConfig::getThemeColors().bg, CameraConfig::getThemeColors(resolved).bg);

    // A stored mode from a future build renders as the theme the app shipped
    // with rather than as an undefined palette.
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "PaperVision", "SystemConfig");
        settings.setValue("ThemeMode", 99);
    }
    QCOMPARE(CameraConfig::getThemeMode(), ThemeMode::Dark);

    CameraConfig::setThemeMode(ThemeMode::System);
}

void ThemePresetConsistency::configDialogReThemesWhenThePresetChanges()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    CameraConfig::clearThemePreview();
    CameraConfig::setThemeMode(ThemeMode::Dark);

    ConfigDialog dialog(nullptr);
    dialog.resize(1200, 800);
    dialog.show();
    QCoreApplication::processEvents();

    // The dropdown lists the two themes plus following the desktop, and the
    // saved Dark mode selects its row by data, not by its stored value: Dark is
    // value 2 but row 1 of three, so a row/value mix-up would show System here.
    QComboBox* themeCombo = nullptr;
    for (QComboBox* combo : dialog.findChildren<QComboBox*>()) {
        if (combo->count() == kThemeCount + 1
            && combo->itemData(0).toInt() == int(ThemeMode::Light)) {
            themeCombo = combo;
            break;
        }
    }
    QVERIFY2(themeCombo, "the Theme dropdown must list Light, Dark and Follow system");
    QCOMPARE(themeCombo->itemData(1).toInt(), int(ThemeMode::Dark));
    QCOMPARE(themeCombo->itemData(2).toInt(), int(ThemeMode::System));
    QCOMPARE(themeCombo->currentData().toInt(), int(ThemeMode::Dark));

    const QPixmap darkLook = dialog.grab();
    QVERIFY2(!darkLook.isNull() && darkLook.width() > 0, "the config dialog must render");
    QVERIFY2(darkLook.save(artifactPath(QStringLiteral("theme-dark-config-dialog.png"))),
             "the dark preview must be saved for review");

    // This is the dialog that holds the dropdown: it used to keep the theme it
    // was built with for the whole session.
    CameraConfig::setThemePreview(ThemeMode::Light);
    dialog.updateTheme();
    QCoreApplication::processEvents();

    const QPixmap lightLook = dialog.grab();
    QVERIFY2(lightLook.save(artifactPath(QStringLiteral("theme-light-config-dialog.png"))),
             "the light preview must be saved for review");
    QVERIFY2(darkLook.toImage() != lightLook.toImage(),
             "switching the theme must visibly re-theme the System Configuration dialog");

    const QString stale = staleColorIn(stylesheetsIn(dialog), ThemeMode::Light);
    QVERIFY2(stale.isEmpty(), qPrintable(QStringLiteral(
                 "the dialog still paints the other theme's color: %1").arg(stale)));

    // A theme picked but never saved is abandoned, not left applied.
    CameraConfig::clearThemePreview();
    dialog.updateTheme();
    QCOMPARE(CameraConfig::getThemeColors().primary, CameraConfig::getThemeColors(ThemeMode::Dark).primary);

    dialog.hide();
}

void ThemePresetConsistency::glyphAssetsAreTintedFromTheTheme()
{
    // The shipped arrows carry one flat fill (#E3E3E3). A stylesheet cannot
    // recolor them, so they are re-rendered in the theme's ink instead: on a
    // theme whose text is not the shipped grey, the glyph must not stay grey.
    const QString asset = QStringLiteral(":/assets/icons/arrow_up.svg");
    const QColor ink(CameraConfig::getThemeColors(ThemeMode::Light).text);

    const QString themedFile = IconManager::themedAssetFile(asset, ink);
    QVERIFY2(themedFile != asset,
             "the arrow must be re-rendered for the theme, not taken from the fixed asset");
    QVERIFY(QFile::exists(themedFile));

    const QImage glyph(themedFile);
    QVERIFY(!glyph.isNull());

    QMap<QRgb, int> coverage;
    for (int y = 0; y < glyph.height(); ++y) {
        for (int x = 0; x < glyph.width(); ++x) {
            const QRgb pixel = glyph.pixel(x, y);
            if (qAlpha(pixel) > 0) {
                coverage[pixel] += 1;
            }
        }
    }
    QVERIFY2(!coverage.isEmpty(), "the re-rendered arrow must not be blank");

    QRgb dominant = 0;
    int best = 0;
    for (auto it = coverage.constBegin(); it != coverage.constEnd(); ++it) {
        if (it.value() > best) {
            best = it.value();
            dominant = it.key();
        }
    }
    QCOMPARE(QColor(dominant).name(), ink.name());

    // A different ink is a different glyph: themes never share one rendering.
    QVERIFY(IconManager::themedAssetFile(
                asset, QColor(CameraConfig::getThemeColors(ThemeMode::Dark).text)) != themedFile);

    // The path goes into an `image: url(...)` rule, so it must drop into a
    // stylesheet cleanly and still load: a broken path would leave every spin
    // box without its steppers.
    QWidget host;
    host.setStyleSheet(QStringLiteral(
        "QSpinBox::up-arrow { image: url(%1); width: 8px; height: 8px; }").arg(themedFile));
    const QString applied = host.styleSheet();
    QVERIFY2(applied.contains(themedFile), qPrintable(applied));
    QVERIFY2(!QPixmap(themedFile).isNull(), qPrintable(themedFile));
}

void ThemePresetConsistency::noSurfaceKeepsAnotherPresetsColors()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    CameraConfig::setThemeMode(ThemeMode::Dark);

    for (const ThemeMode theme : kThemes) {
        CameraConfig::setThemePreview(theme);
        const ThemeColors tc = CameraConfig::getThemeColors(theme);

        // Panels that own local stylesheets and outlive a theme change.
        ConfigDialog dialog(nullptr);
        DocsDialog docs;
        ChangelogDialog changelog;
        CameraCard card(CameraInfo{}, nullptr);

        dialog.resize(1200, 800);
        dialog.show();
        docs.resize(900, 600);
        docs.show();
        changelog.resize(900, 600);
        changelog.show();
        QCoreApplication::processEvents();

        dialog.updateTheme();
        docs.updateTheme();
        changelog.updateTheme();
        card.updateTheme();

        const QVector<QPair<QString, QWidget*>> surfaces = {
            {QStringLiteral("config-dialog"), &dialog},
            {QStringLiteral("docs-dialog"), &docs},
            {QStringLiteral("changelog-dialog"), &changelog},
            {QStringLiteral("camera-card"), &card},
        };

        const QString themeName = CameraConfig::themeModeName(theme);
        for (const auto& [name, surface] : surfaces) {
            const QString stale = staleColorIn(stylesheetsIn(*surface), theme);
            QVERIFY2(stale.isEmpty(), qPrintable(QStringLiteral(
                         "%1 paints the other theme's color on the %2 theme: %3")
                         .arg(name, themeName, stale)));
        }

        // Visual artifacts per theme for review (chrome, not video surfaces).
        QVERIFY2(dialog.grab().save(artifactPath(
                     QStringLiteral("theme-%1-config-dialog.png").arg(themeName))),
                 "the dialog preview must be saved for review");
        QVERIFY2(docs.grab().save(artifactPath(
                     QStringLiteral("theme-%1-docs.png").arg(themeName))),
                 "the docs preview must be saved for review");

        // Spot-check that the theme actually reached the chrome.
        const QVector<QPair<QWidget*, QString>> dialogSheets = stylesheetsIn(dialog);
        QString chrome;
        for (const auto& [widget, sheet] : dialogSheets) {
            chrome += sheet;
        }
        QVERIFY2(chrome.contains(tc.bg, Qt::CaseInsensitive)
                     || chrome.contains(tc.primary, Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("%1 theme token missing from the dialog chrome")
                                .arg(themeName)));

        docs.hide();
        changelog.hide();
        dialog.hide();
    }

    CameraConfig::clearThemePreview();
}

// The shape of one operator session: the surfaces are built once on the saved
// theme and then live through a pick in the Theme dropdown, which the app
// forwards to every updateTheme(). Stylesheets and glyphs both have to follow -
// a duotone icon is rendered from the tokens when it is asked for, so it is not
// covered by the stylesheet re-apply.
void ThemePresetConsistency::liveSwitchReTintsStylesheetsAndGlyphs()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    QTemporaryDir dataDir;
    QVERIFY(dataDir.isValid());
    CameraConfig::setEventStoragePath(dataDir.path());

    CameraConfig::clearThemePreview();
    CameraConfig::setThemeMode(ThemeMode::Dark);

    ConfigDialog dialog(nullptr);
    DocsDialog docs;
    ChangelogDialog changelog;
    NetworkSummaryHeader header;
    CameraCard card(CameraInfo{}, nullptr);
    AnalysisView analysis(2);
    DetailView detail;
    LiveDashboard dashboard(2);
    LiveViewWindow liveWindow;
    OpcUaLiveStatusWindow opcUaStatus;
    // The two canvases that paint their own colors: a machine layout and an
    // event chart, kept for the palette review artifacts.
    MachineLayoutPanel machineLayout;
    EventDashboard eventChart;

    dialog.resize(1200, 800);
    docs.resize(900, 600);
    changelog.resize(900, 600);
    header.resize(1200, 200);
    card.resize(600, 200);
    analysis.resize(1400, 900);
    detail.resize(1200, 800);
    dashboard.resize(1200, 800);
    liveWindow.resize(1200, 800);
    opcUaStatus.resize(900, 600);
    machineLayout.resize(1200, 400);
    eventChart.resize(1200, 240);
    dialog.show();
    docs.show();
    changelog.show();
    header.show();
    card.show();
    analysis.show();
    detail.show();
    dashboard.show();
    liveWindow.show();
    opcUaStatus.show();
    machineLayout.show();
    eventChart.show();
    QCoreApplication::processEvents();

    const QVector<QPair<QString, QWidget*>> surfaces = {
        {QStringLiteral("config-dialog"), &dialog},
        {QStringLiteral("docs-dialog"), &docs},
        {QStringLiteral("changelog-dialog"), &changelog},
        {QStringLiteral("network-summary-header"), &header},
        {QStringLiteral("camera-card"), &card},
        {QStringLiteral("analysis-view"), &analysis},
        {QStringLiteral("detail-view"), &detail},
        {QStringLiteral("live-dashboard"), &dashboard},
        {QStringLiteral("live-view-window"), &liveWindow},
        {QStringLiteral("opcua-live-status"), &opcUaStatus},
        {QStringLiteral("machine-layout"), &machineLayout},
    };

    // Both directions, so a one-way refresh cannot pass: to Light (the theme
    // that shares the fewest tokens with the saved Dark) and back again.
    for (const ThemeMode theme : {ThemeMode::Light, ThemeMode::Dark}) {
        CameraConfig::setThemePreview(theme);

        dialog.updateTheme();
        docs.updateTheme();
        changelog.updateTheme();
        header.updateTheme();
        card.updateTheme();
        analysis.updateTheme();
        detail.updateTheme();
        dashboard.updateTheme();
        liveWindow.updateTheme();
        opcUaStatus.updateTheme();
        machineLayout.updateTheme();
        eventChart.applyTheme(CameraConfig::getThemeColors());
        eventChart.repaint();
        QCoreApplication::processEvents();

        const QString themeName = CameraConfig::themeModeName(theme);
        for (const auto& [name, surface] : surfaces) {
            const QString staleSheet = staleColorIn(stylesheetsIn(*surface), theme);
            QVERIFY2(staleSheet.isEmpty(), qPrintable(QStringLiteral(
                         "%1 keeps the other theme's stylesheet color after the live "
                         "switch to %2: %3").arg(name, themeName, staleSheet)));

            const QString staleGlyph = staleGlyphColorIn(*surface, theme);
            QVERIFY2(staleGlyph.isEmpty(), qPrintable(QStringLiteral(
                         "%1 keeps the other theme's glyph ink after the live "
                         "switch to %2: %3").arg(name, themeName, staleGlyph)));
        }

        QVERIFY2(dialog.grab().save(artifactPath(
                     QStringLiteral("live-switch-%1-config-dialog.png").arg(themeName))),
                 "the switched dialog must be saved for review");
        QVERIFY2(analysis.grab().save(artifactPath(
                     QStringLiteral("live-switch-%1-analysis-view.png").arg(themeName))),
                 "the switched analysis view must be saved for review");
        QVERIFY2(detail.grab().save(artifactPath(
                     QStringLiteral("live-switch-%1-detail-view.png").arg(themeName))),
                 "the switched detail view must be saved for review");
        QVERIFY2(machineLayout.grab().save(artifactPath(
                     QStringLiteral("live-switch-%1-machine-layout.png").arg(themeName))),
                 "the switched machine layout must be saved for review");
        QVERIFY2(eventChart.grab().save(artifactPath(
                     QStringLiteral("live-switch-%1-event-dashboard.png").arg(themeName))),
                 "the switched event dashboard must be saved for review");
    }

    CameraConfig::clearThemePreview();
    eventChart.hide();
    machineLayout.hide();
    opcUaStatus.hide();
    liveWindow.hide();
    dashboard.hide();
    detail.hide();
    analysis.hide();
    card.hide();
    header.hide();
    changelog.hide();
    docs.hide();
    dialog.hide();
}
