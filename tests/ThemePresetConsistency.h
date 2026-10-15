#pragma once

#include <QObject>

// Every surface that paints chrome from the theme tokens must follow the
// Color Theme dropdown: the preset is applied app-wide at once (through the
// preview override) and no widget keeps another preset's colors afterwards.
class ThemePresetConsistency : public QObject {
    Q_OBJECT

private slots:
    void previewOverrideDrivesTheEffectiveTheme();
    void configDialogReThemesWhenThePresetChanges();
    void glyphAssetsAreTintedFromTheTheme();
    void noSurfaceKeepsAnotherPresetsColors();
};
