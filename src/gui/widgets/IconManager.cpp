#include "IconManager.h"
#include <QBuffer>
#include <QByteArray>

IconManager& IconManager::instance() {
    static IconManager instance;
    return instance;
}

IconManager::IconManager() : primaryColor_("#E3E3E3"), accentColor_("#00E5FF") {
    initializeIcons();
}

void IconManager::initializeIcons() {
    // Camera icon - duotone style
    iconData_[Icons::CAMERA] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z" fill="{primary}" opacity="0.2"/>
            <circle cx="12" cy="13" r="4" fill="{primary}"/>
            <path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z" stroke="{secondary}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
            <circle cx="12" cy="13" r="4" stroke="{secondary}" stroke-width="2"/>
        </svg>
    )";

    // Camera off icon
    iconData_[Icons::CAMERA_OFF] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M1 1l22 22M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z" fill="{primary}" opacity="0.2"/>
            <path d="M1 1l22 22M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z" stroke="{secondary}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
        </svg>
    )";

    // Edit icon
    iconData_[Icons::EDIT] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7" fill="{primary}" opacity="0.2"/>
            <path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z" fill="{secondary}" opacity="0.3"/>
            <path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z" stroke="{secondary}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
        </svg>
    )";

    // Trash icon
    iconData_[Icons::TRASH] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M3 6h18M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2" fill="{primary}" opacity="0.2"/>
            <path d="M3 6h18M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2m3 0v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6" stroke="{secondary}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
            <line x1="10" y1="11" x2="10" y2="17" stroke="{secondary}" stroke-width="2" stroke-linecap="round"/>
            <line x1="14" y1="11" x2="14" y2="17" stroke="{secondary}" stroke-width="2" stroke-linecap="round"/>
        </svg>
    )";

    // Check icon
    iconData_[Icons::CHECK] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <circle cx="12" cy="12" r="10" fill="{primary}" opacity="0.2"/>
            <polyline points="20 6 9 17 4 12" stroke="{secondary}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
        </svg>
    )";

    // Warning icon
    iconData_[Icons::WARNING] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z" fill="{primary}" opacity="0.2"/>
            <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z" stroke="{secondary}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
            <line x1="12" y1="9" x2="12" y2="13" stroke="{secondary}" stroke-width="2" stroke-linecap="round"/>
            <line x1="12" y1="17" x2="12.01" y2="17" stroke="{secondary}" stroke-width="2" stroke-linecap="round"/>
        </svg>
    )";

    // Error icon
    iconData_[Icons::ERROR] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <circle cx="12" cy="12" r="10" fill="{primary}" opacity="0.2"/>
            <circle cx="12" cy="12" r="10" stroke="{secondary}" stroke-width="2"/>
            <line x1="15" y1="9" x2="9" y2="15" stroke="{secondary}" stroke-width="2" stroke-linecap="round"/>
            <line x1="9" y1="9" x2="15" y2="15" stroke="{secondary}" stroke-width="2" stroke-linecap="round"/>
        </svg>
    )";

    // Info icon
    iconData_[Icons::INFO] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <circle cx="12" cy="12" r="10" fill="{primary}" opacity="0.2"/>
            <circle cx="12" cy="12" r="10" stroke="{secondary}" stroke-width="2"/>
            <line x1="12" y1="16" x2="12" y2="12" stroke="{secondary}" stroke-width="2" stroke-linecap="round"/>
            <line x1="12" y1="8" x2="12.01" y2="8" stroke="{secondary}" stroke-width="2" stroke-linecap="round"/>
        </svg>
    )";

    // Network icon
    iconData_[Icons::NETWORK] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <rect x="2" y="2" width="20" height="8" rx="2" fill="{primary}" opacity="0.2"/>
            <rect x="2" y="14" width="20" height="8" rx="2" fill="{primary}" opacity="0.2"/>
            <path d="M6 6h12M6 18h12" stroke="{secondary}" stroke-width="2" stroke-linecap="round"/>
            <circle cx="6" cy="6" r="1" fill="{secondary}"/>
            <circle cx="6" cy="18" r="1" fill="{secondary}"/>
        </svg>
    )";

    // Settings icon
    iconData_[Icons::SETTINGS] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <circle cx="12" cy="12" r="3" fill="{primary}" opacity="0.2"/>
            <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z" stroke="{secondary}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
            <circle cx="12" cy="12" r="3" stroke="{secondary}" stroke-width="2"/>
        </svg>
    )";

    // Expand icon
    iconData_[Icons::EXPAND] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <polyline points="6 9 12 15 18 9" stroke="{secondary}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
        </svg>
    )";

    // Collapse icon
    iconData_[Icons::COLLAPSE] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <polyline points="18 15 12 9 6 15" stroke="{secondary}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
        </svg>
    )";

    // Refresh icon
    iconData_[Icons::REFRESH] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M23 4v6h-6M1 20v-6h6" fill="{primary}" opacity="0.2"/>
            <path d="M23 4v6h-6M1 20v-6h6" stroke="{secondary}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
            <path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15" stroke="{secondary}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
        </svg>
    )";

    // Add icon
    iconData_[Icons::ADD] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <circle cx="12" cy="12" r="10" fill="{primary}" opacity="0.2"/>
            <line x1="12" y1="8" x2="12" y2="16" stroke="{secondary}" stroke-width="2" stroke-linecap="round"/>
            <line x1="8" y1="12" x2="16" y2="12" stroke="{secondary}" stroke-width="2" stroke-linecap="round"/>
        </svg>
    )";

    // Save icon
    iconData_[Icons::SAVE] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z" fill="{primary}" opacity="0.2"/>
            <polyline points="17 21 17 13 7 13 7 21" stroke="{secondary}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
            <polyline points="7 3 7 8 15 8" stroke="{secondary}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
        </svg>
    )";

    // Close icon
    iconData_[Icons::CLOSE] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <line x1="18" y1="6" x2="6" y2="18" stroke="{secondary}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
            <line x1="6" y1="6" x2="18" y2="18" stroke="{secondary}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
        </svg>
    )";

    // IP Address icon
    iconData_[Icons::IP_ADDRESS] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <rect x="2" y="6" width="20" height="12" rx="2" fill="{primary}" opacity="0.2"/>
            <rect x="2" y="6" width="20" height="12" rx="2" stroke="{secondary}" stroke-width="2"/>
            <line x1="6" y1="10" x2="6" y2="10" stroke="{secondary}" stroke-width="2" stroke-linecap="round"/>
            <line x1="10" y1="10" x2="10" y2="10" stroke="{secondary}" stroke-width="2" stroke-linecap="round"/>
            <line x1="14" y1="10" x2="14" y2="10" stroke="{secondary}" stroke-width="2" stroke-linecap="round"/>
            <line x1="18" y1="10" x2="18" y2="10" stroke="{secondary}" stroke-width="2" stroke-linecap="round"/>
        </svg>
    )";

    // MAC Address icon
    iconData_[Icons::MAC_ADDRESS] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <rect x="3" y="4" width="18" height="16" rx="2" fill="{primary}" opacity="0.2"/>
            <rect x="3" y="4" width="18" height="16" rx="2" stroke="{secondary}" stroke-width="2"/>
            <path d="M16 2v4M8 2v4M3 10h18" stroke="{secondary}" stroke-width="2" stroke-linecap="round"/>
        </svg>
    )";

    // Speed icon
    iconData_[Icons::SPEED] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <circle cx="12" cy="12" r="10" fill="{primary}" opacity="0.2"/>
            <circle cx="12" cy="12" r="10" stroke="{secondary}" stroke-width="2"/>
            <path d="M12 6v6l4 2" stroke="{secondary}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
        </svg>
    )";

    // Location icon
    iconData_[Icons::LOCATION] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M21 10c0 7-9 13-9 13s-9-6-9-13a9 9 0 0 1 18 0z" fill="{primary}" opacity="0.2"/>
            <circle cx="12" cy="10" r="3" fill="{secondary}" opacity="0.5"/>
            <path d="M21 10c0 7-9 13-9 13s-9-6-9-13a9 9 0 0 1 18 0z" stroke="{secondary}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
            <circle cx="12" cy="10" r="3" stroke="{secondary}" stroke-width="2"/>
        </svg>
    )";

    // Thermometer icon
    iconData_[Icons::THERMOMETER] = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M14 14.76V3.5a2.5 2.5 0 0 0-5 0v11.26a4.5 4.5 0 1 0 5 0z" fill="{primary}" opacity="0.2"/>
            <path d="M14 14.76V3.5a2.5 2.5 0 0 0-5 0v11.26a4.5 4.5 0 1 0 5 0z" stroke="{secondary}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
            <path d="M12 18a2.5 2.5 0 0 1 0-5" stroke="{secondary}" stroke-width="2" stroke-linecap="round"/>
        </svg>
    )";
}

