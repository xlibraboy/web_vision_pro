#pragma once

#include <QAbstractButton>
#include <QPropertyAnimation>
#include <QPainter>

class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(int thumbPos READ thumbPos WRITE setThumbPos)

public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    ~ToggleSwitch() override = default;

    QSize sizeHint() const override;

    int thumbPos() const { return _thumbPos; }
    void setThumbPos(int pos);

protected:
    void paintEvent(QPaintEvent* event) override;
    void nextCheckState() override;
    void showEvent(QShowEvent* event) override;

private:
    int _thumbPos;
    QPropertyAnimation* _anim;
    void updateAnimation(bool checked, bool animated = true);
};
