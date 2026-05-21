#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include "../../config/CameraConfig.h"

/**
 * Custom video widget for Analysis View
 * Displays camera feed with title and timestamp overlay
 */
class AnalysisVideoWidget : public QWidget {
    Q_OBJECT

public:
    explicit AnalysisVideoWidget(int cameraId, const QString& title, QWidget *parent = nullptr);
    ~AnalysisVideoWidget() = default;
    
    void setTimestamp(const QString& timestamp, const QString& tooltip = "");
    void setFrame(const QImage& frame);
    void clear(); // Clear frame and reset to "No Signal" state
    void setPreviewThemeColors(const ThemeColors& themeColors);
    void clearPreviewThemeColors();
    void setPreviewStyle(const AnalysisViewStyle& style);
    void clearPreviewStyle();
    int getCameraId() const { return cameraId_; }

signals:
    void clicked(int cameraId);
    void doubleClicked(int cameraId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    int cameraId_;
    QString title_;
    QString timestamp_;
    QImage currentFrame_;
    bool hasPreviewThemeOverride_ = false;
    ThemeColors previewThemeOverride_;
    bool hasPreviewStyleOverride_ = false;
    AnalysisViewStyle previewStyleOverride_;
    QLabel* titleLabel_;
    QLabel* timestampLabel_;
};
