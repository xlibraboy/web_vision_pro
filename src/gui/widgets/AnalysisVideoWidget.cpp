#include "AnalysisVideoWidget.h"
#include "../../config/CameraConfig.h"
#include <QPainter>
#include <QMouseEvent>
#include <iostream>

AnalysisVideoWidget::AnalysisVideoWidget(int cameraId, const QString& title, QWidget *parent)
    : QWidget(parent), cameraId_(cameraId), title_(title), timestamp_("00:00:00.000") {
    
    setMinimumSize(160, 120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // Background is handled by QPainter in paintEvent; let the global theme set
    // the widget background via QSS inheritance.
}

void AnalysisVideoWidget::setFrame(const QImage& frame) {
    currentFrame_ = frame;
    update();
}

void AnalysisVideoWidget::setTimestamp(const QString& timestamp, const QString& tooltip) {
    timestamp_ = timestamp;
    if (!tooltip.isEmpty()) {
        setToolTip(tooltip);
    }
    update();
}

void AnalysisVideoWidget::clear() {
    currentFrame_ = QImage();
    timestamp_ = "00:00:00.000";
    update();
}

void AnalysisVideoWidget::setPreviewThemeColors(const ThemeColors& themeColors) {
    previewThemeOverride_ = themeColors;
    hasPreviewThemeOverride_ = true;
    update();
}

void AnalysisVideoWidget::clearPreviewThemeColors() {
    hasPreviewThemeOverride_ = false;
    update();
}

void AnalysisVideoWidget::setPreviewStyle(const AnalysisViewStyle& style) {
    previewStyleOverride_ = style;
    hasPreviewStyleOverride_ = true;
    update();
}

void AnalysisVideoWidget::clearPreviewStyle() {
    hasPreviewStyleOverride_ = false;
    update();
}

void AnalysisVideoWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemeColors tc = hasPreviewThemeOverride_ ? previewThemeOverride_ : CameraConfig::getThemeColors();
    const AnalysisViewStyle style = hasPreviewStyleOverride_ ? previewStyleOverride_ : CameraConfig::getAnalysisViewStyle();
    const bool lightSurface = style.playbackSurfaceStyle == "light";
    const QColor surfaceColor = lightSurface ? QColor("#F2F2F2") : QColor(Qt::black);
    const QColor titleTextColor = lightSurface ? QColor("#111111") : QColor(Qt::white);
    const QColor titleBarColor = lightSurface ? QColor(255, 255, 255, 210) : QColor(0, 0, 0, 180);
    const QColor timestampTextColor = lightSurface ? QColor(tc.primary) : QColor(0, 255, 0);
    const QColor timestampBarColor = lightSurface ? QColor(255, 255, 255, 210) : QColor(0, 0, 0, 180);
    
    // Draw background
    painter.fillRect(rect(), surfaceColor);
    
    // Draw frame if available
    if (!currentFrame_.isNull()) {
        // Scale to fill the widget while maintaining aspect ratio (letterboxing)
        QSize targetSize = size();
        QImage scaled = currentFrame_.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        
        // Center the image
        int x = (width() - scaled.width()) / 2;
        int y = (height() - scaled.height()) / 2;
        
        // Clip to widget bounds
        painter.setClipRect(rect());
        painter.drawImage(x, y, scaled);
    } 
    
    // Draw border using current theme color
    painter.setPen(QColor(tc.border));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    
    // Draw title bar at top (ALWAYS)
    QRect titleRect(0, 0, width(), 24); // Increased height slightly
    painter.setClipping(false);
    painter.fillRect(titleRect, titleBarColor);
    painter.setPen(titleTextColor);
    QFont titleFont(style.videoTitleFontFamily);
    titleFont.setPixelSize(style.videoTitleFontSize);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(titleRect.adjusted(8, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, title_);
    
    // Draw timestamp bar at bottom ONLY if we have a frame
    if (!currentFrame_.isNull()) {
        QRect tsRect(0, height() - 16, width(), 16);
        painter.fillRect(tsRect, timestampBarColor);
        painter.setPen(timestampTextColor);
        QFont timestampFont(style.timestampFontFamily);
        timestampFont.setPixelSize(style.timestampFontSize);
        painter.setFont(timestampFont);
        painter.drawText(tsRect.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignRight, timestamp_);
    }
}

void AnalysisVideoWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        std::cout << "[AnalysisVideoWidget] Clicked camera: " << cameraId_ << std::endl;
        emit clicked(cameraId_);
    }
    QWidget::mousePressEvent(event);
}
