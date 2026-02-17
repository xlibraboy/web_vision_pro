#pragma once

#include <QWidget>
#include <QImage>
#include <QMutex>
#include <opencv2/opencv.hpp>

class CameraWidget : public QWidget {
    Q_OBJECT

public:
    explicit CameraWidget(QWidget *parent = nullptr);
    ~CameraWidget();

    void setCameraId(int id);
    int cameraId() const;

public slots:
    void updateFrame(const cv::Mat& frame);

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

public:
    void setOverlayText(const QString& text);
};
