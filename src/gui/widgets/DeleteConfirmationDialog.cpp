#include "DeleteConfirmationDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QPainter>
#include <QStyleOption>
#include "IconManager.h"
#include "../../config/CameraConfig.h"

DeleteConfirmationDialog::DeleteConfirmationDialog(QWidget* parent)
    : QDialog(parent, Qt::FramelessWindowHint | Qt::Dialog)
    , itemName_("this item")
    , confirmed_(false)
    , mainLayout_(nullptr)
    , buttonsLayout_(nullptr)
    , contentFrame_(nullptr)
    , iconLabel_(nullptr)
    , titleLabel_(nullptr)
    , messageLabel_(nullptr)
    , itemLabel_(nullptr)
    , contentLayout_(nullptr)
    , confirmBtn_(nullptr)
    , cancelBtn_(nullptr)
    , shadowEffect_(nullptr)
    , fadeAnimation_(nullptr) {
    setupUI();
    setupAnimations();
}

DeleteConfirmationDialog::DeleteConfirmationDialog(const QString& itemName, QWidget* parent)
    : QDialog(parent, Qt::FramelessWindowHint | Qt::Dialog)
    , itemName_(itemName)
    , confirmed_(false)
    , mainLayout_(nullptr)
    , buttonsLayout_(nullptr)
    , contentFrame_(nullptr)
    , iconLabel_(nullptr)
    , titleLabel_(nullptr)
    , messageLabel_(nullptr)
    , itemLabel_(nullptr)
    , contentLayout_(nullptr)
    , confirmBtn_(nullptr)
    , cancelBtn_(nullptr)
    , shadowEffect_(nullptr)
    , fadeAnimation_(nullptr) {
    setupUI();
    setupAnimations();
}

DeleteConfirmationDialog::~DeleteConfirmationDialog() = default;

