#pragma once

#include <QObject>

// Every surface that paints chrome from the theme tokens must follow the Theme
// dropdown: the theme is applied app-wide at once (through the preview
// override) and no widget keeps the other theme's colors afterwards.
class ThemePresetConsistency : public QObject {
    Q_OBJECT

private slots:
    void previewOverrideDrivesTheEffectiveTheme();
    void themeModesResolveAndMigrate();
    void videoSurfacesFollowTheTheme();
    void configDialogReThemesWhenThePresetChanges();
    void glyphAssetsAreTintedFromTheTheme();
    void noSurfaceKeepsAnotherPresetsColors();
    void liveSwitchReTintsStylesheetsAndGlyphs();
};
