#include "CameraWidget.h"
#include "../../config/CameraConfig.h"
#include <algorithm>
#include <QFont>
#include <QPainter>
#include <QBrush>
#include <QDebug>

namespace {
QColor liveViewBaseBackground(const QString& backgroundStyle) {
    if (backgroundStyle == "white" || backgroundStyle.startsWith("white_")) {
        return QColor("#F2F2F2");
    }

    return QColor("#000000");
}

bool usesTexturedBackground(const QString& backgroundStyle) {
    return backgroundStyle == "textured" ||
           backgroundStyle == "white_textured" ||
           backgroundStyle == "textured_grid" ||
           backgroundStyle == "white_textured_grid" ||
           backgroundStyle == "textured_mesh" ||
           backgroundStyle == "white_textured_mesh" ||
           backgroundStyle == "textured_diagonal" ||
           backgroundStyle == "white_textured_diagonal" ||
           backgroundStyle == "textured_dots" ||
           backgroundStyle == "white_textured_dots";
}

bool usesLightBackground(const QString& backgroundStyle) {
    return backgroundStyle == "white" || backgroundStyle.startsWith("white_");
}

QString normalizeLiveViewBackgroundStyle(const QString& backgroundStyle) {
    if (backgroundStyle == "textured" ||
        backgroundStyle == "textured_grid" ||
        backgroundStyle == "textured_diagonal" ||
        backgroundStyle == "textured_dots") {
        return "textured_mesh";
    }

    if (backgroundStyle == "white_textured" ||
        backgroundStyle == "white_textured_grid" ||
        backgroundStyle == "white_textured_diagonal" ||
        backgroundStyle == "white_textured_dots") {
        return "white_textured_mesh";
    }

    return backgroundStyle;
}

QBrush liveViewBackgroundBrush(const QString& backgroundStyle) {
    const QString normalizedStyle = normalizeLiveViewBackgroundStyle(backgroundStyle);

    if (normalizedStyle == "textured" || normalizedStyle == "white_textured") {
        QPixmap texture(24, 24);
        const bool isWhite = normalizedStyle == "white_textured";
        texture.fill(isWhite ? QColor("#F4F4F4") : QColor("#101010"));

        QPainter texturePainter(&texture);
        texturePainter.setPen(QPen(isWhite ? QColor("#D6D6D6") : QColor("#1F1F1F"), 1));
        texturePainter.drawLine(0, 12, 24, 12);
        texturePainter.drawLine(12, 0, 12, 24);
        texturePainter.setPen(QPen(isWhite ? QColor("#E2E2E2") : QColor("#181818"), 1));
        texturePainter.drawLine(0, 0, 24, 24);
        texturePainter.drawLine(24, 0, 0, 24);
        texturePainter.end();

        return QBrush(texture);
    }

    if (normalizedStyle == "textured_grid" || normalizedStyle == "white_textured_grid") {
        QPixmap texture(20, 20);
        const bool isWhite = normalizedStyle == "white_textured_grid";
        texture.fill(isWhite ? QColor("#F6F6F6") : QColor("#0D0D0D"));

        QPainter texturePainter(&texture);
        texturePainter.setPen(QPen(isWhite ? QColor("#DCDCDC") : QColor("#1B1B1B"), 1));
        for (int offset = 0; offset <= 20; offset += 5) {
            texturePainter.drawLine(offset, 0, offset, 20);
            texturePainter.drawLine(0, offset, 20, offset);
        }
        texturePainter.setPen(QPen(isWhite ? QColor("#CFCFCF") : QColor("#141414"), 1));
        texturePainter.drawPoint(10, 10);
        texturePainter.end();

        return QBrush(texture);
    }

    if (normalizedStyle == "textured_mesh" || normalizedStyle == "white_textured_mesh") {
        QPixmap texture(28, 28);
        const bool isWhite = normalizedStyle == "white_textured_mesh";
        texture.fill(isWhite ? QColor("#F3F3F3") : QColor("#0E0E0E"));

        QPainter texturePainter(&texture);
        texturePainter.setPen(QPen(isWhite ? QColor("#D8D8D8") : QColor("#1F1F1F"), 1));
        for (int offset = 0; offset <= 28; offset += 7) {
            texturePainter.drawLine(offset, 0, offset, 28);
            texturePainter.drawLine(0, offset, 28, offset);
        }
        texturePainter.setPen(QPen(isWhite ? QColor("#E6E6E6") : QColor("#181818"), 1));
        texturePainter.drawLine(0, 0, 28, 28);
        texturePainter.drawLine(28, 0, 0, 28);
        texturePainter.end();

        return QBrush(texture);
    }

    if (normalizedStyle == "textured_diagonal" || normalizedStyle == "white_textured_diagonal") {
        QPixmap texture(24, 24);
        const bool isWhite = normalizedStyle == "white_textured_diagonal";
        texture.fill(isWhite ? QColor("#F5F5F5") : QColor("#0C0C0C"));

        QPainter texturePainter(&texture);
        texturePainter.setPen(QPen(isWhite ? QColor("#D6D6D6") : QColor("#202020"), 2));
        texturePainter.drawLine(-6, 24, 12, 0);
        texturePainter.drawLine(6, 24, 24, 0);
        texturePainter.drawLine(18, 24, 30, 8);
        texturePainter.setPen(QPen(isWhite ? QColor("#E2E2E2") : QColor("#151515"), 1));
        texturePainter.drawLine(0, 24, 18, 0);
        texturePainter.drawLine(12, 24, 24, 8);
        texturePainter.end();

        return QBrush(texture);
    }

    if (normalizedStyle == "textured_dots" || normalizedStyle == "white_textured_dots") {
        QPixmap texture(20, 20);
        const bool isWhite = normalizedStyle == "white_textured_dots";
        texture.fill(isWhite ? QColor("#F7F7F7") : QColor("#0F0F0F"));

        QPainter texturePainter(&texture);
        texturePainter.setPen(Qt::NoPen);
        texturePainter.setBrush(isWhite ? QColor("#D8D8D8") : QColor("#232323"));
        texturePainter.drawEllipse(QRectF(3, 3, 3, 3));
        texturePainter.drawEllipse(QRectF(13, 3, 3, 3));
        texturePainter.drawEllipse(QRectF(8, 8, 4, 4));
        texturePainter.drawEllipse(QRectF(3, 13, 3, 3));
        texturePainter.drawEllipse(QRectF(13, 13, 3, 3));
        texturePainter.setBrush(isWhite ? QColor("#E6E6E6") : QColor("#191919"));
        texturePainter.drawEllipse(QRectF(1, 8, 2, 2));
        texturePainter.drawEllipse(QRectF(17, 8, 2, 2));
        texturePainter.end();

        return QBrush(texture);
    }

    return QBrush(liveViewBaseBackground(backgroundStyle));
}
}

