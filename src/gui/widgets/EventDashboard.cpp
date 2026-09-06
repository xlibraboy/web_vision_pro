#include "EventDashboard.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QToolTip>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kMargin = 8;
// All five stacked regions share the same height (Thumbnails strip included)
// so any friend + Thumbnails renders as two equal bands.
constexpr int kRegionH = 72;
constexpr int kLaneCount = 2; // CHANGE % and CONTRAST
constexpr int kGap = 5;

const QColor kTriggerColor = QColor(QStringLiteral("#E57373"));
const QColor kDefectColor = QColor(QStringLiteral("#FF9800"));
const QColor kLocalColor = QColor(QStringLiteral("#FF5252"));
const QColor kContrastColor = QColor(QStringLiteral("#B388FF"));
const QColor kPlayheadColor = QColor(QStringLiteral("#FFD54F"));
const QColor kPlotAreaColor = QColor(QStringLiteral("#23262D"));
} // namespace

EventDashboard::EventDashboard(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    updateMinimumHeight();
}

void EventDashboard::updateMinimumHeight() {
    // All five regions share the same height (kRegionH). Total height is the
    // sum of every VISIBLE band as (kRegionH + kGap), plus margins and the
    // time-axis row — independent of which specific tracks are shown, so any
    // friend + Thumbnails yields the exact same widget height.
    int bands = 0;
    if (brightnessVisible_) ++bands;
    if (detailEnabled_ && detailVisible_) ++bands;
    bands += visibleLaneCount();
    if (thumbsVisible_) ++bands;
    const int h = kMargin + bands * (kRegionH + kGap) + kMargin + 16;
    setMinimumHeight(h);
    setMaximumHeight(h);
}

void EventDashboard::setBrightnessVisible(bool on) {
    brightnessVisible_ = on;
    updateMinimumHeight();
    update();
}

void EventDashboard::setRegionsVisible(bool detail, bool spots, bool contrast, bool thumbs) {
    detailVisible_ = detail;
    lanesVisible_[0] = spots;
    lanesVisible_[1] = contrast;
    thumbsVisible_ = thumbs;
    updateMinimumHeight();
    update();
}

int EventDashboard::chartTop() const { return kMargin; }
int EventDashboard::chartHeight() const { return brightnessVisible_ ? kRegionH : 0; }
int EventDashboard::detailTop() const {
    // Uniform stacking: every visible band above occupies kRegionH + kGap.
    return kMargin + chartHeight() + (chartHeight() ? kGap : 0);
}
int EventDashboard::detailHeight() const {
    return (detailEnabled_ && detailVisible_) ? kRegionH : 0;
}
int EventDashboard::visibleLaneCount() const {
    int n = 0;
    for (int k = 0; k < kLaneCount; ++k) if (lanesVisible_[k]) ++n;
    return n;
}
int EventDashboard::laneTop(int i) const {
    // i = index among VISIBLE lanes; base is below detail, +gap when shown.
    return detailTop() + detailHeight() + (detailHeight() ? kGap : 0)
           + i * (kRegionH + kGap);
}
int EventDashboard::stripTop() const {
    // Below the chart, detail strip, and any lanes — each visible band above
    // takes kRegionH + kGap so the thumbnail strip sits at the same offset no
    // matter which friend track is on.
    int above = 0;
    if (brightnessVisible_) ++above;
    if (detailEnabled_ && detailVisible_) ++above;
    above += visibleLaneCount();
    return kMargin + above * (kRegionH + kGap);
}
int EventDashboard::stripHeight() const { return thumbsVisible_ ? kRegionH : 0; }

void EventDashboard::setDetailZoomEnabled(bool on) {
    if (detailEnabled_ == on) return;
    detailEnabled_ = on;
    updateMinimumHeight();
    update();
}

void EventDashboard::setDetailSeries(int winStart, int winEnd,
                                     const QVector<int>& frames,
                                     const QVector<double>& brightness,
                                     const QVector<double>& stddev,
                                     const QVector<double>& spotPct,
                                     const QVector<int>& defectBrightness,
                                     const QVector<int>& defectLocal,
                                     const QVector<int>& defectContrast) {
    detailWinStart_ = winStart;
    detailWinEnd_ = winEnd;
    detailFrames_ = frames;
    detailBrightness_ = brightness;
    detailStddev_ = stddev;
    detailSpotPct_ = spotPct;
    detailDefectBrightness_ = defectBrightness;
    detailDefectLocal_ = defectLocal;
    detailDefectContrast_ = defectContrast;
    detailLoading_ = false;
    update();
}

