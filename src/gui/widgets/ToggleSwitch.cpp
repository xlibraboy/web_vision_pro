#include "ToggleSwitch.h"
#include "../../config/CameraConfig.h"
#include <QPainterPath>

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent), _thumbPos(2) {
    setCheckable(true);
    _anim = new QPropertyAnimation(this, "thumbPos", this);
    _anim->setDuration(150); // 150ms animation
    
    connect(this, &QAbstractButton::toggled, [this](bool checked) {
        updateAnimation(checked);
    });
}

QSize ToggleSwitch::sizeHint() const {
    return QSize(44, 24);
}

void ToggleSwitch::setThumbPos(int pos) {
    _thumbPos = pos;
    update();
}

void ToggleSwitch::updateAnimation(bool checked, bool animated) {
    _anim->stop();
    if (animated) {
        _anim->setEndValue(checked ? width() - height() + 2 : 2);
        _anim->start();
    } else {
        setThumbPos(checked ? width() - height() + 2 : 2);
    }
}

void ToggleSwitch::showEvent(QShowEvent* event) {
    QAbstractButton::showEvent(event);
    updateAnimation(isChecked(), false);
}

void ToggleSwitch::nextCheckState() {
    QAbstractButton::nextCheckState();
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    bool state = isChecked();
    
    // Theme colors
    ThemeColors tc = CameraConfig::getThemeColors();
    QColor bgColor(tc.bg);
    QColor activeColor(tc.primary);
    QColor disabledTrack("#555555");
    QColor trackColor = state ? activeColor : bgColor;

    if (!isEnabled()) {
        trackColor = disabledTrack;
    }

    // Draw track
    QPainterPath trackPath;
    trackPath.addRoundedRect(0, 0, width(), height(), height() / 2, height() / 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(trackColor);
    painter.drawPath(trackPath);

    // Draw border if not active
    if (!state && isEnabled()) {
        painter.setPen(QPen(QColor(tc.border), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(trackPath);
    }

    // Draw thumb
    painter.setPen(Qt::NoPen);
    if (!isEnabled()) {
        painter.setBrush(QColor("#888888"));
    } else {
        painter.setBrush(Qt::white);
    }
    
    int thumbSize = height() - 4;
    painter.drawEllipse(_thumbPos, 2, thumbSize, thumbSize);
}
