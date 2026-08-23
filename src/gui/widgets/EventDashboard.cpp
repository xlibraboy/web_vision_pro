#include "EventDashboard.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QToolTip>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kMargin = 8;
constexpr int kChartH = 110;
constexpr int kStripH = 72;
constexpr int kGap = 6;

const QColor kTriggerColor = QColor(QStringLiteral("#E57373"));
const QColor kDefectColor = QColor(QStringLiteral("#FF9800"));
const QColor kPlayheadColor = QColor(QStringLiteral("#FFD54F"));
const QColor kPlotAreaColor = QColor(QStringLiteral("#23262D"));
} // namespace

EventDashboard::EventDashboard(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setMinimumHeight(kMargin + kChartH + kGap + kStripH + kMargin);
}

int EventDashboard::chartTop() const { return kMargin; }
int EventDashboard::chartHeight() const { return kChartH; }
int EventDashboard::stripTop() const { return kMargin + kChartH + kGap; }
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
                                  const QVector<int>& defectFrames) {
    cameraLabel_ = cameraLabel;
    totalFrames_ = std::max(0, totalFrames);
    triggerIndex_ = qBound(0, triggerIndex, std::max(0, totalFrames_ - 1));
    fps_ = (fps > 0.0) ? fps : 20.0;
    sampleFrames_ = sampleFrames;
    brightness_ = brightness;
    defectFrames_ = defectFrames;
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
    defectFrames_.clear();
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
    return QSize(400, kMargin * 2 + kChartH + kGap + kStripH);
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

    // Defect spikes as vertical ticks behind the curve.
    QPen pen(kDefectColor, 1);
    p.setPen(pen);
    for (int d : defectFrames_) {
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
        const int f = qBound(0, xToFrameFloor(event->pos().x()), totalFrames_ - 1);
        emit frameHovered(f);
        const double relSec = (f - triggerIndex_) / fps_;
        QToolTip::showText(event->globalPos(),
                           QStringLiteral("%1  frame %2  (%3%4s)")
                               .arg(cameraLabel_, QString::number(f),
                                    relSec >= 0 ? QStringLiteral("+") : QString())
                               .arg(relSec, 0, 'f', 1),
                           this);
    }
}

void EventDashboard::leaveEvent(QEvent* /*event*/) {
    QToolTip::hideText();
}