void EventDashboard::setDetailLoading(bool on) {
    if (detailLoading_ == on) return;
    detailLoading_ = on;
    update();
}

void EventDashboard::detailWindow(int* startFrame, int* endFrame) const {
    const int last = std::max(0, totalFrames_ - 1);
    int start = currentFrame_ - detailRadius_;
    int end = currentFrame_ + detailRadius_;
    if (start < 0) {
        end -= start; // shift window right to keep full width at the left edge
        start = 0;
    }
    if (end > last) {
        start -= (end - last); // shift left at the right edge
        end = last;
    }
    *startFrame = std::max(0, start);
    *endFrame = end;
}

double EventDashboard::frameToX(double frame) const {
    const int w = width();
    const double usable = std::max(1.0, static_cast<double>(w - 2 * kMargin));
    const double span = std::max(1, totalFrames_ - 1);
    return kMargin + (frame / span) * usable;
}

bool EventDashboard::inDetailRect(const QPoint& pos) const {
    return detailEnabled_ && totalFrames_ > 1
           && pos.y() >= detailTop() && pos.y() < detailTop() + detailHeight();
}

int EventDashboard::frameIndexAtPos(const QPoint& pos) const {
    if (inDetailRect(pos)) {
        int winStart = 0;
        int winEnd = 0;
        detailWindow(&winStart, &winEnd);
        const double t = static_cast<double>(pos.x() - kMargin)
                         / std::max(1, width() - 2 * kMargin);
        return qBound(0, static_cast<int>(std::lround(winStart + t * (winEnd - winStart))),
                      std::max(0, totalFrames_ - 1));
    }
    return qBound(0, xToFrameFloor(pos.x()), std::max(0, totalFrames_ - 1));
}

void EventDashboard::emitSeekAtPos(const QPoint& pos) {
    if (totalFrames_ <= 0) {
        return;
    }
    emit seekRequested(frameIndexAtPos(pos));
}

int EventDashboard::xToFrameFloor(int x) const {
    const double t = static_cast<double>(x - kMargin) / std::max(1, width() - 2 * kMargin);
    return static_cast<int>(std::floor(t * std::max(0, totalFrames_ - 1)));
}

void EventDashboard::emitSeekAt(int x) {
    if (totalFrames_ <= 0) {
        return;
    }
    emit seekRequested(qBound(0, xToFrameFloor(x), totalFrames_ - 1));
}

void EventDashboard::setEventData(const QString& cameraLabel, int totalFrames, int triggerIndex,
                                  double fps, const QVector<int>& sampleFrames,
                                  const QVector<double>& brightness,
                                  const QVector<double>& stddev,
                                  const QVector<double>& spotPct,
                                  const QVector<int>& defectBrightness,
                                  const QVector<int>& defectLocal,
                                  const QVector<int>& defectContrast) {
    cameraLabel_ = cameraLabel;
    totalFrames_ = std::max(0, totalFrames);
    triggerIndex_ = qBound(0, triggerIndex, std::max(0, totalFrames_ - 1));
    fps_ = (fps > 0.0) ? fps : 20.0;
    sampleFrames_ = sampleFrames;
    brightness_ = brightness;
    stddev_ = stddev;
    spotPct_ = spotPct;
    defectBrightness_ = defectBrightness;
    defectLocal_ = defectLocal;
    defectContrast_ = defectContrast;
    // New event/camera: any previous lazy window series is stale.
    detailWinStart_ = 0;
    detailWinEnd_ = -1;
    detailFrames_.clear();
    detailBrightness_.clear();
    detailStddev_.clear();
    detailSpotPct_.clear();
    detailDefectBrightness_.clear();
    detailDefectLocal_.clear();
    detailDefectContrast_.clear();
    detailLoading_ = false;
    update();
}

void EventDashboard::setThumbnails(const QVector<QImage>& thumbs) {
    thumbs_ = thumbs;
    update();
}

