#include "CircularSpinner.h"
#include <QPainter>

CircularSpinner::CircularSpinner(QWidget* parent) : QWidget(parent) {
    setFixedSize(14, 14);
    timer_.setInterval(40);   // ~25 fps rotation
    connect(&timer_, &QTimer::timeout, this, &CircularSpinner::advance);
}

void CircularSpinner::setColor(const QColor& color) {
    color_ = color;
    update();
}

void CircularSpinner::start() {
    if (!timer_.isActive()) {
        angle_ = 0;
        timer_.start();
    }
}

void CircularSpinner::stop() {
    timer_.stop();
    update();
}

QSize CircularSpinner::sizeHint() const {
    return QSize(14, 14);
}

void CircularSpinner::advance() {
    angle_ = (angle_ + 30) % 360;
    update();
}

void CircularSpinner::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal side = qMin(width(), height());
    const QRectF arcRect((width() - side) / 2.0 + 1.5,
                         (height() - side) / 2.0 + 1.5,
                         side - 3.0, side - 3.0);
    QPen pen(color_, 2.0);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    // Rotating ~80° arc (drawArc angles are in 1/16 of a degree).
    p.drawArc(arcRect, angle_ * 16, 80 * 16);
}