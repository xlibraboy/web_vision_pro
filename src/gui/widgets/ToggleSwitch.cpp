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
    QColor disabledTrack(tc.disabled);
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

    // Keyboard focus: a ring inside the track, drawn before the thumb so the
    // knob stays whole. The focus token equals the theme accent, which is
    // exactly the checked track fill, so fall back through the other two inks
    // until the ring separates from the fill it sits on - starting with the one
    // that contrasts that fill on this theme (the status ink is dark on the
    // dark theme's bright fills and light on the light theme's dark ones).
    if (hasFocus()) {
        const auto colorDistance = [](const QColor& a, const QColor& b) {
            return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green())
                 + qAbs(a.blue() - b.blue());
        };
        const QColor firstFallback(tc.isDark() ? tc.text : tc.onStatus);
        const QColor secondFallback(tc.isDark() ? tc.onStatus : tc.text);
        QColor ringColor(tc.focus);
        if (colorDistance(ringColor, trackColor) < 180) {
            ringColor = firstFallback;
        }
        if (colorDistance(ringColor, trackColor) < 180) {
            ringColor = secondFallback;
        }
        painter.setPen(QPen(ringColor, 2));
        painter.setBrush(Qt::NoBrush);
        const int inset = 2;
        painter.drawRoundedRect(QRectF(rect()).adjusted(inset, inset, -inset, -inset),
                                height() / 2.0 - inset, height() / 2.0 - inset);
    }

    // Draw thumb. White reads on the dark theme's tracks; on the light theme's
    // near-white unchecked track the thumb needs the grey ink to stay visible.
    painter.setPen(Qt::NoPen);
    if (!isEnabled()) {
        painter.setBrush(QColor(tc.muted));
    } else if (state || tc.isDark()) {
        painter.setBrush(QColor(tc.handle));
    } else {
        painter.setBrush(QColor(tc.disabled));
    }
    
    int thumbSize = height() - 4;
    painter.drawEllipse(_thumbPos, 2, thumbSize, thumbSize);
}
