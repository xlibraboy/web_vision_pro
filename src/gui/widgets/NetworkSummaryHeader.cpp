#include "NetworkSummaryHeader.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QPainter>
#include <QStyleOption>
#include "IconManager.h"

NetworkSummaryHeader::NetworkSummaryHeader(QWidget* parent)
    : QFrame(parent)
    , totalCount_(0)
    , onlineCount_(0)
    , warningCount_(0)
    , errorCount_(0)
    , offlineCount_(0)
    , isRefreshing_(false)
    , primaryColor_("#E3E3E3")
    , accentColor_("#00E5FF")
    , statusColor_("#4CAF50") {

    setupUI();
    setupStyleSheet();
}

NetworkSummaryHeader::~NetworkSummaryHeader() = default;

void NetworkSummaryHeader::setupUI() {
    const QString secondaryButtonStyle =
        "QPushButton { "
        "  background-color: #21262D; "
        "  color: #E3E3E3; "
        "  border: 1px solid #30363D; "
        "  border-radius: 8px; "
        "  padding: 8px 14px; "
        "  font-size: 13px; "
        "  font-weight: 500; "
        "  min-height: 18px; "
        "} "
        "QPushButton:hover { "
        "  background-color: #30363D; "
        "  border-color: #00E5FF; "
        "} "
        "QPushButton:pressed { "
        "  background-color: #1C2128; "
        "}";

    const QString primaryButtonStyle =
        "QPushButton { "
        "  background-color: #238636; "
        "  color: white; "
        "  border: none; "
        "  border-radius: 8px; "
        "  padding: 8px 14px; "
        "  font-size: 13px; "
        "  font-weight: 600; "
        "  min-height: 18px; "
        "} "
        "QPushButton:hover { "
        "  background-color: #2EA043; "
        "} "
        "QPushButton:pressed { "
        "  background-color: #238636; "
        "}";

    setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
    setMinimumHeight(112);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Shadow effect
    shadowEffect_ = new QGraphicsDropShadowEffect(this);
    shadowEffect_->setBlurRadius(0);
    shadowEffect_->setColor(QColor(0, 0, 0, 40));
    shadowEffect_->setOffset(0, 2);
    setGraphicsEffect(shadowEffect_);

    // Main layout
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setSpacing(12);
    mainLayout_->setContentsMargins(18, 16, 18, 16);

    // Header row
    headerLayout_ = new QHBoxLayout();
    headerLayout_->setSpacing(12);

    // Network icon
    networkIcon_ = new QLabel(this);
    networkIcon_->setPixmap(IconManager::instance().network(28).pixmap(28, 28));
    headerLayout_->addWidget(networkIcon_);

    QWidget* titleBlock = new QWidget(this);
    QVBoxLayout* titleLayout = new QVBoxLayout(titleBlock);
    titleLayout->setSpacing(2);
    titleLayout->setContentsMargins(0, 0, 0, 0);

    titleLabel_ = new QLabel("Network Status", this);
    titleLabel_->setStyleSheet("font-size: 17px; font-weight: 700; color: #E3E3E3;");
    titleLayout->addWidget(titleLabel_);

    summaryLabel_ = new QLabel("Review camera connectivity and assignment before saving.", this);
    summaryLabel_->setStyleSheet("font-size: 12px; color: #8B949E;");
    titleLayout->addWidget(summaryLabel_);

    headerLayout_->addWidget(titleBlock, 1);

    QWidget* actionRow = new QWidget(this);
    QHBoxLayout* actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setSpacing(10);
    actionLayout->setContentsMargins(0, 0, 0, 0);

    // Refresh button
    refreshBtn_ = new QPushButton("Refresh", this);
    refreshBtn_->setIcon(IconManager::instance().refresh(16));
    refreshBtn_->setCursor(Qt::PointingHandCursor);
    refreshBtn_->setStyleSheet(secondaryButtonStyle);
    connect(refreshBtn_, &QPushButton::clicked, this, &NetworkSummaryHeader::refreshRequested);
    actionLayout->addWidget(refreshBtn_);

    // Add button
    addBtn_ = new QPushButton("Add Camera", this);
    addBtn_->setIcon(IconManager::instance().add(16));
    addBtn_->setCursor(Qt::PointingHandCursor);
    addBtn_->setStyleSheet(primaryButtonStyle);
    connect(addBtn_, &QPushButton::clicked, this, &NetworkSummaryHeader::addCameraRequested);
    actionLayout->addWidget(addBtn_);

    headerLayout_->addWidget(actionRow, 0, Qt::AlignRight | Qt::AlignTop);

    mainLayout_->addLayout(headerLayout_);

    // Status row
    statusLayout_ = new QHBoxLayout();
    statusLayout_->setSpacing(12);

    // Status indicator
    statusLabel_ = new QLabel("Scanning network...", this);
    statusLabel_->setStyleSheet(
        "font-size: 14px; font-weight: 500; color: #8B949E;"
    );
    statusLayout_->addWidget(statusLabel_, 1);

    statusLayout_->addSpacing(8);

    // Camera count indicators
    indicatorsWidget_ = new QWidget(this);
    QHBoxLayout* indicatorsLayout = new QHBoxLayout(indicatorsWidget_);
    indicatorsLayout->setSpacing(8);
    indicatorsLayout->setContentsMargins(0, 0, 0, 0);

    auto createIndicator = [&](const QString& label, const QColor& color) -> QLabel* {
        QLabel* indicator = new QLabel(QString("%1: 0").arg(label), indicatorsWidget_);
        indicator->setStyleSheet(QString(
            "font-size: 12px; font-weight: 500; "
            "padding: 5px 10px; border-radius: 12px; "
            "background-color: %1; color: white;"
        ).arg(color.name()));
        indicatorsLayout->addWidget(indicator);
        return indicator;
    };

    totalIndicator_ = createIndicator("Total", QColor("#30363D"));
    onlineIndicator_ = createIndicator("Online", QColor("#4CAF50"));
    warningIndicator_ = createIndicator("Warning", QColor("#E0A800"));
    errorIndicator_ = createIndicator("Error", QColor("#FF5A5A"));
    offlineIndicator_ = createIndicator("Offline", QColor("#6E7681"));

    statusLayout_->addWidget(indicatorsWidget_);
    mainLayout_->addLayout(statusLayout_);
}

