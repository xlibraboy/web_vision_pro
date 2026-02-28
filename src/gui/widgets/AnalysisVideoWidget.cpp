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

void AnalysisVideoWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw background
    painter.fillRect(rect(), Qt::black); // Black to hide letterboxing bars ("black bars")
    
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
    ThemeColors tc = CameraConfig::getThemeColors();
    painter.setPen(QColor(tc.border));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    
    // Draw title bar at top (ALWAYS)
    QRect titleRect(0, 0, width(), 24); // Increased height slightly
    painter.setClipping(false);
    painter.fillRect(titleRect, QColor(0, 0, 0, 180));
    painter.setPen(Qt::white);
    painter.setFont(QFont("Segoe UI", 10, QFont::Bold)); // Increased font size
    painter.drawText(titleRect.adjusted(8, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, title_);
    
    // Draw timestamp bar at bottom ONLY if we have a frame
    if (!currentFrame_.isNull()) {
        QRect tsRect(0, height() - 16, width(), 16);
        painter.fillRect(tsRect, QColor(0, 0, 0, 180));
        painter.setPen(QColor(0, 255, 0));  // Green timestamp
        painter.setFont(QFont("Consolas", 8));
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