QPixmap IconManager::renderSvg(const QString& svgData, int size,
                               const QColor& primary, const QColor& secondary) {
    QString processedSvg = svgData;
    processedSvg.replace("{primary}", primary.name());
    processedSvg.replace("{secondary}", secondary.name());

    QSvgRenderer renderer(processedSvg.toUtf8());
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    renderer.render(&painter);
    painter.end();

    return pixmap;
}

QIcon IconManager::getIcon(const QString& name, int size,
                           const QColor& primaryColor, const QColor& secondaryColor) {
    QIcon icon;

    if (!iconData_.contains(name)) {
        return icon;
    }

    const QString& svgData = iconData_[name];

    // Add pixmap for normal state
    QPixmap normalPixmap = renderSvg(svgData, size, primaryColor, secondaryColor);
    icon.addPixmap(normalPixmap, QIcon::Normal, QIcon::Off);

    // Add pixmap for disabled state (grayscale)
    QPixmap disabledPixmap = normalPixmap;
    QPainter painter(&disabledPixmap);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.fillRect(disabledPixmap.rect(), QColor(128, 128, 128, 128));
    painter.end();
    icon.addPixmap(disabledPixmap, QIcon::Disabled, QIcon::Off);

    return icon;
}

QPixmap IconManager::getPixmap(const QString& name, int size,
                               const QColor& primaryColor, const QColor& secondaryColor) {
    if (!iconData_.contains(name)) {
        return QPixmap();
    }
    return renderSvg(iconData_[name], size, primaryColor, secondaryColor);
}