CameraWidget::CameraWidget(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    displayTimestamps_.resize(DISPLAY_FPS_WINDOW, 0);
}

CameraWidget::~CameraWidget() {}

void CameraWidget::setCameraId(int id) {
    cameraId_ = id;
}

int CameraWidget::cameraId() const {
    return cameraId_;
}

void CameraWidget::mousePressEvent(QMouseEvent *event) {
    emit clicked(cameraId_);
    QWidget::mousePressEvent(event);
}

void CameraWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    emit doubleClicked(cameraId_);
    QWidget::mouseDoubleClickEvent(event);
}

void CameraWidget::updateFrame(const cv::Mat& frame) {
    if (frame.empty()) return;

    QMutexLocker locker(&mutex_);
    
    // Check format and convert
    if (frame.channels() == 3) {
        cv::Mat rgb;
        cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
        image_ = QImage((const unsigned char*)(rgb.data), 
                        rgb.cols, rgb.rows, 
                        rgb.step, 
                        QImage::Format_RGB888).copy();
    } else if (frame.channels() == 1) {
        image_ = QImage((const unsigned char*)(frame.data), 
                        frame.cols, frame.rows, 
                        frame.step, 
                        QImage::Format_Grayscale8).copy();
    }
    
    // Record timestamp for Display FPS calculation
    displayTimestamps_[displayTimestampIndex_] = 
        std::chrono::steady_clock::now().time_since_epoch().count();
    displayTimestampIndex_ = (displayTimestampIndex_ + 1) % DISPLAY_FPS_WINDOW;
    
    frameCounter_++;
    // Only update tooltip if resolution actually changed (avoids Qt tooltip-internal overhead per frame)
    if (frame.cols != lastWidth_ || frame.rows != lastHeight_) {
        lastWidth_ = frame.cols;
        lastHeight_ = frame.rows;
        setToolTip(QString("Resolution: %1x%2\nFrame: %3")
                   .arg(frame.cols).arg(frame.rows).arg(frameCounter_));
    }
    
    update();
}

void CameraWidget::clearFrame() {
    QMutexLocker locker(&mutex_);
    image_ = QImage(); // Reset to null
    update();
}

void CameraWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    QMutexLocker locker(&mutex_);
    
    // Cache theme colors — avoid repeated getThemeColors() calls at frame rate
    cachedTheme_ = hasPreviewThemeOverride_ ? previewThemeOverride_ : CameraConfig::getThemeColors();
    ThemeColors& tc = cachedTheme_;
    const LiveViewCardStyle cardStyle = CameraConfig::getLiveViewCardStyle();
    const QString effectiveBackgroundStyle = hasPreviewBackgroundOverride_
        ? previewBackgroundStyleOverride_
        : cardStyle.backgroundStyle;
    const bool lightBackground = usesLightBackground(effectiveBackgroundStyle);
    const QColor overlayColor = lightBackground ? QColor("#1A1A1A") : QColor(tc.primary);
    const QColor messageColor = lightBackground ? QColor("#1A1A1A") : QColor(tc.text);
    const QBrush backgroundBrush = liveViewBackgroundBrush(effectiveBackgroundStyle);
    const QColor baseBackgroundColor = liveViewBaseBackground(effectiveBackgroundStyle);

    auto makeFont = [&](const QString& family, int pixelSize, bool bold = false) {
        QFont font(family);
        font.setPixelSize(pixelSize);
        font.setBold(bold);
        return font;
    };

    auto overlayFont = [&]() {
        QFont font = overlayFont_;
        if (font.family().isEmpty()) {
            font = makeFont(cardStyle.gridTitleFontFamily, cardStyle.gridTitleFontSize, true);
        }
        return font;
    };
    
    // Draw border first using theme color
    painter.setPen(QPen(QColor(tc.border), 1));
    painter.drawRect(0, 0, width() - 1, height() - 1);
    
    // Content area (inside border)
    QRect contentRect = rect().adjusted(1, 1, -1, -1);
    
    if (image_.isNull()) {
        painter.fillRect(contentRect, backgroundBrush);
        painter.setPen(messageColor);
        
        // Draw Warning Icon (Centered above text)
        QFont iconFont = makeFont(cardStyle.gridTitleFontFamily, std::max(cardStyle.gridTitleFontSize + 34, 28), false);
        painter.setFont(iconFont);
        painter.drawText(contentRect.adjusted(0, -25, 0, -25), Qt::AlignCenter, "⚠");

        // Info Text
    QFont textFont = makeFont(cardStyle.gridTitleFontFamily, cardStyle.gridTitleFontSize, true);
        painter.setFont(textFont);
        QString msg;
        if (cameraId_ >= 0) {
            msg = QString("%1\nWaiting for physical connection...").arg(CameraConfig::getCameraName(cameraId_));
        } else {
            msg = "Waiting for physical connection...";
        }
        painter.drawText(contentRect.adjusted(0, 45, 0, 45), Qt::AlignCenter, msg);
        
        // Draw overlay text if set even when disconnected
        if (!overlayText_.isEmpty()) {
            painter.setPen(overlayColor);
            painter.setFont(overlayFont());
            painter.drawText(contentRect.adjusted(10, 10, -10, -10), Qt::AlignLeft | Qt::AlignTop, overlayText_);
        }
        return;
    }
    
    // Scale to fit widget
    QImage scaled = image_.scaled(contentRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    
    // Center the image
    int x = contentRect.x() + (contentRect.width() - scaled.width()) / 2;
    int y = contentRect.y() + (contentRect.height() - scaled.height()) / 2;
    
    painter.fillRect(contentRect, backgroundBrush);
    if (effectiveBackgroundStyle == "white" || usesTexturedBackground(effectiveBackgroundStyle)) {
        painter.fillRect(contentRect, QColor(baseBackgroundColor.red(), baseBackgroundColor.green(), baseBackgroundColor.blue(), 28));
    }
    painter.drawImage(x, y, scaled);
    // Draw overlay text if set
    if (!overlayText_.isEmpty()) {
        painter.setPen(overlayColor);
        QFont font = overlayFont();
        painter.setFont(font);
        
        // Draw at top-left with some padding
        painter.drawText(contentRect.adjusted(10, 10, -10, -10), Qt::AlignLeft | Qt::AlignTop, overlayText_);
    }

    // === Temperature Badge (top-right corner) ===
    // Only drawn for Critical or Error states (Ok/Unknown need no badge)
    if (tempStatus_ == TempStatus::Critical ||
        tempStatus_ == TempStatus::Error) {

        QColor badgeColor = (tempStatus_ == TempStatus::Error)
                            ? QColor("#ff4444")  // Red for Error
                            : QColor("#ff9900"); // Orange for Critical

        QString tempStr = (tempValue_ >= 0)
                          ? QString("%1°C").arg(tempValue_, 0, 'f', 0)
                          : "TEMP!";

        QFont badgeFont = painter.font();
        badgeFont.setPixelSize(11);
        badgeFont.setBold(true);
        painter.setFont(badgeFont);

        QFontMetrics fm(badgeFont);
        int textW = fm.horizontalAdvance(tempStr) + 8;
        int textH = fm.height() + 4;
        QRect badgeRect(contentRect.right() - textW - 5,
                        contentRect.top() + 5,
                        textW, textH);

        // Badge background
        painter.setBrush(badgeColor);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(badgeRect, 3, 3);

        // Badge text
        painter.setPen(Qt::white);
        painter.drawText(badgeRect, Qt::AlignCenter, tempStr);
    }

    // === PTP State Badge (bottom-left corner) ===
    // Compact readout of the camera's IEEE 1588 clock state. Shown whenever the
    // camera exposes PTP nodes: the role (SLAVE/MASTER), a dim OFF state, or an
    // amber SYNC while the clock is still settling. Hidden on emulation/no-PTP.
    if (ptpAvailable_) {
        QString ptpText;
        QColor ptpColor;
        if (!ptpEnabled_ || ptpState_.isEmpty()) {
            ptpText = "PTP OFF";
            ptpColor = QColor(140, 140, 140);
        } else if (ptpState_ == QLatin1String("Slave")) {
            ptpText = "PTP SLAVE";
            ptpColor = QColor("#2EA043");
        } else if (ptpState_ == QLatin1String("Master")) {
            ptpText = "PTP MASTER";
            ptpColor = QColor("#58A6FF");
        } else if (ptpState_ == QLatin1String("Faulty")
                   || ptpState_ == QLatin1String("Disabled")) {
            ptpText = (ptpState_ == QLatin1String("Faulty")) ? "PTP FAULT" : "PTP OFF";
            ptpColor = (ptpState_ == QLatin1String("Faulty")) ? QColor("#ff4444") : QColor(140, 140, 140);
        } else {
            ptpText = "PTP SYNC";  // Initializing / Listening / Passive / ...
            ptpColor = QColor("#ff9900");
        }

        QFont ptpFont = painter.font();
        ptpFont.setPixelSize(10);
        ptpFont.setBold(true);
        painter.setFont(ptpFont);

        QFontMetrics ptpFm(ptpFont);
        int ptpW = ptpFm.horizontalAdvance(ptpText) + 8;
        int ptpH = ptpFm.height() + 4;
        QRect ptpBadgeRect(contentRect.left() + 5,
                           contentRect.bottom() - ptpH - 5,
                           ptpW, ptpH);

        painter.setBrush(ptpColor);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(ptpBadgeRect, 3, 3);
        painter.setPen(Qt::white);
        painter.drawText(ptpBadgeRect, Qt::AlignCenter, ptpText);
    }
}