void DeleteConfirmationDialog::setupUI() {
    setModal(true);
    setWindowModality(Qt::ApplicationModal);
    setAttribute(Qt::WA_TranslucentBackground);

    const ThemeColors tc = CameraConfig::getThemeColors();
    // Muted secondary-text tone (text color at reduced alpha over the frame bg)
    const QString mutedText = [&tc]() {
        const QColor c(tc.text);
        return QString("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(185);
    }();

    // Main layout
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setSpacing(0);
    mainLayout_->setContentsMargins(0, 0, 0, 0);

    // Content frame
    contentFrame_ = new QFrame(this);
    contentFrame_->setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
    contentFrame_->setMinimumWidth(400);
    contentFrame_->setStyleSheet(
        QString("QFrame { background-color: %1; border: 1px solid %2; border-radius: 16px; }")
            .arg(tc.btnBg, tc.border)
    );

    contentLayout_ = new QVBoxLayout(contentFrame_);
    contentLayout_->setSpacing(20);
    contentLayout_->setContentsMargins(32, 32, 32, 32);

    // Icon
    iconLabel_ = new QLabel(contentFrame_);
    iconLabel_->setPixmap(IconManager::instance().trash(48).pixmap(48, 48));
    iconLabel_->setAlignment(Qt::AlignCenter);
    contentLayout_->addWidget(iconLabel_);

    // Title
    titleLabel_ = new QLabel("Delete Confirmation", contentFrame_);
    titleLabel_->setAlignment(Qt::AlignCenter);
    titleLabel_->setStyleSheet(
        QString("font-size: 20px; font-weight: 700; color: %1;").arg(tc.text)
    );
    contentLayout_->addWidget(titleLabel_);

    // Message
    messageLabel_ = new QLabel(
        "Are you sure you want to delete this camera configuration? "
        "This action cannot be undone.",
        contentFrame_
    );
    messageLabel_->setAlignment(Qt::AlignCenter);
    messageLabel_->setWordWrap(true);
    messageLabel_->setStyleSheet(
        QString("font-size: 14px; color: %1; line-height: 1.5;").arg(mutedText)
    );
    contentLayout_->addWidget(messageLabel_);

    // Item name
    itemLabel_ = new QLabel(itemName_, contentFrame_);
    itemLabel_->setAlignment(Qt::AlignCenter);
    itemLabel_->setStyleSheet(
        QString("font-size: 16px; font-weight: 600; color: %1; "
                "padding: 12px; background-color: %2; border-radius: 8px; "
                "margin: 8px 40px;")
            .arg(tc.text, tc.bg)
    );
    contentLayout_->addWidget(itemLabel_);

    // Buttons
    buttonsLayout_ = new QHBoxLayout();
    buttonsLayout_->setSpacing(12);
    buttonsLayout_->addStretch();

    cancelBtn_ = new QPushButton("Cancel", contentFrame_);
    cancelBtn_->setCursor(Qt::PointingHandCursor);
    cancelBtn_->setStyleSheet(
        QString("QPushButton { "
                "  background-color: transparent; "
                "  color: %1; "
                "  border: 1px solid %2; "
                "  border-radius: 8px; "
                "  padding: 12px 24px; "
                "  font-size: 14px; "
                "  font-weight: 500; "
                "} "
                "QPushButton:hover { "
                "  background-color: %2; "
                "  border-color: %3; "
                "}")
            .arg(tc.text, tc.border, tc.primary)
    );
    connect(cancelBtn_, &QPushButton::clicked, this, &DeleteConfirmationDialog::onCancelClicked);
    buttonsLayout_->addWidget(cancelBtn_);

    confirmBtn_ = new QPushButton("Delete", contentFrame_);
    confirmBtn_->setCursor(Qt::PointingHandCursor);
    confirmBtn_->setStyleSheet(
        "QPushButton { "
        "  background-color: #DA3633; "
        "  color: white; "
        "  border: none; "
        "  border-radius: 8px; "
        "  padding: 12px 24px; "
        "  font-size: 14px; "
        "  font-weight: 600; "
        "} "
        "QPushButton:hover { "
        "  background-color: #F85149; "
        "} "
        "QPushButton:pressed { "
        "  background-color: #DA3633; "
        "}"
    );
    connect(confirmBtn_, &QPushButton::clicked, this, &DeleteConfirmationDialog::onConfirmClicked);
    buttonsLayout_->addWidget(confirmBtn_);

    contentLayout_->addLayout(buttonsLayout_);

    mainLayout_->addWidget(contentFrame_, 0, Qt::AlignCenter);

    // Shadow effect
    shadowEffect_ = new QGraphicsDropShadowEffect(contentFrame_);
    shadowEffect_->setBlurRadius(20);
    shadowEffect_->setColor(QColor(0, 0, 0, 80));
    shadowEffect_->setOffset(0, 8);
    contentFrame_->setGraphicsEffect(shadowEffect_);
}

void DeleteConfirmationDialog::setupAnimations() {
    // Fade-in animation
    fadeAnimation_ = new QPropertyAnimation(this, "windowOpacity", this);
    fadeAnimation_->setDuration(200);
    fadeAnimation_->setStartValue(0.0);
    fadeAnimation_->setEndValue(1.0);
    fadeAnimation_->setEasingCurve(QEasingCurve::OutCubic);
}

void DeleteConfirmationDialog::setItemName(const QString& name) {
    itemName_ = name;
    if (itemLabel_) {
        itemLabel_->setText(itemName_);
    }
}

void DeleteConfirmationDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    fadeIn();
}

void DeleteConfirmationDialog::fadeIn() {
    if (fadeAnimation_) {
        fadeAnimation_->start();
    }
}

void DeleteConfirmationDialog::paintEvent(QPaintEvent* event) {
    // Draw semi-transparent backdrop
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 120));
    QDialog::paintEvent(event);
}

void DeleteConfirmationDialog::onConfirmClicked() {
    confirmed_ = true;
    accept();
}

void DeleteConfirmationDialog::onCancelClicked() {
    confirmed_ = false;
    reject();
}
