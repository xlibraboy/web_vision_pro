#include "AnalysisVideoWidget.h"
#include "../../config/CameraConfig.h"
#include <QPainter>
#include <QMouseEvent>
#include <QPen>
#include <QLineF>
#include <QPainterPath>
#include <cmath>
#include <iostream>
#include <algorithm>

AnalysisVideoWidget::AnalysisVideoWidget(int cameraId, const QString& title, QWidget *parent)
    : QWidget(parent), cameraId_(cameraId), title_(title), timestamp_("00:00:00.000") {
    
    setMinimumSize(160, 120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // Background is handled by QPainter in paintEvent; let the global theme set
    // the widget background via QSS inheritance.
}

void AnalysisVideoWidget::setTitle(const QString& title) {
    title_ = title;
    update();
}

void AnalysisVideoWidget::setFrame(const QImage& frame) {
    currentFrame_ = frame;
    scaledFrameCache_ = QImage();
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

void AnalysisVideoWidget::setMarkerToolEnabled(bool enabled) {
    markerToolEnabled_ = enabled;
    panning_ = false;
    drawingMarker_ = false;
    setCursor(markerToolEnabled_ ? Qt::CrossCursor : (zoomFactor_ > 1.0 ? Qt::OpenHandCursor : Qt::ArrowCursor));
    update();
}

void AnalysisVideoWidget::setMarkerShape(const QString& shape) {
    markerShape_ = shape;
    markerPenPoints_.clear();
    markerNormalizedPoints_.clear();
    markerStrokeOverlay_ = QImage();
    drawingMarker_ = false;
    markerVisible_ = !markerPenPoints_.isEmpty() || !markerNormalizedPoints_.isEmpty();
    update();
}

void AnalysisVideoWidget::setAnnotationEditable(bool editable) {
    annotationEditable_ = editable;
    if (!annotationEditable_) {
        drawingMarker_ = false;
    }
}

void AnalysisVideoWidget::setZoomFactor(double factor) {
    const double nextZoom = std::max(1.0, std::min(6.0, factor));
    if (nextZoom <= 1.0) {
        panOffset_ = QPointF(0, 0);
        panning_ = false;
    }
    zoomFactor_ = nextZoom;
    scaledFrameCache_ = QImage();
    setCursor(zoomFactor_ > 1.0 && !markerToolEnabled_ ? Qt::OpenHandCursor : Qt::ArrowCursor);
    update();
}

void AnalysisVideoWidget::setBrightnessOffset(int offset) {
    brightnessOffset_ = std::max(-100, std::min(100, offset));
    update();
}

void AnalysisVideoWidget::resetImageTools() {
    markerToolEnabled_ = false;
    markerVisible_ = false;
    drawingMarker_ = false;
    markerPenPoints_.clear();
    markerNormalizedPoints_.clear();
    markerStrokeOverlay_ = QImage();
    zoomFactor_ = 1.0;
    brightnessOffset_ = 0;
    panOffset_ = QPointF(0, 0);
    panning_ = false;
    setCursor(Qt::ArrowCursor);
    update();
}

void AnalysisVideoWidget::setAnnotation(const QString& shape, const QVector<QPoint>& points) {
    markerShape_ = shape.isEmpty() ? "pen" : shape;
    markerPenPoints_ = points;
    markerNormalizedPoints_.clear();
    markerStrokeOverlay_ = QImage();
    markerVisible_ = !points.isEmpty();
    drawingMarker_ = false;
    if (!points.isEmpty()) {
        markerStart_ = points.first();
        markerEnd_ = points.last();
        markerPos_ = markerEnd_;
    }
    update();
}

void AnalysisVideoWidget::setAnnotationNormalized(const QString& shape, const QVector<QPointF>& points) {
    markerShape_ = shape.isEmpty() ? "pen" : shape;
    markerNormalizedPoints_ = points;
    markerPenPoints_.clear();
    markerStrokeOverlay_ = QImage();
    markerVisible_ = !points.isEmpty();
    drawingMarker_ = false;
    update();
}

void AnalysisVideoWidget::clearAnnotation() {
    markerVisible_ = false;
    drawingMarker_ = false;
    markerPenPoints_.clear();
    markerNormalizedPoints_.clear();
    markerStrokeOverlay_ = QImage();
    update();
}

QPoint AnalysisVideoWidget::normalizedToWidgetPoint(const QPointF& point) const {
    const QRect drawRect = imageDrawRect_.isValid() ? imageDrawRect_ : rect();
    return QPoint(drawRect.left() + static_cast<int>(point.x() * drawRect.width()),
                  drawRect.top() + static_cast<int>(point.y() * drawRect.height()));
}

QPointF AnalysisVideoWidget::widgetToNormalizedPoint(const QPoint& point) const {
    const QRect drawRect = imageDrawRect_.isValid() ? imageDrawRect_ : rect();
    const double w = std::max(1, drawRect.width());
    const double h = std::max(1, drawRect.height());
    const double nx = std::max(0.0, std::min(1.0, (point.x() - drawRect.left()) / w));
    const double ny = std::max(0.0, std::min(1.0, (point.y() - drawRect.top()) / h));
    return QPointF(nx, ny);
}

QVector<QPoint> AnalysisVideoWidget::currentMarkerDisplayPoints() const {
    if (markerNormalizedPoints_.isEmpty()) {
        return markerPenPoints_;
    }

    QVector<QPoint> points;
    points.reserve(markerNormalizedPoints_.size());
    for (const QPointF& point : markerNormalizedPoints_) {
        points.append(normalizedToWidgetPoint(point));
    }
    return points;
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
        // Scale to fit first, then apply detail-view zoom around the center.
        // Cache the scaled frame so pen drawing doesn't rescale the full image on every mouse move.
        QSize targetSize = size();
        if (scaledFrameCache_.isNull()
            || scaledFrameCacheWidgetSize_ != targetSize
            || scaledFrameCacheSourceSize_ != currentFrame_.size()
            || std::abs(scaledFrameCacheZoom_ - zoomFactor_) > 0.001) {
            scaledFrameCache_ = currentFrame_.scaled(targetSize * zoomFactor_, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            scaledFrameCacheWidgetSize_ = targetSize;
            scaledFrameCacheSourceSize_ = currentFrame_.size();
            scaledFrameCacheZoom_ = zoomFactor_;
        }
        const QImage& scaled = scaledFrameCache_;
        
        // Center the image, then apply panning when zoomed in.
        const double maxPanX = std::max(0, (scaled.width() - width()) / 2);
        const double maxPanY = std::max(0, (scaled.height() - height()) / 2);
        panOffset_.setX(std::max(-maxPanX, std::min(maxPanX, panOffset_.x())));
        panOffset_.setY(std::max(-maxPanY, std::min(maxPanY, panOffset_.y())));

        int x = (width() - scaled.width()) / 2 + static_cast<int>(panOffset_.x());
        int y = (height() - scaled.height()) / 2 + static_cast<int>(panOffset_.y());
        imageDrawRect_ = QRect(x, y, scaled.width(), scaled.height());
        
        // Clip to widget bounds
        painter.setClipRect(rect());
        painter.drawImage(x, y, scaled);

        // Display-only brightness adjustment. Apply only to the visible image area;
        // title/timestamp bars are painted afterwards and stay unaffected.
        if (brightnessOffset_ != 0) {
            const int alpha = std::min(180, std::abs(brightnessOffset_) * 2);
            QColor overlay = brightnessOffset_ > 0 ? QColor(255, 255, 255, alpha) : QColor(0, 0, 0, alpha);
            painter.fillRect(imageDrawRect_.intersected(rect()), overlay);
        }

        if (markerVisible_) {
            painter.setClipping(false);
            painter.setBrush(Qt::NoBrush);

            auto drawMarker = [&](const QString& shape, const QVector<QPoint>& displayPoints, const QColor& color) {
                if (displayPoints.isEmpty()) return;

                QPen pen(color, 3);
                pen.setCapStyle(Qt::RoundCap);
                pen.setJoinStyle(Qt::RoundJoin);
                painter.setPen(pen);

                const QPoint startPoint = displayPoints.first();
                const QPoint endPointRaw = displayPoints.last();
                if (shape == "rectangle") {
                    painter.drawRect(QRect(startPoint, endPointRaw).normalized());
                } else if (shape == "circle") {
                    painter.drawEllipse(QRect(startPoint, endPointRaw).normalized());
                } else if (shape == "arrow") {
                    QLineF line(startPoint, endPointRaw);
                    painter.drawLine(line);
                    if (line.length() > 1.0) {
                        const double angle = std::atan2(-line.dy(), line.dx());
                        constexpr double PI = 3.14159265358979323846;
                        const double arrowSize = 14.0;
                        const QPointF endPoint(endPointRaw);
                        QPointF arrowP1 = endPoint - QPointF(std::cos(angle + PI / 6.0) * arrowSize,
                                                              -std::sin(angle + PI / 6.0) * arrowSize);
                        QPointF arrowP2 = endPoint - QPointF(std::cos(angle - PI / 6.0) * arrowSize,
                                                              -std::sin(angle - PI / 6.0) * arrowSize);
                        painter.drawLine(endPointRaw, arrowP1.toPoint());
                        painter.drawLine(endPointRaw, arrowP2.toPoint());
                    }
                } else {
                    if (displayPoints.size() == 1) {
                        painter.drawPoint(displayPoints.first());
                    } else {
                        QPainterPath path(displayPoints.first());
                        for (int i = 1; i < displayPoints.size() - 1; ++i) {
                            const QPointF mid = (QPointF(displayPoints[i]) + QPointF(displayPoints[i + 1])) / 2.0;
                            path.quadTo(displayPoints[i], mid);
                        }
                        path.lineTo(displayPoints.last());
                        painter.drawPath(path);
                    }
                }
            };

            if (drawingMarker_ && markerShape_ == "pen" && !markerStrokeOverlay_.isNull()) {
                painter.drawImage(0, 0, markerStrokeOverlay_);
            } else if (drawingMarker_) {
                QVector<QPoint> previewPoints;
                previewPoints.append(markerStart_);
                previewPoints.append(markerEnd_);
                drawMarker(markerShape_, previewPoints, markerColor_);
            } else {
                drawMarker(markerShape_, currentMarkerDisplayPoints(), markerColor_);
            }
        }
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
    if (event->button() == Qt::LeftButton && annotationEditable_ && markerToolEnabled_ && !currentFrame_.isNull()) {
        markerStart_ = event->pos();
        markerEnd_ = event->pos();
        markerPos_ = event->pos();
        markerPenPoints_.clear();
        markerNormalizedPoints_.clear();
        markerPenPoints_.append(event->pos());
        markerNormalizedPoints_.append(widgetToNormalizedPoint(event->pos()));
        markerVisible_ = true;
        drawingMarker_ = true;
        if (markerShape_ == "pen") {
            markerStrokeOverlay_ = QImage(size(), QImage::Format_ARGB32_Premultiplied);
            markerStrokeOverlay_.fill(Qt::transparent);
            lastStrokePoint_ = event->pos();
            QPainter overlayPainter(&markerStrokeOverlay_);
            overlayPainter.setRenderHint(QPainter::Antialiasing);
            QPen pen(markerColor_, 3);
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            overlayPainter.setPen(pen);
            overlayPainter.drawPoint(event->pos());
        }
        update(QRect(event->pos() - QPoint(8, 8), QSize(16, 16)));
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && zoomFactor_ > 1.0 && !currentFrame_.isNull()) {
        panning_ = true;
        lastPanPos_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        emit clicked(cameraId_);
    }
    QWidget::mousePressEvent(event);
}

void AnalysisVideoWidget::mouseMoveEvent(QMouseEvent* event) {
    if (drawingMarker_ && annotationEditable_ && markerToolEnabled_) {
        markerEnd_ = event->pos();
        markerPos_ = event->pos();
        if (markerShape_ == "pen") {
            markerPenPoints_.append(event->pos());
            markerNormalizedPoints_.append(widgetToNormalizedPoint(event->pos()));
            if (!markerStrokeOverlay_.isNull()) {
                QPainter overlayPainter(&markerStrokeOverlay_);
                overlayPainter.setRenderHint(QPainter::Antialiasing);
                QPen pen(markerColor_, 3);
                pen.setCapStyle(Qt::RoundCap);
                pen.setJoinStyle(Qt::RoundJoin);
                overlayPainter.setPen(pen);
                overlayPainter.drawLine(lastStrokePoint_, event->pos());
                QRect dirty(lastStrokePoint_, event->pos());
                dirty = dirty.normalized().adjusted(-8, -8, 8, 8);
                lastStrokePoint_ = event->pos();
                update(dirty);
            } else {
                update();
            }
        } else {
            update();
        }
        event->accept();
        return;
    }

    if (panning_) {
        const QPoint delta = event->pos() - lastPanPos_;
        lastPanPos_ = event->pos();
        panOffset_ += delta;
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void AnalysisVideoWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && drawingMarker_) {
        markerEnd_ = event->pos();
        markerPos_ = event->pos();
        if (markerShape_ != "pen") {
            markerPenPoints_.clear();
            markerPenPoints_.append(markerStart_);
            markerPenPoints_.append(markerEnd_);
            markerNormalizedPoints_.clear();
            markerNormalizedPoints_.append(widgetToNormalizedPoint(markerStart_));
            markerNormalizedPoints_.append(widgetToNormalizedPoint(markerEnd_));
        }
        drawingMarker_ = false;
        emit annotationChangedNormalized(cameraId_, markerShape_, markerNormalizedPoints_);
        update();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && panning_) {
        panning_ = false;
        setCursor(zoomFactor_ > 1.0 && !markerToolEnabled_ ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void AnalysisVideoWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        std::cout << "[AnalysisVideoWidget] Double-clicked camera: " << cameraId_ << std::endl;
        emit clicked(cameraId_);
        emit doubleClicked(cameraId_);
    }
    QWidget::mouseDoubleClickEvent(event);
}
