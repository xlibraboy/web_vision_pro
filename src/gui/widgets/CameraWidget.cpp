#include "CameraWidget.h"
#include "../../config/CameraConfig.h"
#include <QPainter>
#include <QDebug>

CameraWidget::CameraWidget(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent); // Optimization
    // Tooltip style uses system default
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
    
    
    
    frameCounter_++;
    if (!frame.empty()) {
       setToolTip(QString("Resolution: %1x%2\nFrame: %3")
                  .arg(frame.cols).arg(frame.rows).arg(frameCounter_));
    }
    
    update();
}

void CameraWidget::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    QMutexLocker locker(&mutex_);
    
    // Draw border first using theme color
    ThemeColors tc = CameraConfig::getThemeColors();
    painter.setPen(QPen(QColor(tc.border), 1));
    painter.drawRect(0, 0, width() - 1, height() - 1);
    
    // Content area (inside border)
    QRect contentRect = rect().adjusted(1, 1, -1, -1);
    
    if (image_.isNull()) {
        painter.fillRect(contentRect, QColor(0, 0, 0)); // Pure black
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
}

void CameraWidget::setOverlayText(const QString& text) {
    overlayText_ = text;
    update();
}
