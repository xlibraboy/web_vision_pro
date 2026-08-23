#pragma once

#include <QWidget>
#include <QVector>
#include <QString>
#include <QImage>
#include <QColor>

/**
 * EventDashboard - Single-camera time-series view of one recorded event.
 *
 * Top region: brightness-over-time curve (sampled), defect-hit spikes
 * marked, trigger line and playback playhead. Bottom region: evenly sampled
 * frame thumbnails in time order.
 *
 * Click/drag anywhere seeks (maps to the nearest frame); hovering shows the
 * frame under the cursor. Purely presentational: data is pushed in by the
 * owner (AnalysisView) from EventSignalScanner results and decoded frames.
 */
class EventDashboard : public QWidget {
    Q_OBJECT
public:
    static constexpr int kThumbCount = 24;

    explicit EventDashboard(QWidget* parent = nullptr);

    void setEventData(const QString& cameraLabel, int totalFrames, int triggerIndex,
                      double fps, const QVector<int>& sampleFrames,
                      const QVector<double>& brightness,
                      const QVector<double>& stddev,
                      const QVector<double>& spotPct,
                      const QVector<int>& defectBrightness,
                      const QVector<int>& defectLocal,
                      const QVector<int>& defectContrast);
    // Evenly sampled thumbnails; slot i corresponds to frame
    // round(i * (total-1) / (count-1)).
    void setThumbnails(const QVector<QImage>& thumbs);
    void setCurrentFrame(int frame);
    void clear();
    void applyTheme(const QColor& background, const QColor& curve,
                    const QColor& text);

    QSize sizeHint() const override;

signals:
    void seekRequested(int frame);
    void frameHovered(int frame);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    int chartTop() const;
    int chartHeight() const;
    int laneTop(int i) const;
    int stripTop() const;
    int stripHeight() const;
    double frameToX(double frame) const;
    int xToFrameFloor(int x) const;
    void emitSeekAt(int x);

    QString cameraLabel_;
    int totalFrames_ = 0;
    int triggerIndex_ = 0;
    double fps_ = 20.0;
    QVector<int> sampleFrames_;
    QVector<double> brightness_;
    QVector<double> stddev_;
    QVector<double> spotPct_;
    QVector<int> defectBrightness_;
    QVector<int> defectLocal_;
    QVector<int> defectContrast_;
    QVector<QImage> thumbs_;
    int currentFrame_ = 0;

    QColor bgColor_ = QColor(QStringLiteral("#1B1B1F"));
    QColor curveColor_ = QColor(QStringLiteral("#4FC3F7"));
    QColor textColor_ = QColor(QStringLiteral("#E0E0E0"));
};
