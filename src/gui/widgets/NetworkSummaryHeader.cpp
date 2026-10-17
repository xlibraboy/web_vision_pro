#include "NetworkSummaryHeader.h"
#include "../../config/CameraConfig.h"
#include "../../config/UiTypeScale.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QPainter>
#include <QStyleOption>
#include "IconManager.h"

namespace {
ThemeColors theme() {
    return CameraConfig::getThemeColors();
}

// Tinted pill: the status color as text on its own 16% wash, with a 35%
// border. Used for the camera-count indicators and the link badge.
QString pillStyle(const QColor& color, double fillAlpha = 0.16, double borderAlpha = 0.35) {
    return QString(
        "font-size: " + UiTypeScale::px(UiTypeScale::kBody) + "; font-weight: 600; "
        "padding: 5px 10px; border-radius: 12px; "
        "background-color: rgba(%1, %2, %3, %4); "
        "color: %5; "
        "border: 1px solid rgba(%1, %2, %3, %6);")
        .arg(color.red()).arg(color.green()).arg(color.blue())
        .arg(fillAlpha).arg(color.name()).arg(borderAlpha);
}

// Secondary action chrome: button-role background with an accent hover.
QString secondaryButtonStyle(const ThemeColors& tc) {
    return QString(
        "QPushButton { "
        "  background-color: %1; "
        "  color: %2; "
        "  border: 1px solid %3; "
        "  border-radius: 8px; "
        "  padding: 8px 14px; "
        "  font-size: " + UiTypeScale::px(UiTypeScale::kBody) + "; "
        "  font-weight: 500; "
        "  min-height: 18px; "
        "} "
        "QPushButton:hover { "
        "  background-color: %4; "
        "  border-color: %5; "
        "} "
        "QPushButton:pressed { "
        "  background-color: %3; "
        "}").arg(tc.btnBg, tc.text, tc.border, tc.btnHover, tc.primary);
}

// Primary action: the app-wide "go" fill (ok) with ink text, so it keeps
// the best contrast of any theme on any status fill.
QString primaryButtonStyle(const ThemeColors& tc) {
    return QString(
        "QPushButton { "
        "  background-color: %1; "
        "  color: %2; "
        "  border: none; "
        "  border-radius: 8px; "
        "  padding: 8px 14px; "
        "  font-size: " + UiTypeScale::px(UiTypeScale::kBody) + "; "
        "  font-weight: 600; "
        "  min-height: 18px; "
        "} "
        "QPushButton:hover { "
        "  background-color: %3; "
        "} "
        "QPushButton:pressed { "
        "  background-color: %4; "
        "}").arg(tc.ok, tc.onStatus, QColor(tc.ok).lighter(112).name(), QColor(tc.ok).darker(108).name());
}
}  // namespace

NetworkSummaryHeader::NetworkSummaryHeader(QWidget* parent)
    : QFrame(parent)
    , totalCount_(0)
    , onlineCount_(0)
    , warningCount_(0)
    , errorCount_(0)
    , offlineCount_(0)
    , isRefreshing_(false)
    , linkSpeedMbps_(0) {

    setupUI();
    updateTheme();
}

NetworkSummaryHeader::~NetworkSummaryHeader() = default;

void NetworkSummaryHeader::setupUI() {
    const ThemeColors tc = theme();

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
    titleLayout->addWidget(titleLabel_);

    summaryLabel_ = new QLabel("Review camera connectivity and assignment before saving.", this);
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
    connect(refreshBtn_, &QPushButton::clicked, this, &NetworkSummaryHeader::refreshRequested);
    actionLayout->addWidget(refreshBtn_);

    // Add button
    addBtn_ = new QPushButton("Add Camera", this);
    addBtn_->setIcon(IconManager::instance().getIcon(Icons::ADD, 16,
                                                     QColor(tc.onStatus), QColor(tc.onStatus)));
    addBtn_->setCursor(Qt::PointingHandCursor);
    connect(addBtn_, &QPushButton::clicked, this, &NetworkSummaryHeader::addCameraRequested);
    actionLayout->addWidget(addBtn_);

    headerLayout_->addWidget(actionRow, 0, Qt::AlignRight | Qt::AlignTop);

    mainLayout_->addLayout(headerLayout_);

    // Status row
    statusLayout_ = new QHBoxLayout();
    statusLayout_->setSpacing(12);

    // Status indicator
    statusLabel_ = new QLabel("Scanning network...", this);
    statusLayout_->addWidget(statusLabel_, 1);

    statusLayout_->addSpacing(8);

    // Camera count indicators
    indicatorsWidget_ = new QWidget(this);
    QHBoxLayout* indicatorsLayout = new QHBoxLayout(indicatorsWidget_);
    indicatorsLayout->setSpacing(8);
    indicatorsLayout->setContentsMargins(0, 0, 0, 0);

    auto createIndicator = [&](const QString& label) -> QLabel* {
        QLabel* indicator = new QLabel(QString("%1: 0").arg(label), indicatorsWidget_);
        indicatorsLayout->addWidget(indicator);
        return indicator;
    };

    totalIndicator_ = createIndicator("Total");
    onlineIndicator_ = createIndicator("Online");
    warningIndicator_ = createIndicator("Warning");
    errorIndicator_ = createIndicator("Error");
    offlineIndicator_ = createIndicator("Offline");
    linkIndicator_ = createIndicator("Link");
    linkIndicator_->setText("Link: --");
    linkIndicator_->setToolTip("Negotiated speed of the vision NIC carrying the camera subnets. "
                               "Below 1 Gb/s, high-FPS streams drop frames.");

    statusLayout_->addWidget(indicatorsWidget_);
    mainLayout_->addLayout(statusLayout_);
}

