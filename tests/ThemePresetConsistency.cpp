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
#include "gui/widgets/NetworkSummaryHeader.h"
#include "gui/widgets/OpcUaLiveStatusWindow.h"

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

constexpr int kPresetCount = 8;

QStringList tokenValues(const ThemeColors& tc) {
    return {tc.bg, tc.border, tc.btnBg, tc.btnHover, tc.primary, tc.sliderBg,
            tc.handle, tc.text, tc.surface, tc.muted, tc.disabled, tc.focus,
            tc.ok, tc.warn, tc.critical, tc.info, tc.onStatus};
}

// Colors that identify one preset: they appear in that preset's tokens and in
// no other preset's. Anything still painted in them after a switch is stale.
QStringList exclusiveColors(int preset) {
    const QStringList mine = tokenValues(CameraConfig::getThemeColors(preset));
    QStringList exclusive;
    for (const QString& value : mine) {
        bool sharedWithAnotherPreset = false;
        for (int other = 0; other < kPresetCount; ++other) {
            if (other == preset) {
                continue;
            }
            if (tokenValues(CameraConfig::getThemeColors(other)).contains(value)) {
                sharedWithAnotherPreset = true;
                break;
            }
        }
        // Pure black/white are scrims, shadows and ink-on-video surfaces:
        // intentionally preset-independent, so they identify nothing.
        const QString lowered = value.toLower();
        if (!sharedWithAnotherPreset && lowered != "#000000" && lowered != "#ffffff") {
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

// First stale color found in any of the sheets, named for the failure message.
QString staleColorIn(const QVector<QPair<QWidget*, QString>>& stylesheets, int activePreset) {
    for (int other = 0; other < kPresetCount; ++other) {
        if (other == activePreset) {
            continue;
        }
        for (const QString& color : exclusiveColors(other)) {
            for (const QString& spelling : colorSpellings(color)) {
                for (const auto& [widget, sheet] : stylesheets) {
                    if (sheet.contains(spelling, Qt::CaseInsensitive)) {
                        return QStringLiteral("%1 in %2 (preset %3)")
                            .arg(color, describeWidget(widget), QString::number(other));
                    }
                }
            }
        }
    }
    return QString();
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

QString staleGlyphColorIn(QWidget& root, int activePreset) {
    const QVector<QPair<QString, QSet<QRgb>>> glyphs = glyphsIn(root);
    for (int other = 0; other < kPresetCount; ++other) {
        if (other == activePreset) {
            continue;
        }
        for (const QString& color : exclusiveColors(other)) {
            const QColor target(color);
            const QRgb wanted = (static_cast<QRgb>(target.red()) << 16)
                              | (static_cast<QRgb>(target.green()) << 8)
                              | static_cast<QRgb>(target.blue());
            for (const auto& [owner, pixels] : glyphs) {
                if (pixels.contains(wanted)) {
                    return QStringLiteral("%1 is drawn in %2 (preset %3)")
                        .arg(owner, color, QString::number(other));
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
    CameraConfig::setThemePreset(0);

    // Picking a preset in the dropdown must reach every getThemeColors() caller
    // before anything is saved - that is what makes the selector feel live.
    CameraConfig::setThemePreview(5);
    QCOMPARE(CameraConfig::getThemeColors().primary, CameraConfig::getThemeColors(5).primary);
    QCOMPARE(CameraConfig::getThemeColors().bg, CameraConfig::getThemeColors(5).bg);

    // Abandoning the pick restores the saved preset.
    CameraConfig::clearThemePreview();
    QCOMPARE(CameraConfig::getThemeColors().primary, CameraConfig::getThemeColors(0).primary);
    QCOMPARE(CameraConfig::getThemePreset(), 0);
}

void ThemePresetConsistency::configDialogReThemesWhenThePresetChanges()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    CameraConfig::clearThemePreview();
    CameraConfig::setThemePreset(0);

    ConfigDialog dialog(nullptr);
    dialog.resize(1200, 800);
    dialog.show();
    QCoreApplication::processEvents();

    const QPixmap cyanLook = dialog.grab();
    QVERIFY2(!cyanLook.isNull() && cyanLook.width() > 0, "the config dialog must render");
    QVERIFY2(cyanLook.save(artifactPath(QStringLiteral("theme-0-config-dialog.png"))),
             "the preset-0 preview must be saved for review");

    // This is the dialog that holds the dropdown: it used to keep the theme it
    // was built with for the whole session.
    CameraConfig::setThemePreview(4);
    dialog.updateTheme();
    QCoreApplication::processEvents();

    const QPixmap greenLook = dialog.grab();
    QVERIFY2(greenLook.save(artifactPath(QStringLiteral("theme-4-config-dialog.png"))),
             "the preset-4 preview must be saved for review");
    QVERIFY2(cyanLook.toImage() != greenLook.toImage(),
             "switching the preset must visibly re-theme the System Configuration dialog");

    const QString stale = staleColorIn(stylesheetsIn(dialog), 4);
    QVERIFY2(stale.isEmpty(), qPrintable(QStringLiteral(
                 "the dialog still paints a previous preset's color: %1").arg(stale)));

    // A preset picked but never saved is abandoned, not left applied.
    CameraConfig::clearThemePreview();
    dialog.updateTheme();
    QCOMPARE(CameraConfig::getThemeColors().primary, CameraConfig::getThemeColors(0).primary);

    dialog.hide();
}

void ThemePresetConsistency::glyphAssetsAreTintedFromTheTheme()
{
    // The shipped arrows carry one flat fill (#E3E3E3). A stylesheet cannot
    // recolor them, so they are re-rendered in the theme's ink instead: on a
    // preset whose text is not grey, the glyph must not stay the shipped grey.
    const QString asset = QStringLiteral(":/assets/icons/arrow_up.svg");
    const QColor ink("#FFD700");

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

    // A different ink is a different glyph: presets never share one rendering.
    QVERIFY(IconManager::themedAssetFile(asset, QColor("#00FF66")) != themedFile);

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

    CameraConfig::setThemePreset(0);

    for (int preset = 0; preset < kPresetCount; ++preset) {
        CameraConfig::setThemePreview(preset);
        const ThemeColors tc = CameraConfig::getThemeColors(preset);

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

        for (const auto& [name, surface] : surfaces) {
            const QString stale = staleColorIn(stylesheetsIn(*surface), preset);
            QVERIFY2(stale.isEmpty(), qPrintable(QStringLiteral(
                         "%1 paints a previous preset's color on preset %2: %3")
                         .arg(name).arg(preset).arg(stale)));
        }

        // Visual artifacts per preset for review (chrome, not video surfaces).
        QVERIFY2(dialog.grab().save(artifactPath(
                     QStringLiteral("theme-%1-config-dialog.png").arg(preset))),
                 "the dialog preview must be saved for review");
        QVERIFY2(docs.grab().save(artifactPath(
                     QStringLiteral("theme-%1-docs.png").arg(preset))),
                 "the docs preview must be saved for review");

        // Spot-check that the preset actually reached the chrome.
        const QVector<QPair<QWidget*, QString>> dialogSheets = stylesheetsIn(dialog);
        QString chrome;
        for (const auto& [widget, sheet] : dialogSheets) {
            chrome += sheet;
        }
        QVERIFY2(chrome.contains(tc.bg, Qt::CaseInsensitive)
                     || chrome.contains(tc.primary, Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("preset %1 token missing from the dialog chrome")
                                .arg(preset)));

        docs.hide();
        changelog.hide();
        dialog.hide();
    }

    CameraConfig::clearThemePreview();
}

// The shape of one operator session: the surfaces are built once on the saved
// preset and then live through a pick in the Color Theme dropdown, which the app
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
    CameraConfig::setThemePreset(0);

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
    };

    // Mono is the preset that leaves the least room for doubt: everything the
    // switch still paints in a chromatic token names the preset it came from.
    // Then back to the original, so a one-way refresh cannot pass either.
    for (const int preset : {7, 0}) {
        CameraConfig::setThemePreview(preset);

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
        QCoreApplication::processEvents();

        for (const auto& [name, surface] : surfaces) {
            const QString staleSheet = staleColorIn(stylesheetsIn(*surface), preset);
            QVERIFY2(staleSheet.isEmpty(), qPrintable(QStringLiteral(
                         "%1 keeps a previous preset's stylesheet color after the live "
                         "switch to preset %2: %3").arg(name).arg(preset).arg(staleSheet)));

            const QString staleGlyph = staleGlyphColorIn(*surface, preset);
            QVERIFY2(staleGlyph.isEmpty(), qPrintable(QStringLiteral(
                         "%1 keeps a previous preset's glyph ink after the live "
                         "switch to preset %2: %3").arg(name).arg(preset).arg(staleGlyph)));
        }

        QVERIFY2(dialog.grab().save(artifactPath(
                     QStringLiteral("live-switch-%1-config-dialog.png").arg(preset))),
                 "the switched dialog must be saved for review");
        QVERIFY2(analysis.grab().save(artifactPath(
                     QStringLiteral("live-switch-%1-analysis-view.png").arg(preset))),
                 "the switched analysis view must be saved for review");
        QVERIFY2(detail.grab().save(artifactPath(
                     QStringLiteral("live-switch-%1-detail-view.png").arg(preset))),
                 "the switched detail view must be saved for review");
    }

    CameraConfig::clearThemePreview();
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