void EventDashboard::setCurrentFrame(int frame) {
    const int clamped = qBound(0, frame, std::max(0, totalFrames_ - 1));
    if (clamped == currentFrame_) {
        return;
    }
    currentFrame_ = clamped;
    update();
}

void EventDashboard::setLoadingSignals(bool on) {
    if (loadingSignals_ == on) return;
    loadingSignals_ = on;
    update();
}

void EventDashboard::setSignalProgress(int percent) {
    if (signalProgress_ == percent) return;
    signalProgress_ = percent;
    if (loadingSignals_) update();
}

void EventDashboard::setLoadingThumbnails(bool on) {
    if (loadingThumbs_ == on) return;
    loadingThumbs_ = on;
    update();
}

void EventDashboard::clear() {
    cameraLabel_.clear();
    totalFrames_ = 0;
    sampleFrames_.clear();
    brightness_.clear();
    stddev_.clear();
    spotPct_.clear();
    defectBrightness_.clear();
    defectLocal_.clear();
    defectContrast_.clear();
    thumbs_.clear();
    currentFrame_ = 0;
    detailWinStart_ = 0;
    detailWinEnd_ = -1;
    detailFrames_.clear();
    detailBrightness_.clear();
    detailStddev_.clear();
    detailSpotPct_.clear();
    detailDefectBrightness_.clear();
    detailDefectLocal_.clear();
    detailDefectContrast_.clear();
    detailLoading_ = false;
    loadingSignals_ = false;
    signalProgress_ = -1;
    loadingThumbs_ = false;
    update();
}

void EventDashboard::applyTheme(const QColor& background, const QColor& curve, const QColor& text) {
    bgColor_ = background;
    curveColor_ = curve;
    textColor_ = text;
    update();
}

QSize EventDashboard::sizeHint() const {
    return QSize(400, minimumHeight());
}

