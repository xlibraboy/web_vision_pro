#pragma once

#include <QDialog>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QHBoxLayout;

/**
 * @brief DeleteConfirmationDialog - Premium styled confirmation dialog
 *
 * Features:
 * - Dark theme with accent colors
 * - Smooth fade-in animation
 * - Icon-based warning
 * - Centered on parent with backdrop
 * - Clear action buttons with styling
 */
class DeleteConfirmationDialog : public QDialog {
    Q_OBJECT

public:
    explicit DeleteConfirmationDialog(QWidget* parent = nullptr);
    explicit DeleteConfirmationDialog(const QString& itemName, QWidget* parent = nullptr);
    ~DeleteConfirmationDialog() override;

    // Set the item being deleted
    void setItemName(const QString& name);

    // Dialog result
    bool confirmed() const { return confirmed_; }

protected:
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onConfirmClicked();
    void onCancelClicked();
    void fadeIn();

private:
    void setupUI();
    void setupStyleSheet();
    void setupAnimations();

    // Layouts
    QVBoxLayout* mainLayout_;
    QHBoxLayout* buttonsLayout_;

    // Widgets
    QFrame* contentFrame_;
    QLabel* iconLabel_;
    QLabel* titleLabel_;
    QLabel* messageLabel_;
    QLabel* itemLabel_;
    QVBoxLayout* contentLayout_;
    QPushButton* confirmBtn_;
    QPushButton* cancelBtn_;

    // Data
    QString itemName_;
    bool confirmed_;

    // Animation
    QGraphicsDropShadowEffect* shadowEffect_;
    QPropertyAnimation* fadeAnimation_;

};
