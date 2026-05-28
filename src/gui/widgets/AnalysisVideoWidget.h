#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QVector>
#include <QPoint>
#include <QPointF>
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
    void setTitle(const QString& title);
    void setFrame(const QImage& frame);
    void clear(); // Clear frame and reset to "No Signal" state
    void setPreviewThemeColors(const ThemeColors& themeColors);
    void clearPreviewThemeColors();
    void setPreviewStyle(const AnalysisViewStyle& style);
    void clearPreviewStyle();
    void setMarkerToolEnabled(bool enabled);
    void setMarkerShape(const QString& shape);
    void setAnnotationEditable(bool editable);
    void setZoomFactor(double factor);
    void setBrightnessOffset(int offset);
    void resetImageTools();
    void setAnnotation(const QString& shape, const QVector<QPoint>& points);
    void setAnnotationNormalized(const QString& shape, const QVector<QPointF>& points);
    void clearAnnotation();
    int getCameraId() const { return cameraId_; }

signals:
    void clicked(int cameraId);
    void doubleClicked(int cameraId);
    void annotationChanged(int cameraId, const QString& shape, const QVector<QPoint>& points);
    void annotationChangedNormalized(int cameraId, const QString& shape, const QVector<QPointF>& points);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    int cameraId_;
    QString title_;
    QString timestamp_;
    QImage currentFrame_;
    QImage scaledFrameCache_;
    QSize scaledFrameCacheWidgetSize_;
    QSize scaledFrameCacheSourceSize_;
    double scaledFrameCacheZoom_ = 0.0;
    QImage markerStrokeOverlay_;
    QPoint lastStrokePoint_;
    bool hasPreviewThemeOverride_ = false;
    ThemeColors previewThemeOverride_;
    bool hasPreviewStyleOverride_ = false;
    AnalysisViewStyle previewStyleOverride_;
    bool markerToolEnabled_ = false;
    bool annotationEditable_ = true;
    bool markerVisible_ = false;
    bool drawingMarker_ = false;
    QString markerShape_ = "pen";
    QPoint markerPos_;
    QPoint markerStart_;
    QPoint markerEnd_;
    QVector<QPoint> markerPenPoints_;
    QVector<QPointF> markerNormalizedPoints_;
    QColor markerColor_ = QColor(30, 144, 255);
    double zoomFactor_ = 1.0;
    int brightnessOffset_ = 0;
    QPointF panOffset_;
    QPoint lastPanPos_;
    bool panning_ = false;
    QRect imageDrawRect_;
    QPoint normalizedToWidgetPoint(const QPointF& point) const;
    QPointF widgetToNormalizedPoint(const QPoint& point) const;
    QVector<QPoint> currentMarkerDisplayPoints() const;
    QLabel* titleLabel_;
    QLabel* timestampLabel_;
};