void NetworkSummaryHeader::updateTheme() {
    setupStyleSheet();

    const ThemeColors tc = theme();

    refreshBtn_->setStyleSheet(secondaryButtonStyle(tc));
    addBtn_->setStyleSheet(primaryButtonStyle(tc));

    titleLabel_->setStyleSheet(QString("font-size: " + UiTypeScale::px(UiTypeScale::kDisplay) + "; font-weight: 700; color: %1;").arg(tc.text));
    summaryLabel_->setStyleSheet(QString("font-size: " + UiTypeScale::px(UiTypeScale::kBody) + "; color: %1;").arg(tc.muted));
    applyStatusLabelStyle();

    // Count pills are one token color each; the link badge keeps its runtime
    // state, so it refreshes through refreshLinkIndicator().
    totalIndicator_->setStyleSheet(pillStyle(QColor(tc.text)));
    onlineIndicator_->setStyleSheet(pillStyle(QColor(tc.ok)));
    warningIndicator_->setStyleSheet(pillStyle(QColor(tc.warn)));
    errorIndicator_->setStyleSheet(pillStyle(QColor(tc.critical)));
    offlineIndicator_->setStyleSheet(pillStyle(QColor(tc.muted)));
    refreshLinkIndicator();

    // Icons are rendered from the tokens on demand, not by the stylesheet, so
    // they need their own re-render on a preset change.
    networkIcon_->setPixmap(IconManager::instance().network(28).pixmap(28, 28));
    refreshBtn_->setIcon(IconManager::instance().refresh(16));
    addBtn_->setIcon(IconManager::instance().getIcon(Icons::ADD, 16,
                                                     QColor(tc.onStatus), QColor(tc.onStatus)));
}

void NetworkSummaryHeader::setupStyleSheet() {
    const ThemeColors tc = theme();
    setStyleSheet(QString(
        "NetworkSummaryHeader { "
        "  background-color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 12px; "
        "}"
    ).arg(tc.surface, tc.border));
}

void NetworkSummaryHeader::setNetworkStatus(const QString& status, StatusTone tone) {
    statusLabel_->setText(status);
    statusTone_ = tone;
    applyStatusLabelStyle();
}

void NetworkSummaryHeader::applyStatusLabelStyle() {
    const ThemeColors tc = theme();

    // A reported status carries the tone's own ink; the muted "Scanning
    // network..." holding state (no setNetworkStatus() yet) stays lighter.
    QString ink = tc.muted;
    int weight = 500;
    switch (statusTone_) {
        case StatusTone::Neutral:
            break;
        case StatusTone::Ok:
            ink = tc.ok;
            weight = 600;
            break;
        case StatusTone::Warn:
            ink = tc.warn;
            weight = 600;
            break;
        case StatusTone::Critical:
            ink = tc.critical;
            weight = 600;
            break;
    }
    statusLabel_->setStyleSheet(QString(
        "font-size: " + UiTypeScale::px(UiTypeScale::kTitle) + "; font-weight: %1; color: %2;"
    ).arg(weight).arg(ink));
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

void NetworkSummaryHeader::setLinkSpeedMbps(int mbps) {
    linkSpeedMbps_ = mbps;
    refreshLinkIndicator();
}

void NetworkSummaryHeader::refreshLinkIndicator() {
    if (!linkIndicator_) {
        return;
    }

    const ThemeColors tc = theme();
    if (linkSpeedMbps_ <= 0) {
        linkIndicator_->setText("Link: --");
        linkIndicator_->setStyleSheet(pillStyle(QColor(tc.muted)));
        return;
    }
    const QString text = (linkSpeedMbps_ >= 1000)
        ? QString("Link: %1 Gb/s").arg(linkSpeedMbps_ / 1000.0, 0, 'f', linkSpeedMbps_ % 1000 == 0 ? 0 : 1)
        : QString("Link: %1 Mb/s").arg(linkSpeedMbps_);
    const QColor color = (linkSpeedMbps_ >= 1000) ? QColor(tc.ok)
                                                  : QColor(tc.warn); // <1 Gb/s: warning
    linkIndicator_->setText(text);
    linkIndicator_->setStyleSheet(pillStyle(color));
}

void NetworkSummaryHeader::updateSummary(const QString& summary, StatusTone tone) {
    setNetworkStatus(summary, tone);
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
        QColor glow = QColor(theme().primary);
        glow.setAlpha(40);
        shadowEffect_->setColor(glow);
        shadowEffect_->setOffset(0, 4);
    } else {
        shadowEffect_->setColor(QColor(0, 0, 0, 40));
        shadowEffect_->setOffset(0, 2);
    }
}