void EventDashboard::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), bgColor_);

    if (totalFrames_ <= 0) {
        p.setPen(textColor_);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No event loaded"));
        return;
    }

    QPen pen(kDefectColor, 1); // shared by chart, detail strip, lanes, lines

    // ---------- Chart region ----------
    QRectF plot(kMargin, chartTop(), width() - 2 * kMargin, chartHeight());
    p.fillRect(plot, kPlotAreaColor);

    if (brightnessVisible_) {
    if (brightness_.isEmpty() && loadingSignals_) {
        // Fresh event: scanner still working — show indeterminate state.
        const QString msg = (signalProgress_ >= 0)
            ? QStringLiteral("Analyzing signals… %1%").arg(signalProgress_)
            : QStringLiteral("Analyzing signals…");
        QFont f = font();
        f.setItalic(true);
        p.setFont(f);
        p.setPen(QColor(textColor_).darker(130));
        p.drawText(plot, Qt::AlignCenter, msg);
    }

    double yMin = 0.0;
    double yMax = 255.0;
    if (!brightness_.isEmpty()) {
        yMin = *std::min_element(brightness_.constBegin(), brightness_.constEnd());
        yMax = *std::max_element(brightness_.constBegin(), brightness_.constEnd());
        if (yMax - yMin < 10.0) { // avoid a flat line hugging one edge
            const double mid = (yMax + yMin) / 2.0;
            yMin = mid - 5.0;
            yMax = mid + 5.0;
        }
        yMin = std::max(0.0, yMin - 2.0);
        yMax = std::min(255.0, yMax + 2.0);
    }

    auto yFor = [&](double v) {
        const double span = std::max(1.0, yMax - yMin);
        return plot.bottom() - ((v - yMin) / span) * plot.height();
    };

    // Brightness-jump defect spikes as vertical ticks behind the curve.
    p.setPen(pen);
    for (int d : defectBrightness_) {
        const double dx = frameToX(d);
        p.drawLine(QPointF(dx, plot.top()), QPointF(dx, plot.bottom()));
    }

    // Brightness curve.
    if (sampleFrames_.size() == brightness_.size() && !brightness_.isEmpty()) {
        QPainterPath path;
        for (int i = 0; i < sampleFrames_.size(); ++i) {
            const QPointF pt(frameToX(sampleFrames_.at(i)), yFor(brightness_.at(i)));
            if (i == 0) {
                path.moveTo(pt);
            } else {
                path.lineTo(pt);
            }
        }
        pen.setColor(curveColor_);
        pen.setWidth(1);
        p.setPen(pen);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.drawPath(path);
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    // Range labels (top-right).
    p.setPen(textColor_);
    p.setFont(font());
    p.drawText(QRectF(plot.right() - 120, plot.top() + 2, 118, 14),
               Qt::AlignRight, QStringLiteral("brightness %1–%2")
                                   .arg(static_cast<int>(yMin))
                                   .arg(static_cast<int>(yMax)));
    // Lane name (top-left), styled like the SPOTS % / CONTRAST lane labels.
    p.drawText(QRectF(plot.left() + 4, plot.top() + 1, 110, 12),
               Qt::AlignLeft | Qt::AlignTop, QStringLiteral("BRIGHTNESS"));
    } // end brightnessVisible_

    // ---------- Detail strip: magnified window around the playhead ----------
    const bool detailActive = detailEnabled_ && detailVisible_ && totalFrames_ > 1;
    QRectF det;
    int winStart = 0;
    int winEnd = 0;
    if (detailActive) {
        det = QRectF(kMargin, detailTop(), width() - 2 * kMargin, kRegionH);
        p.fillRect(det, kPlotAreaColor);
        detailWindow(&winStart, &winEnd);
        const double spanW = std::max(1, winEnd - winStart);
        auto dxFor = [&](double frame) {
            return det.left() + ((frame - winStart) / spanW) * det.width();
        };

        // Prefer the lazy stride-1 sub-scan when it covers the window;
        // otherwise slice the coarse whole-event series.
        const bool detailCovers = detailWinStart_ <= winStart && detailWinEnd_ >= winEnd
                                  && detailFrames_.size() == detailBrightness_.size()
                                  && !detailFrames_.isEmpty();

        // Shared drawing: curve + per-sample dots + defect ticks + trigger
        // tick + rulers, over an arbitrary (frame, brightness) series.
        auto drawDetailContent =
            [&](const QVector<int>& frames, const QVector<double>& brightness,
                const QVector<int>* ticksB, const QVector<int>* ticksL,
                const QVector<int>* ticksC) {
                double yMin = 255.0;
                double yMax = 0.0;
                for (int i = 0; i < frames.size(); ++i) {
                    yMin = std::min(yMin, brightness.at(i));
                    yMax = std::max(yMax, brightness.at(i));
                }
                if (yMax - yMin < 10.0) { // same flat-line guard as the main chart
                    const double mid = (yMax + yMin) / 2.0;
                    yMin = mid - 5.0;
                    yMax = mid + 5.0;
                }
                yMin = std::max(0.0, yMin - 2.0);
                yMax = std::min(255.0, yMax + 2.0);

                auto dyFor = [&](double v) {
                    const double span = std::max(1.0, yMax - yMin);
                    return det.bottom() - ((v - yMin) / span) * (det.height() - 12)
                           - 10.0; // reserve bottom edge for the frame ruler
                };

                pen.setWidth(1);
                for (const QVector<int>* marks : { ticksB, ticksL, ticksC }) {
                    if (!marks) continue;
                    pen.setColor(marks == ticksB ? kDefectColor
                                 : marks == ticksL ? kLocalColor
                                                   : kContrastColor);
                    p.setPen(pen);
                    for (int d : *marks) {
                        if (d < winStart || d > winEnd) continue;
                        p.drawLine(QPointF(dxFor(d), det.top()), QPointF(dxFor(d), det.bottom()));
                    }
                }

                QPainterPath dp;
                for (int i = 0; i < frames.size(); ++i) {
                    const QPointF pt(dxFor(frames.at(i)), dyFor(brightness.at(i)));
                    if (i == 0) dp.moveTo(pt); else dp.lineTo(pt);
                }
                pen.setColor(curveColor_);
                pen.setWidth(1);
                p.setPen(pen);
                p.setRenderHint(QPainter::Antialiasing, true);
                p.drawPath(dp);
                if (det.width() / std::max(1.0, spanW) >= 3.0) {
                    p.setBrush(curveColor_);
                    for (int i = 0; i < frames.size(); ++i) {
                        p.drawRect(QRectF(dxFor(frames.at(i)) - 1.5,
                                          dyFor(brightness.at(i)) - 1.5, 3, 3));
                    }
                    p.setBrush(Qt::NoBrush);
                }
                p.setRenderHint(QPainter::Antialiasing, false);

                // Local trigger tick (the global line below is clipped out here).
                if (triggerIndex_ >= winStart && triggerIndex_ <= winEnd) {
                    pen.setColor(kTriggerColor);
                    pen.setWidth(2);
                    p.setPen(pen);
                    p.drawLine(QPointF(dxFor(triggerIndex_), det.top()),
                               QPointF(dxFor(triggerIndex_), det.bottom()));
                }

                // Frame-number ruler at both window edges.
                p.setPen(QColor(textColor_).darker(120));
                p.setFont(font());
                p.drawText(QRectF(det.left() + 2, det.bottom() - 11, 60, 11),
                           Qt::AlignLeft | Qt::AlignBottom, QString::number(winStart));
                p.drawText(QRectF(det.right() - 62, det.bottom() - 11, 60, 11),
                           Qt::AlignRight | Qt::AlignBottom, QString::number(winEnd));

                // Local Y range: makes the magnification explicit vs the main
                // chart's whole-event range label.
                p.setPen(textColor_);
                p.drawText(QRectF(det.right() - 120, det.top() + 1, 116, 12),
                           Qt::AlignRight | Qt::AlignTop,
                           QStringLiteral("y %1–%2")
                               .arg(static_cast<int>(yMin))
                               .arg(static_cast<int>(yMax)));
            };

        if (detailCovers) {
            drawDetailContent(detailFrames_, detailBrightness_,
                              &detailDefectBrightness_, &detailDefectLocal_,
                              &detailDefectContrast_);
        } else {
        // Window slice of the sampled series (contiguous when stride=1).
        QVector<int> idx;
        for (int i = 0; i < sampleFrames_.size(); ++i) {
            if (sampleFrames_.at(i) >= winStart && sampleFrames_.at(i) <= winEnd) {
                idx.append(i);
            }
        }

        if (!brightness_.isEmpty() && idx.size() >= 2
                && sampleFrames_.size() == brightness_.size()) {
            // Local autoscale: magnifies both axes relative to the main chart.
            double yMin = 255.0;
            double yMax = 0.0;
            for (int i : idx) {
                yMin = std::min(yMin, brightness_.at(i));
                yMax = std::max(yMax, brightness_.at(i));
            }
            if (yMax - yMin < 10.0) { // same flat-line guard as the main chart
                const double mid = (yMax + yMin) / 2.0;
                yMin = mid - 5.0;
                yMax = mid + 5.0;
            }
            yMin = std::max(0.0, yMin - 2.0);
            yMax = std::min(255.0, yMax + 2.0);

            auto dyFor = [&](double v) {
                const double span = std::max(1.0, yMax - yMin);
                return det.bottom() - ((v - yMin) / span) * (det.height() - 12)
                       - 10.0; // reserve bottom edge for the frame ruler
            };

            // Defect ticks within the window, color-matched to the lanes.
            pen.setWidth(1);
            for (const QVector<int>* marks : { &defectBrightness_, &defectLocal_, &defectContrast_ }) {
                pen.setColor(marks == &defectBrightness_ ? kDefectColor
                             : marks == &defectLocal_     ? kLocalColor
                                                          : kContrastColor);
                p.setPen(pen);
                for (int d : *marks) {
                    if (d < winStart || d > winEnd) continue;
                    p.drawLine(QPointF(dxFor(d), det.top()), QPointF(dxFor(d), det.bottom()));
                }
            }

            // Curve + per-sample point markers (individual frames become
            // visible as discrete dots at this zoom level).
            QPainterPath dp;
            for (int k = 0; k < idx.size(); ++k) {
                const int i = idx.at(k);
                const QPointF pt(dxFor(sampleFrames_.at(i)), dyFor(brightness_.at(i)));
                if (k == 0) dp.moveTo(pt); else dp.lineTo(pt);
            }
            pen.setColor(curveColor_);
            pen.setWidth(1);
            p.setPen(pen);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.drawPath(dp);
            if (det.width() / spanW >= 3.0) {
                p.setBrush(curveColor_);
                for (int k = 0; k < idx.size(); ++k) {
                    const int i = idx.at(k);
                    p.drawRect(QRectF(dxFor(sampleFrames_.at(i)) - 1.5,
                                      dyFor(brightness_.at(i)) - 1.5, 3, 3));
                }
                p.setBrush(Qt::NoBrush);
            }
            p.setRenderHint(QPainter::Antialiasing, false);

            // Local trigger tick (the global line below is clipped out here).
            if (triggerIndex_ >= winStart && triggerIndex_ <= winEnd) {
                pen.setColor(kTriggerColor);
                pen.setWidth(2);
                p.setPen(pen);
                p.drawLine(QPointF(dxFor(triggerIndex_), det.top()),
                           QPointF(dxFor(triggerIndex_), det.bottom()));
            }

            // Frame-number ruler at both window edges.
            p.setPen(QColor(textColor_).darker(120));
            p.setFont(font());
            p.drawText(QRectF(det.left() + 2, det.bottom() - 11, 60, 11),
                       Qt::AlignLeft | Qt::AlignBottom, QString::number(winStart));
            p.drawText(QRectF(det.right() - 62, det.bottom() - 11, 60, 11),
                       Qt::AlignRight | Qt::AlignBottom, QString::number(winEnd));

            // Local Y range: makes the magnification explicit vs the main
            // chart's whole-event range label.
            p.setPen(textColor_);
            p.drawText(QRectF(det.right() - 120, det.top() + 1, 116, 12),
                       Qt::AlignRight | Qt::AlignTop,
                       QStringLiteral("y %1–%2")
                           .arg(static_cast<int>(yMin))
                           .arg(static_cast<int>(yMax)));
        } else {
            QFont f = font();
            f.setItalic(true);
            p.setFont(f);
            p.setPen(QColor(textColor_).darker(130));
            p.drawText(det, Qt::AlignCenter,
                       (loadingSignals_ || (detailLoading_ && detailFrames_.isEmpty()))
                           ? QStringLiteral("Analyzing signals…")
                           : QStringLiteral("No samples in window"));
        }
        }

        // Label.
        p.setPen(textColor_);
        p.setFont(font());
        p.drawText(QRectF(det.left() + 4, det.top() + 1, 150, 12),
                   Qt::AlignLeft | Qt::AlignTop,
                   QStringLiteral("DETAIL ±%1f (wheel to zoom)").arg(detailRadius_));

        // Local playhead marker: small filled notch on the strip's top edge
        // (its x differs from the global-scale playhead line below).
        const double localPlayX = dxFor(currentFrame_);
        if (localPlayX >= det.left() && localPlayX <= det.right()) {
            pen.setColor(kPlayheadColor);
            pen.setWidth(1);
            p.setPen(pen);
            p.drawLine(QPointF(localPlayX, det.top()), QPointF(localPlayX, det.bottom()));
        }
    }

    // ---------- Signal lanes: CHANGE % and CONTRAST ----------
    struct LaneDef {
        const char* label;
        const QVector<double>* series;
        const QVector<int>* marks;
        QColor markColor;
        double fixedMax; // 0 = autoscale from data
    };
    const LaneDef lanes[kLaneCount] = {
        {"SPOTS %", &spotPct_, &defectLocal_, kLocalColor, 0.0},
        {"CONTRAST", &stddev_, &defectContrast_, kContrastColor, 0.0},
    };
    int visLane = 0;
    for (int li = 0; li < kLaneCount; ++li) {
        if (!lanesVisible_[li]) continue;
        const LaneDef& ld = lanes[li];
        QRectF lane(kMargin, laneTop(visLane++), width() - 2 * kMargin, kRegionH);
        p.fillRect(lane, kPlotAreaColor);

        double lo = 0.0;
        double hi = ld.fixedMax;
        if (!ld.series->isEmpty()) {
            hi = std::max(hi, *std::max_element(ld.series->constBegin(),
                                                ld.series->constEnd()));
        }
        if (hi <= lo) {
            hi = 1.0;
        }
        auto yLane = [&](double v) {
            return lane.bottom() - ((v - lo) / std::max(1e-9, hi - lo)) * lane.height();
        };

        // Rule ticks behind the trace.
        pen.setColor(ld.markColor);
        pen.setWidth(1);
        p.setPen(pen);
        for (int d : *ld.marks) {
            const double dx = frameToX(d);
            p.drawLine(QPointF(dx, lane.top()), QPointF(dx, lane.bottom()));
        }

        if (sampleFrames_.size() == ld.series->size() && !ld.series->isEmpty()) {
            QPainterPath lp;
            for (int i = 0; i < sampleFrames_.size(); ++i) {
                const QPointF pt(frameToX(sampleFrames_.at(i)),
                                 yLane(std::max(lo, std::min(hi, ld.series->at(i)))));
                if (i == 0) lp.moveTo(pt); else lp.lineTo(pt);
            }
            pen.setColor(curveColor_);
            pen.setWidth(1);
            p.setPen(pen);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.drawPath(lp);
            p.setRenderHint(QPainter::Antialiasing, false);
        }

        p.setPen(textColor_);
        p.setFont(font());
        p.drawText(QRectF(lane.left() + 4, lane.top() + 1, 90, 12),
                   Qt::AlignLeft | Qt::AlignTop, QLatin1String(ld.label));
        p.drawText(QRectF(lane.right() - 120, lane.top() + 1, 116, 12),
                   Qt::AlignRight | Qt::AlignTop,
                   QStringLiteral("max %1").arg(hi, 0, 'f', 1));
    }

    // Trigger line across chart + strip. When the detail strip is active the
    // global-scale x has no meaning inside it (different time scale), so the
    // line is drawn in two segments around that region.
    const double trigX = frameToX(triggerIndex_);
    pen.setColor(kTriggerColor);
    pen.setWidth(2);
    p.setPen(pen);
    if (detailActive) {
        p.drawLine(QPointF(trigX, plot.top() - 3), QPointF(trigX, det.top() - 2));
        p.drawLine(QPointF(trigX, det.bottom() + 2), QPointF(trigX, stripTop() + stripHeight() + 3));
    } else {
        p.drawLine(QPointF(trigX, plot.top() - 3), QPointF(trigX, stripTop() + stripHeight() + 3));
    }

    // ---------- Thumbnail strip ----------
    const double stripW = width() - 2 * kMargin;
    const double slotW = stripW / kThumbCount;
    if (stripHeight() > 0) {

    if (thumbs_.isEmpty() && loadingThumbs_) {
        QFont f = font();
        f.setItalic(true);
        p.setFont(f);
        p.setPen(QColor(textColor_).darker(130));
        p.drawText(QRectF(kMargin, stripTop(), stripW, stripHeight()),
                   Qt::AlignCenter, QStringLiteral("Loading thumbnails…"));
    }

    for (int i = 0; i < kThumbCount; ++i) {
        QRectF slot(kMargin + i * slotW, stripTop(), slotW - 1.0, stripHeight());
        p.fillRect(slot, kPlotAreaColor);
        if (i < thumbs_.size() && !thumbs_.at(i).isNull()) {
            const QImage& img = thumbs_.at(i);
            QImage scaled = img.scaled(static_cast<int>(slot.width()),
                                       static_cast<int>(slot.height()),
                                       Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const double ix = slot.left() + (slot.width() - scaled.width()) / 2.0;
            const double iy = slot.top() + (slot.height() - scaled.height()) / 2.0;
            p.drawImage(QPointF(ix, iy), scaled);
        } else {
            // Slot frame position marker even when the thumb is missing.
            p.setPen(bgColor_.lighter(140));
            p.drawRect(slot.adjusted(0, 0, -1, -1));
        }
    }

    // Highlight thumbnail covering the playhead.
    const double frac = static_cast<double>(currentFrame_)
                        / std::max(1, totalFrames_ - 1);
    const int curSlot = qBound(0, static_cast<int>(frac * kThumbCount), kThumbCount - 1);
    pen.setColor(kPlayheadColor);
    pen.setWidth(2);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(kMargin + curSlot * slotW, stripTop(), slotW - 1.0, stripHeight()));
    } // end thumbnailsVisible

    // Playhead across both regions (same segment split as the trigger line).
    const double playX = frameToX(currentFrame_);
    pen.setColor(kPlayheadColor);
    p.setPen(pen);
    if (detailActive) {
        p.drawLine(QPointF(playX, plot.top() - 3), QPointF(playX, det.top() - 2));
        p.drawLine(QPointF(playX, det.bottom() + 2), QPointF(playX, stripTop() + stripHeight() + 3));
    } else {
        p.drawLine(QPointF(playX, plot.top() - 3), QPointF(playX, stripTop() + stripHeight() + 3));
    }

    // ---------- Time axis ----------
    // The labels sit in the reserved row below the last band (the same +16 the
    // minimum-height math reserves). Center them vertically in that fixed row
    // so the padding above and below the tick text is even, without letting
    // them drift down into the playback area (where the zero/trigger flag on
    // the scrubber lives).
    const double axisTop = stripTop() + stripHeight();
    constexpr double kAxisRowH = 16.0;
    p.setPen(textColor_);
    const int tickPx = 90;
    const int tickCount = std::max(2, width() / tickPx);
    for (int k = 0; k <= tickCount; ++k) {
        const double fracT = static_cast<double>(k) / tickCount;
        const double m = fracT * (totalFrames_ - 1);
        const double x = frameToX(m);
        const double relSec = (m - triggerIndex_) / fps_;
        const QString text = QStringLiteral("%1%2s")
                                 .arg(relSec >= 0 ? QStringLiteral("+") : QString())
                                 .arg(relSec, 0, 'f', 1);
        p.drawText(QRectF(x - 34, axisTop, 68, kAxisRowH),
                   Qt::AlignHCenter | Qt::AlignVCenter, text);
    }

    // Camera label intentionally not painted: the Camera tab's tab title and
    // the video area already identify the camera — a third copy here is
    // noise. The hover tooltip still reports it.
    // p.drawText(QRectF(plot.left() + 4, plot.top() + 2, 160, 14),
    //            Qt::AlignLeft, cameraLabel_);
}

