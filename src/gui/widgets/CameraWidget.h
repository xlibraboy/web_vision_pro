#pragma once

#include <QWidget>
#include <QImage>
#include <QMutex>
#include <chrono>
#include <opencv2/opencv.hpp>
#include "../../core/TemperatureStatus.h"
#include "../../config/CameraConfig.h"

class CameraWidget : public QWidget {
    Q_OBJECT

public:
    explicit CameraWidget(QWidget *parent = nullptr);
    ~CameraWidget();

    void setCameraId(int id);
    int cameraId() const;

public slots:
    void updateFrame(const cv::Mat& frame);
    void clearFrame(); // Reset to disconnected state
    
    QImage getImage();
    void setImage(const QImage& img);

signals:
    void clicked(int cameraId);
    void doubleClicked(int cameraId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QImage image_;
    QImage scaledImage_;       // Pre-scaled image — avoids O(W×H) bilinear scale on every repaint
    QMutex mutex_;
    int cameraId_ = -1;
    QString overlayText_;
    long frameCounter_ = 0;
    int lastWidth_ = -1;     // Track resolution to avoid redundant setToolTip calls
    int lastHeight_ = -1;
    // Cached theme colors — avoids repeated getThemeColors() on every repaint
    ThemeColors cachedTheme_;
    // Temperature badge
    double tempValue_ = -1.0;
    TempStatus::Status tempStatus_ = TempStatus::Unknown;

public:
    void setOverlayText(const QString& text);
    void setTemperatureStatus(double temp, TempStatus::Status status);
    double getActualDisplayFps();

private:
    static constexpr int DISPLAY_FPS_WINDOW = 30;
    std::vector<int64_t> displayTimestamps_;
    int displayTimestampIndex_ = 0;
};
