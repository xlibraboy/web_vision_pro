#pragma once

#include <QWidget>
#include <QImage>
#include <QMutex>
#include <opencv2/opencv.hpp>
#include "../../core/TemperatureStatus.h"

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
    QMutex mutex_;
    int cameraId_ = -1;
    QString overlayText_;
    long frameCounter_ = 0;
    // Temperature badge
    double tempValue_ = -1.0;
    TempStatus::Status tempStatus_ = TempStatus::Unknown;

public:
    void setOverlayText(const QString& text);
    // Set temperature reading for badge display in grid view
    void setTemperatureStatus(double temp, TempStatus::Status status);
};
