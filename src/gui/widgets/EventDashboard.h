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
    // Detail strip half-window in frames (shows ±detailRadius_ around the
    // playhead). Default 30; mouse-wheel adjustable over the strip (10–120).
    static constexpr int kDetailRadius = 30;
    static constexpr int kDetailRadiusMin = 10;
    static constexpr int kDetailRadiusMax = 120;

    explicit EventDashboard(QWidget* parent = nullptr);

    void setEventData(const QString& cameraLabel, int totalFrames, int triggerIndex,
                      double fps, const QVector<int>& sampleFrames,
                      const QVector<double>& brightness,
                      const QVector<double>& stddev,
                      const QVector<double>& spotPct,
                      const QVector<int>& defectBrightness,
                      const QVector<int>& defectLocal,
                      const QVector<int>& defectContrast);
    // Per-region visibility. Five stacked regions: brightness chart, detail
    // strip, spots% lane, contrast lane, thumbnail strip. Default: only the
    // brightness chart is shown.
    void setBrightnessVisible(bool on);
    void setRegionsVisible(bool detail, bool spots, bool contrast, bool thumbs);
    // Detail strip actually drawn (zoom-enabled AND user-visible).
    bool isDetailRegionVisible() const { return detailEnabled_ && detailVisible_; }
    // Evenly sampled thumbnails; slot i corresponds to frame
    // round(i * (total-1) / (count-1)).
    void setThumbnails(const QVector<QImage>& thumbs);
    void setCurrentFrame(int frame);
    // Detail strip (magnified signal window around the playhead) on/off.
    void setDetailZoomEnabled(bool on);
    bool isDetailZoomEnabled() const { return detailEnabled_; }
    // Stride-1 series for the detail window (lazy sub-scan result). When set
    // and covering the visible window, the strip draws these instead of the
    // coarse whole-event samples.
    void setDetailSeries(int winStart, int winEnd,
                         const QVector<int>& frames,
                         const QVector<double>& brightness,
                         const QVector<double>& stddev,
                         const QVector<double>& spotPct,
                         const QVector<int>& defectBrightness,
                         const QVector<int>& defectLocal,
                         const QVector<int>& defectContrast);
    void setDetailLoading(bool on);
    // Loading indicators: shown only while the corresponding data is absent.
    void setLoadingSignals(bool on);
    void setSignalProgress(int percent); // -1 = indeterminate
    void setLoadingThumbnails(bool on);
    // When disabled, hovering the tracks no longer pops the frame/values
    // tooltip (used while the TRACKS hover panel is open so its tooltip never
    // fights the panel). Hover seeking is unaffected.
    void setHoverTooltipEnabled(bool on);
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
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    int chartTop() const;
    int chartHeight() const;
    // Detail strip geometry (between the main chart and the signal lanes).
    int detailTop() const;
    int detailHeight() const;
    int laneTop(int i) const;
    int stripTop() const;
    int stripHeight() const;
    int visibleLaneCount() const;
    double frameToX(double frame) const;
    int xToFrameFloor(int x) const;
    void emitSeekAt(int x);
    void emitSeekAtPos(const QPoint& pos);
    bool inDetailRect(const QPoint& pos) const;
    int frameIndexAtPos(const QPoint& pos) const;
    void updateMinimumHeight();
    // [start, end] frame window shown by the detail strip (clamped to the
    // event and to 2*kDetailRadius+1 frames wide when possible).
    void detailWindow(int* startFrame, int* endFrame) const;

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
    bool detailEnabled_ = true;
    int detailRadius_ = kDetailRadius;
    // Lazy stride-1 sub-scan result covering [detailWinStart_ .. detailWinEnd_].
    int detailWinStart_ = 0;
    int detailWinEnd_ = -1;
    QVector<int> detailFrames_;
    QVector<double> detailBrightness_;
    QVector<double> detailStddev_;
    QVector<double> detailSpotPct_;
    QVector<int> detailDefectBrightness_;
    QVector<int> detailDefectLocal_;
    QVector<int> detailDefectContrast_;
    bool detailLoading_ = false;
    // Region visibility (user toggles via the TRACKS hover panel). Default:
    // brightness chart only.
    bool brightnessVisible_ = true;
    bool detailVisible_ = false;
    bool lanesVisible_[2] = {false, false}; // spots%, contrast
    bool thumbsVisible_ = false;
    bool loadingSignals_ = false;
    int signalProgress_ = -1;
    bool loadingThumbs_ = false;
    bool hoverTooltipEnabled_ = true;

    QColor bgColor_ = QColor(QStringLiteral("#1B1B1F"));
    QColor curveColor_ = QColor(QStringLiteral("#4FC3F7"));
    QColor textColor_ = QColor(QStringLiteral("#E0E0E0"));
};
