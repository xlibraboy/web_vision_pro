#pragma once

#include <QWidget>
#include <QColor>
#include <QTimer>

// Small indeterminate circular progress spinner (a rotating arc) for compact
// in-panel loading states. Paints an antialiased ~80° arc that rotates every
// 40 ms while running. Tint via setColor(); size via setFixedSize().
class CircularSpinner : public QWidget {
    Q_OBJECT
public:
    explicit CircularSpinner(QWidget* parent = nullptr);

    void setColor(const QColor& color);
    void start();
    void stop();

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void advance();

    QColor color_ = QColor(QStringLiteral("#4FC3F7"));
    int angle_ = 0;
    QTimer timer_;
};