#pragma once

#include <QWidget>
#include <QFont>
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
    // PTP (IEEE 1588) state badge
    bool ptpAvailable_ = false;
    bool ptpEnabled_ = false;
    bool ptpLocked_ = false;
    QString ptpState_;
    int64_t ptpOffsetFromMasterNs_ = -1;

public:
    void setOverlayText(const QString& text);
    void setOverlayFont(const QFont& font);
    void setTemperatureStatus(double temp, TempStatus::Status status);
    void setPtpStatus(bool available, bool enabled, const QString& state,
                      bool locked, int64_t offsetFromMasterNs);
    void setPreviewThemeColors(const ThemeColors& themeColors);
    void clearPreviewThemeColors();
    void setPreviewBackgroundStyle(const QString& backgroundStyle);
    void clearPreviewBackgroundStyle();
    double getActualDisplayFps();

private:
    QFont overlayFont_;
    bool hasPreviewThemeOverride_ = false;
    ThemeColors previewThemeOverride_;
    bool hasPreviewBackgroundOverride_ = false;
    QString previewBackgroundStyleOverride_;
    static constexpr int DISPLAY_FPS_WINDOW = 30;
    std::vector<int64_t> displayTimestamps_;
    int displayTimestampIndex_ = 0;
};
