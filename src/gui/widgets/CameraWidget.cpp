#include "CameraWidget.h"
#include "../../config/CameraConfig.h"
#include <QPainter>
#include <QDebug>

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
    QPainter painter(this);
    QMutexLocker locker(&mutex_);
    
    // Cache theme colors — avoid repeated getThemeColors() calls at frame rate
    cachedTheme_ = CameraConfig::getThemeColors();
    ThemeColors& tc = cachedTheme_;
    
    // Draw border first using theme color
    painter.setPen(QPen(QColor(tc.border), 1));
    painter.drawRect(0, 0, width() - 1, height() - 1);
    
    // Content area (inside border)
    QRect contentRect = rect().adjusted(1, 1, -1, -1);
    
    if (image_.isNull()) {
        painter.fillRect(contentRect, QColor(0, 0, 0)); // Pure black
        painter.setPen(QColor(tc.text)); // Standard text color
        
        // Draw Warning Icon (Centered above text)
        QFont iconFont = painter.font();
        iconFont.setPixelSize(48);
        painter.setFont(iconFont);
        painter.drawText(contentRect.adjusted(0, -25, 0, -25), Qt::AlignCenter, "⚠");

        // Info Text
        QFont textFont = painter.font();
        textFont.setPixelSize(14);
        textFont.setBold(true);
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
            painter.setPen(QColor(tc.primary));
            painter.setFont(textFont);
            painter.drawText(contentRect.adjusted(10, 10, -10, -10), Qt::AlignLeft | Qt::AlignTop, overlayText_);
        }
        return;
    }
    
    // Scale to fit widget
    QImage scaled = image_.scaled(contentRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    
    // Center the image
    int x = contentRect.x() + (contentRect.width() - scaled.width()) / 2;
    int y = contentRect.y() + (contentRect.height() - scaled.height()) / 2;
    
    painter.fillRect(contentRect, Qt::black);
    painter.drawImage(x, y, scaled);
    // Draw overlay text if set
    if (!overlayText_.isEmpty()) {
        painter.setPen(QColor(tc.primary));  // Use theme primary/accent color
        QFont font = painter.font();
        font.setPixelSize(14); // Reduced size
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
}

void CameraWidget::setOverlayText(const QString& text) {
    overlayText_ = text;
    update();
}

void CameraWidget::setTemperatureStatus(double temp, TempStatus::Status status) {
    tempValue_  = temp;
    tempStatus_ = status;
    update();  // Repaint to show updated badge
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