void NetworkSummaryHeader::setupStyleSheet() {
    setStyleSheet(
        "NetworkSummaryHeader { "
        "  background-color: #161B22; "
        "  border: 1px solid #30363D; "
        "  border-radius: 12px; "
        "}"
    );
}

void NetworkSummaryHeader::setNetworkStatus(const QString& status, const QColor& color) {
    statusLabel_->setText(status);
    statusColor_ = color;

    // Update status label color
    statusLabel_->setStyleSheet(QString(
        "font-size: 14px; font-weight: 600; color: %1;"
    ).arg(color.name()));
}

void NetworkSummaryHeader::setCameraCounts(int total, int online, int warning, int error, int offline) {
    totalCount_ = total;
    onlineCount_ = online;
    warningCount_ = warning;
    errorCount_ = error;
    offlineCount_ = offline;

    // Update indicators
    totalIndicator_->setText(QString("Total: %1").arg(total));
    onlineIndicator_->setText(QString("Online: %1").arg(online));
    warningIndicator_->setText(QString("Warning: %1").arg(warning));
    errorIndicator_->setText(QString("Error: %1").arg(error));
    offlineIndicator_->setText(QString("Offline: %1").arg(offline));
}

void NetworkSummaryHeader::updateSummary(const QString& summary, const QColor& color) {
    setNetworkStatus(summary, color);
}

void NetworkSummaryHeader::onRefreshComplete() {
    isRefreshing_ = false;
    refreshBtn_->setEnabled(true);
    refreshBtn_->setText("Refresh");
}

void NetworkSummaryHeader::setRefreshing(bool refreshing) {
    isRefreshing_ = refreshing;
    refreshBtn_->setEnabled(!refreshing);

    if (refreshing) {
        refreshBtn_->setText("Refreshing...");
        animateRefreshButton();
    } else {
        refreshBtn_->setText("Refresh");
    }
}

void NetworkSummaryHeader::animateRefreshButton() {
    // Rotate the refresh icon
    QIcon refreshIcon = IconManager::instance().refresh(16);
    refreshBtn_->setIcon(refreshIcon);
}

void NetworkSummaryHeader::applyTheme(const QColor& primaryColor, const QColor& accentColor) {
    primaryColor_ = primaryColor;
    accentColor_ = accentColor;

    setupStyleSheet();

    // Refresh icons
    networkIcon_->setPixmap(IconManager::instance().network(28).pixmap(28, 28));
    refreshBtn_->setIcon(IconManager::instance().refresh(16));
    addBtn_->setIcon(IconManager::instance().add(16));
}

void NetworkSummaryHeader::enterEvent(QEvent* event) {
    updateElevation(true);
    QFrame::enterEvent(event);
}

void NetworkSummaryHeader::leaveEvent(QEvent* event) {
    updateElevation(false);
    QFrame::leaveEvent(event);
}

void NetworkSummaryHeader::updateElevation(bool hovered) {
    if (!shadowEffect_) return;

    QPropertyAnimation* anim = new QPropertyAnimation(shadowEffect_, "blurRadius", this);
    anim->setDuration(150);
    anim->setStartValue(shadowEffect_->blurRadius());
    anim->setEndValue(hovered ? 16.0 : 0.0);
    anim->setEasingCurve(QEasingCurve::OutQuad);
    anim->start(QAbstractAnimation::DeleteWhenStopped);

    if (hovered) {
        shadowEffect_->setColor(QColor(0, 229, 255, 40));
        shadowEffect_->setOffset(0, 4);
    } else {
        shadowEffect_->setColor(QColor(0, 0, 0, 40));
        shadowEffect_->setOffset(0, 2);
    }
}
