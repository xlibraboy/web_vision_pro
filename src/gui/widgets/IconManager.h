#pragma once

#include <QIcon>
#include <QPixmap>
#include <QColor>
#include <QMap>
#include <QString>
#include <QPainter>
#include <QSvgRenderer>

// Icon names for the duotone icon system
namespace Icons {
    constexpr const char* CAMERA = "camera";
    constexpr const char* CAMERA_OFF = "camera_off";
    constexpr const char* EDIT = "edit";
    constexpr const char* TRASH = "trash";
    constexpr const char* CHECK = "check";
    constexpr const char* WARNING = "warning";
    constexpr const char* ERROR = "error";
    constexpr const char* INFO = "info";
    constexpr const char* NETWORK = "network";
    constexpr const char* SETTINGS = "settings";
    constexpr const char* EXPAND = "expand";
    constexpr const char* COLLAPSE = "collapse";
    constexpr const char* REFRESH = "refresh";
    constexpr const char* ADD = "add";
    constexpr const char* SAVE = "save";
    constexpr const char* CLOSE = "close";
    constexpr const char* IP_ADDRESS = "ip_address";
    constexpr const char* MAC_ADDRESS = "mac_address";
    constexpr const char* SPEED = "speed";
    constexpr const char* LOCATION = "location";
    constexpr const char* THERMOMETER = "thermometer";
    constexpr const char* USER = "user";
}

/**
 * @brief IconManager - Provides a duotone icon system for the application
 *
 * This class manages SVG-based icons with duotone styling (primary and secondary colors).
 * Icons are rendered on-demand with theme-aware coloring.
 */
class IconManager {
public:
    static IconManager& instance();

    // Get an icon with specified size and colors. Passing an invalid QColor
    // (the default) resolves that plane from the active theme: text for the
    // primary plane, accent for the secondary plane.
    QIcon getIcon(const QString& name, int size = 20,
                  const QColor& primaryColor = QColor(),
                  const QColor& secondaryColor = QColor());

    QPixmap getPixmap(const QString& name, int size = 20,
                      const QColor& primaryColor = QColor(),
                      const QColor& secondaryColor = QColor());

    // Predefined icon getters with theme-aware colors
    QIcon camera(int size = 20);
    QIcon cameraOff(int size = 20);
    QIcon edit(int size = 20);
    QIcon trash(int size = 20);
    QIcon check(int size = 20);
    QIcon warning(int size = 20);
    QIcon error(int size = 20);
    QIcon info(int size = 20);
    QIcon network(int size = 20);
    QIcon settings(int size = 20);
    QIcon expand(int size = 20);
    QIcon collapse(int size = 20);
    QIcon refresh(int size = 20);
    QIcon add(int size = 20);
    QIcon save(int size = 20);
    QIcon close(int size = 20);
    QIcon ipAddress(int size = 20);
    QIcon macAddress(int size = 20);
    QIcon speed(int size = 20);
    QIcon location(int size = 20);
    QIcon thermometer(int size = 20);
    QIcon user(int size = 20);

    // Status-aware icons
    QIcon statusOk(int size = 20);
    QIcon statusWarning(int size = 20);
    QIcon statusError(int size = 20);
    QIcon statusOffline(int size = 20);

    // The shipped SVG assets carry a fixed fill, which neither a stylesheet nor
    // a plain QIcon can recolor. This renders a themed copy: as a pixmap for
    // icons, or as a cached file path for `image: url(...)` rules. Both fall
    // back to the asset itself when it cannot be re-rendered.
    static QPixmap themedAssetPixmap(const QString& assetPath, const QColor& color, int size);
    static QString themedAssetFile(const QString& assetPath, const QColor& color);

private:
    IconManager();
    ~IconManager() = default;
    IconManager(const IconManager&) = delete;
    IconManager& operator=(const IconManager&) = delete;

    // SVG icon data storage
    QMap<QString, QString> iconData_;

    // Initialize built-in icons
    void initializeIcons();

    // Render SVG to pixmap
    QPixmap renderSvg(const QString& svgData, int size,
                      const QColor& primary, const QColor& secondary);
};
