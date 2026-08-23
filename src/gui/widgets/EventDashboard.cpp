#include "EventDashboard.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QToolTip>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kMargin = 8;
constexpr int kChartH = 96;
constexpr int kLaneH = 26;
constexpr int kLaneCount = 2; // CHANGE % and CONTRAST
constexpr int kStripH = 72;
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
    setMinimumHeight(kMargin * 2 + kChartH + kStripH
                     + kLaneCount * (kLaneH + kGap));
}

int EventDashboard::chartTop() const { return kMargin; }
int EventDashboard::chartHeight() const { return kChartH; }
int EventDashboard::laneTop(int i) const {
    return kMargin + kChartH + kGap + i * (kLaneH + kGap);
}
int EventDashboard::stripTop() const {
    return laneTop(kLaneCount);
}
int EventDashboard::stripHeight() const { return kStripH; }

double EventDashboard::frameToX(double frame) const {
    const int w = width();
    const double usable = std::max(1.0, static_cast<double>(w - 2 * kMargin));
    const double span = std::max(1, totalFrames_ - 1);
    return kMargin + (frame / span) * usable;
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
                                  const QVector<double>& changePct,
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
    changePct_ = changePct;
    defectBrightness_ = defectBrightness;
    defectLocal_ = defectLocal;
    defectContrast_ = defectContrast;
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

void EventDashboard::clear() {
    cameraLabel_.clear();
    totalFrames_ = 0;
    sampleFrames_.clear();
    brightness_.clear();
    stddev_.clear();
    changePct_.clear();
    defectBrightness_.clear();
    defectLocal_.clear();
    defectContrast_.clear();
    thumbs_.clear();
    currentFrame_ = 0;
    update();
}

void EventDashboard::applyTheme(const QColor& background, const QColor& curve, const QColor& text) {
    bgColor_ = background;
    curveColor_ = curve;
    textColor_ = text;
    update();
}

QSize EventDashboard::sizeHint() const {
    return QSize(400, kMargin * 2 + kChartH + kStripH
                          + kLaneCount * (kLaneH + kGap));
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

    // ---------- Chart region ----------
    QRectF plot(kMargin, chartTop(), width() - 2 * kMargin, chartHeight());
    p.fillRect(plot, kPlotAreaColor);

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
    QPen pen(kDefectColor, 1);
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

    // ---------- Signal lanes: CHANGE % and CONTRAST ----------
    struct LaneDef {
        const char* label;
        const QVector<double>* series;
        const QVector<int>* marks;
        QColor markColor;
        double fixedMax; // 0 = autoscale from data
    };
    const LaneDef lanes[kLaneCount] = {
        {"CHANGE %", &changePct_, &defectLocal_, kLocalColor, 0.0},
        {"CONTRAST", &stddev_, &defectContrast_, kContrastColor, 0.0},
    };
    for (int li = 0; li < kLaneCount; ++li) {
        const LaneDef& ld = lanes[li];
        QRectF lane(kMargin, laneTop(li), width() - 2 * kMargin, kLaneH);
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

    // Trigger line across chart + strip.
    const double trigX = frameToX(triggerIndex_);
    pen.setColor(kTriggerColor);
    pen.setWidth(2);
    p.setPen(pen);
    p.drawLine(QPointF(trigX, plot.top() - 3), QPointF(trigX, stripTop() + stripHeight() + 3));

    // ---------- Thumbnail strip ----------
    const double stripW = width() - 2 * kMargin;
    const double slotW = stripW / kThumbCount;
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

    // Playhead across both regions.
    const double playX = frameToX(currentFrame_);
    p.drawLine(QPointF(playX, plot.top() - 3), QPointF(playX, stripTop() + stripHeight() + 3));

    // ---------- Time axis ----------
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
        p.drawText(QRectF(x - 34, stripTop() + stripHeight() + 4, 68, 14),
                   Qt::AlignHCenter | Qt::AlignTop, text);
    }

    // Camera label intentionally not painted: the Camera tab's tab title and
    // the video area already identify the camera — a third copy here is
    // noise. The hover tooltip still reports it.
    // p.drawText(QRectF(plot.left() + 4, plot.top() + 2, 160, 14),
    //            Qt::AlignLeft, cameraLabel_);
}

void EventDashboard::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emitSeekAt(event->pos().x());
    }
}

void EventDashboard::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        emitSeekAt(event->pos().x());
        return;
    }
    if (totalFrames_ > 0) {
        const int fIdx = qBound(0, xToFrameFloor(event->pos().x()), totalFrames_ - 1);
        emit frameHovered(fIdx);
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
            if (changePct_.size() == sampleFrames_.size()) {
                tip += QStringLiteral(" | change %1%").arg(changePct_.at(si), 0, 'f', 2);
            }
        }
        QToolTip::showText(event->globalPos(), tip, this);
    }
}

void EventDashboard::leaveEvent(QEvent* /*event*/) {
    QToolTip::hideText();
}