// Predefined icon getters
QIcon IconManager::camera(int size) {
    return getIcon(Icons::CAMERA, size, primaryColor_, accentColor_);
}

QIcon IconManager::cameraOff(int size) {
    return getIcon(Icons::CAMERA_OFF, size, primaryColor_, QColor("#888888"));
}

QIcon IconManager::edit(int size) {
    return getIcon(Icons::EDIT, size, primaryColor_, accentColor_);
}

QIcon IconManager::trash(int size) {
    return getIcon(Icons::TRASH, size, primaryColor_, QColor("#FF5A5A"));
}

QIcon IconManager::check(int size) {
    return getIcon(Icons::CHECK, size, primaryColor_, QColor("#4CAF50"));
}

QIcon IconManager::warning(int size) {
    return getIcon(Icons::WARNING, size, primaryColor_, QColor("#E0A800"));
}

QIcon IconManager::error(int size) {
    return getIcon(Icons::ERROR, size, primaryColor_, QColor("#FF5A5A"));
}

QIcon IconManager::info(int size) {
    return getIcon(Icons::INFO, size, primaryColor_, accentColor_);
}

QIcon IconManager::network(int size) {
    return getIcon(Icons::NETWORK, size, primaryColor_, accentColor_);
}

QIcon IconManager::settings(int size) {
    return getIcon(Icons::SETTINGS, size, primaryColor_, accentColor_);
}

QIcon IconManager::expand(int size) {
    return getIcon(Icons::EXPAND, size, primaryColor_, accentColor_);
}

QIcon IconManager::collapse(int size) {
    return getIcon(Icons::COLLAPSE, size, primaryColor_, accentColor_);
}

QIcon IconManager::refresh(int size) {
    return getIcon(Icons::REFRESH, size, primaryColor_, accentColor_);
}

QIcon IconManager::add(int size) {
    return getIcon(Icons::ADD, size, primaryColor_, QColor("#4CAF50"));
}

QIcon IconManager::save(int size) {
    return getIcon(Icons::SAVE, size, primaryColor_, accentColor_);
}

QIcon IconManager::close(int size) {
    return getIcon(Icons::CLOSE, size, primaryColor_, QColor("#FF5A5A"));
}

QIcon IconManager::ipAddress(int size) {
    return getIcon(Icons::IP_ADDRESS, size, primaryColor_, accentColor_);
}

QIcon IconManager::macAddress(int size) {
    return getIcon(Icons::MAC_ADDRESS, size, primaryColor_, accentColor_);
}

QIcon IconManager::speed(int size) {
    return getIcon(Icons::SPEED, size, primaryColor_, accentColor_);
}

QIcon IconManager::location(int size) {
    return getIcon(Icons::LOCATION, size, primaryColor_, accentColor_);
}

QIcon IconManager::thermometer(int size) {
    return getIcon(Icons::THERMOMETER, size, primaryColor_, accentColor_);
}

// Status-aware icons
QIcon IconManager::statusOk(int size) {
    return check(size);
}

QIcon IconManager::statusWarning(int size) {
    return warning(size);
}

QIcon IconManager::statusError(int size) {
    return error(size);
}

QIcon IconManager::statusOffline(int size) {
    return cameraOff(size);
}

void IconManager::setThemeColors(const QColor& primary, const QColor& accent) {
    primaryColor_ = primary;
    accentColor_ = accent;
}