void CameraWidget::setOverlayText(const QString& text) {
    overlayText_ = text;
    update();
}

void CameraWidget::setOverlayFont(const QFont& font) {
    overlayFont_ = font;
    update();
}

void CameraWidget::setTemperatureStatus(double temp, TempStatus::Status status) {
    tempValue_  = temp;
    tempStatus_ = status;
    update();  // Repaint to show updated badge
}

void CameraWidget::setPtpStatus(bool available, bool enabled, const QString& state,
                                bool locked, int64_t offsetFromMasterNs) {
    ptpAvailable_ = available;
    ptpEnabled_ = enabled;
    ptpState_ = state;
    ptpLocked_ = locked;
    ptpOffsetFromMasterNs_ = offsetFromMasterNs;
    update();  // Repaint to show updated badge
}

void CameraWidget::setPreviewThemeColors(const ThemeColors& themeColors) {
    previewThemeOverride_ = themeColors;
    hasPreviewThemeOverride_ = true;
    update();
}

void CameraWidget::clearPreviewThemeColors() {
    hasPreviewThemeOverride_ = false;
    update();
}

void CameraWidget::setPreviewBackgroundStyle(const QString& backgroundStyle) {
    previewBackgroundStyleOverride_ = backgroundStyle;
    hasPreviewBackgroundOverride_ = true;
    update();
}

void CameraWidget::clearPreviewBackgroundStyle() {
    hasPreviewBackgroundOverride_ = false;
    previewBackgroundStyleOverride_.clear();
    update();
}

QImage CameraWidget::getImage() {
    QMutexLocker locker(&mutex_);
    return image_.copy();
}

void CameraWidget::setImage(const QImage& img) {
    QMutexLocker locker(&mutex_);
    image_ = img.copy();
    update();
}

double CameraWidget::getActualDisplayFps() {
    QMutexLocker locker(&mutex_);
    
    // Find first and last valid (non-zero) timestamps
    int64_t firstTs = 0;
    int64_t lastTs = 0;
    int count = 0;
    
    for (int i = 0; i < DISPLAY_FPS_WINDOW; ++i) {
        if (displayTimestamps_[i] != 0) {
            if (firstTs == 0) firstTs = displayTimestamps_[i];
            lastTs = displayTimestamps_[i];
            count++;
        }
    }
    
    if (count < 2) return -1.0;
    
    double elapsedNs = static_cast<double>(lastTs - firstTs);
    if (elapsedNs <= 0) return -1.0;
    
    return (count - 1) / (elapsedNs / 1e9);
}