void EventDashboard::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emitSeekAtPos(event->pos());
    }
}

void EventDashboard::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        emitSeekAtPos(event->pos());
        return;
    }
    if (totalFrames_ > 0) {
        const int fIdx = frameIndexAtPos(event->pos());
        emit frameHovered(fIdx);
        if (!hoverTooltipEnabled_) {
            return;
        }
        const double relSec = (fIdx - triggerIndex_) / fps_;
        QString tip = QStringLiteral("%1  frame %2  (%3%4s)")
                          .arg(cameraLabel_, QString::number(fIdx),
                               relSec >= 0 ? QStringLiteral("+") : QString())
                          .arg(relSec, 0, 'f', 1);
        // Nearest sampled values for the signal lanes.
        if (sampleFrames_.size() == brightness_.size() && !sampleFrames_.isEmpty()) {
            int si = 0;
            for (int i = 0; i < sampleFrames_.size(); ++i) {
                if (std::abs(sampleFrames_.at(i) - fIdx)
                    < std::abs(sampleFrames_.at(si) - fIdx)) {
                    si = i;
                }
            }
            tip += QStringLiteral("\nbrightness %1").arg(brightness_.at(si), 0, 'f', 1);
            if (stddev_.size() == sampleFrames_.size()) {
                tip += QStringLiteral(" | contrast %1").arg(stddev_.at(si), 0, 'f', 1);
            }
            if (spotPct_.size() == sampleFrames_.size()) {
                tip += QStringLiteral(" | spots %1%").arg(spotPct_.at(si), 0, 'f', 2);
            }
        }
        QToolTip::showText(event->globalPos(), tip, this);
    }
}

void EventDashboard::setHoverTooltipEnabled(bool on) {
    hoverTooltipEnabled_ = on;
    if (!on) {
        QToolTip::hideText();
    }
}

void EventDashboard::leaveEvent(QEvent* /*event*/) {
    QToolTip::hideText();
}

void EventDashboard::wheelEvent(QWheelEvent* event) {
    // Wheel over the detail strip zooms its frame window (not the whole
    // dashboard). One notch = ±5 frames, clamped to a usable range.
    if (!inDetailRect(event->position().toPoint())) {
        QWidget::wheelEvent(event);
        return;
    }
    const int before = detailRadius_;
    const int step = event->angleDelta().y() > 0 ? 5 : -5;
    detailRadius_ = qBound(kDetailRadiusMin, detailRadius_ + step, kDetailRadiusMax);
    if (detailRadius_ != before) {
        update();
        QToolTip::showText(event->globalPosition().toPoint(),
                           QStringLiteral("Detail window ±%1 frames").arg(detailRadius_), this);
    }
    event->accept();
}
