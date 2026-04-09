#include "CameraCard.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QToolButton>
#include <QGroupBox>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOption>
#include "IconManager.h"

CameraCard::CameraCard(const CameraInfo& info, QWidget* parent)
    : QFrame(parent)
    , cameraId_(info.id)
    , isExpanded_(true)
    , isHovered_(false)
    , hasIssues_(false)
    , contentHeight_(0)
    , statusColor_("#888888")
    , primaryColor_("#E3E3E3")
    , accentColor_("#00E5FF") {

    setupUI(info);
    setupAnimations();
    setupStyleSheet();
    updateSmartState();
}

CameraCard::~CameraCard() {
    // Animations are automatically cleaned up by parent
}

void CameraCard::setupUI(const CameraInfo& info) {
    // Set frame properties
    setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
    setMinimumWidth(CARD_MIN_WIDTH);
    setMaximumWidth(CARD_MAX_WIDTH);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Main layout
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setSpacing(0);
    mainLayout_->setContentsMargins(0, 0, 0, 0);

    // Create header
    createHeader();
    updateHeader(info);

    // Create content area
    contentWidget_ = new QWidget(this);
    contentLayout_ = new QVBoxLayout(contentWidget_);
    contentLayout_->setSpacing(10);
    contentLayout_->setContentsMargins(14, 10, 14, 14);

    createContent(info);

    mainLayout_->addWidget(contentWidget_);

    // Set initial state
    setExpanded(true, false);
}

void CameraCard::createHeader() {
    headerLayout_ = new QHBoxLayout();
    headerLayout_->setSpacing(12);
    headerLayout_->setContentsMargins(16, 12, 16, 12);

    // Camera icon
    cameraIcon_ = new QLabel(this);
    cameraIcon_->setPixmap(IconManager::instance().camera(24).pixmap(24, 24));
    headerLayout_->addWidget(cameraIcon_);

    // Camera title
    cameraTitle_ = new QLabel(this);
    cameraTitle_->setStyleSheet("font-size: 14px; font-weight: 600; color: #E3E3E3;");
    headerLayout_->addWidget(cameraTitle_);

    headerLayout_->addStretch();

    // Status label
    statusLabel_ = new QLabel("Pending", this);
    statusLabel_->setStyleSheet(
        "font-size: 12px; font-weight: 500; padding: 4px 10px; "
        "border-radius: 12px; background-color: rgba(136, 136, 136, 0.2); "
        "color: #888888;"
    );
    headerLayout_->addWidget(statusLabel_);

    // Expand/collapse button
    expandButton_ = new QToolButton(this);
    expandButton_->setIcon(IconManager::instance().collapse(16));
    expandButton_->setStyleSheet(
        "QToolButton { background: transparent; border: none; padding: 4px; }"
        "QToolButton:hover { background-color: rgba(0, 229, 255, 0.1); border-radius: 4px; }"
    );
    connect(expandButton_, &QToolButton::clicked, this, [this]() {
        setExpanded(!isExpanded_);
    });
    headerLayout_->addWidget(expandButton_);

    // Edit checkbox
    editCheck_ = new QCheckBox("Edit", this);
    editCheck_->setStyleSheet(
        "QCheckBox { color: #E3E3E3; font-size: 12px; spacing: 6px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; }"
    );
    connect(editCheck_, &QCheckBox::toggled, this, &CameraCard::editToggled);
    headerLayout_->addWidget(editCheck_);

    // Remove button
    removeButton_ = new QToolButton(this);
    removeButton_->setIcon(IconManager::instance().trash(16));
    removeButton_->setStyleSheet(
        "QToolButton { background: transparent; border: none; padding: 4px; }"
        "QToolButton:hover { background-color: rgba(255, 90, 90, 0.2); border-radius: 4px; }"
    );
    connect(removeButton_, &QToolButton::clicked, this, &CameraCard::removeClicked);
    headerLayout_->addWidget(removeButton_);

    // Add header layout to main layout
    QWidget* headerWidget = new QWidget(this);
    headerWidget->setLayout(headerLayout_);
    headerWidget->setCursor(Qt::PointingHandCursor);
    headerWidget->setMouseTracking(true);
    mainLayout_->addWidget(headerWidget);

    // Make header clickable for expand/collapse
    headerWidget->installEventFilter(this);
}

void CameraCard::updateHeader(const CameraInfo& info) {
    cameraTitle_->setText(QString("Camera %1: %2").arg(info.id).arg(info.name));
}

void CameraCard::createContent(const CameraInfo& info) {
    const QString groupStyle =
        "QGroupBox { font-weight: 600; color: #00E5FF; border: 1px solid #30363D; "
        "border-radius: 8px; margin-top: 6px; padding-top: 8px; font-size: 12px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }";

    const QString fieldStyle =
        "QComboBox, QLineEdit, QSpinBox, QLabel { "
        "background-color: #1C2128; border: 1px solid #30363D; "
        "border-radius: 6px; padding: 6px 8px; color: #E3E3E3; font-size: 12px; }"
        "QComboBox:hover, QLineEdit:hover { border-color: #00E5FF; }"
        "QComboBox:focus, QLineEdit:focus { border-color: #00E5FF; }"
        "QSpinBox { padding-right: 0; }";

    // Basic Info Group
    basicInfoGroup_ = new QGroupBox("Basic Info", contentWidget_);
    basicInfoGroup_->setStyleSheet(groupStyle);

    basicFieldsLayout_ = new QGridLayout(basicInfoGroup_);
    basicFieldsLayout_->setHorizontalSpacing(10);
    basicFieldsLayout_->setVerticalSpacing(8);
    basicFieldsLayout_->setContentsMargins(12, 14, 12, 10);

    int basicRow = 0;

    // Helper lambda to add field
    auto addField = [&](QGroupBox* group, QGridLayout* layout, int& row, const QString& label, QWidget* widget) {
        QLabel* lbl = new QLabel(label, group);
        lbl->setStyleSheet("color: #8B949E; font-size: 11px;");
        widget->setStyleSheet(fieldStyle);
        layout->addWidget(lbl, row, 0, Qt::AlignRight | Qt::AlignVCenter);
        layout->addWidget(widget, row, 1);
        row++;
    };

    // Source
    sourceCombo_ = new QComboBox(basicInfoGroup_);
    sourceCombo_->addItem("Emulated", 0);
    sourceCombo_->addItem("Real", 1);
    sourceCombo_->addItem("Disabled", 2);
    sourceCombo_->setCurrentIndex(sourceCombo_->findData(info.source));
    addField(basicInfoGroup_, basicFieldsLayout_, basicRow, "Source:", sourceCombo_);
    connect(sourceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraCard::sourceChanged);

    // Name
    nameEdit_ = new QLineEdit(info.name, basicInfoGroup_);
    addField(basicInfoGroup_, basicFieldsLayout_, basicRow, "Name:", nameEdit_);

    // Location
    locationEdit_ = new QLineEdit(info.location, basicInfoGroup_);
    addField(basicInfoGroup_, basicFieldsLayout_, basicRow, "Location:", locationEdit_);

    // Side
    sideCombo_ = new QComboBox(basicInfoGroup_);
    sideCombo_->addItem("DRIVE SIDE");
    sideCombo_->addItem("OPERATOR SIDE");
    sideCombo_->setCurrentText(info.side);
    addField(basicInfoGroup_, basicFieldsLayout_, basicRow, "Side:", sideCombo_);

    // Position
    positionSpin_ = new QSpinBox(basicInfoGroup_);
    positionSpin_->setRange(0, 500000);
    positionSpin_->setSuffix(" mm");
    positionSpin_->setValue(info.machinePosition);
    addField(basicInfoGroup_, basicFieldsLayout_, basicRow, "Position:", positionSpin_);

    contentLayout_->addWidget(basicInfoGroup_);

    // Network Settings Group
    networkInfoGroup_ = new QGroupBox("Network Settings", contentWidget_);
    networkInfoGroup_->setStyleSheet(groupStyle);

    networkFieldsLayout_ = new QGridLayout(networkInfoGroup_);
    networkFieldsLayout_->setHorizontalSpacing(10);
    networkFieldsLayout_->setVerticalSpacing(8);
    networkFieldsLayout_->setContentsMargins(12, 14, 12, 10);

    int networkRow = 0;

    // IP Address (Read Only)
    ipLabel_ = new QLabel(info.ipAddress, networkInfoGroup_);
    ipLabel_->setStyleSheet("font-family: 'SF Mono', Monaco, monospace; color: #00E5FF;");
    addField(networkInfoGroup_, networkFieldsLayout_, networkRow, "IP Address:", ipLabel_);

    // Detected IP (Read Only)
    detectedIpLabel_ = new QLabel("Offline", networkInfoGroup_);
    detectedIpLabel_->setStyleSheet("font-family: 'SF Mono', Monaco, monospace; color: #8B949E;");
    addField(networkInfoGroup_, networkFieldsLayout_, networkRow, "Current Camera IP:", detectedIpLabel_);

    // MAC Address
    macCombo_ = new QComboBox(networkInfoGroup_);
    macCombo_->addItem("None / Auto");
    macCombo_->setEditable(true);
    macCombo_->setCurrentText(info.macAddress);
    addField(networkInfoGroup_, networkFieldsLayout_, networkRow, "MAC Address:", macCombo_);
    connect(macCombo_, &QComboBox::currentTextChanged, this, &CameraCard::macChanged);

    // Subnet Mask
    subnetEdit_ = new QLineEdit(info.subnetMask, networkInfoGroup_);
    addField(networkInfoGroup_, networkFieldsLayout_, networkRow, "Subnet Mask:", subnetEdit_);

    // Gateway
    gatewayEdit_ = new QLineEdit(info.defaultGateway, networkInfoGroup_);
    addField(networkInfoGroup_, networkFieldsLayout_, networkRow, "Gateway:", gatewayEdit_);

    // Write IP Button
    writeIpBtn_ = new QPushButton("Apply IP to Camera", networkInfoGroup_);
    writeIpBtn_->setIcon(IconManager::instance().save(16));
    writeIpBtn_->setStyleSheet(
        "QPushButton { background-color: #238636; color: white; "
        "border: none; border-radius: 6px; padding: 8px 16px; "
        "font-size: 12px; font-weight: 500; }"
        "QPushButton:hover { background-color: #2EA043; }"
        "QPushButton:disabled { background-color: #30363D; color: #8B949E; }"
    );
    connect(writeIpBtn_, &QPushButton::clicked, this, &CameraCard::writeIpClicked);
    networkFieldsLayout_->addWidget(writeIpBtn_, networkRow, 0, 1, 2);

    contentLayout_->addWidget(networkInfoGroup_);

    acquisitionGroup_ = new QGroupBox("Acquisition", contentWidget_);
    acquisitionGroup_->setStyleSheet(groupStyle);
    QGridLayout* acquisitionLayout = new QGridLayout(acquisitionGroup_);
    acquisitionLayout->setHorizontalSpacing(10);
    acquisitionLayout->setVerticalSpacing(8);
    acquisitionLayout->setContentsMargins(12, 14, 12, 10);

    // FPS with Enable checkbox
    enableFpsCheck_ = new QCheckBox("Enable Acquisition Frame Rate", acquisitionGroup_);
    enableFpsCheck_->setChecked(info.enableAcquisitionFps);
    enableFpsCheck_->setStyleSheet("color: #E3E3E3; font-size: 11px;");

    fpsSpin_ = new QSpinBox(acquisitionGroup_);
    fpsSpin_->setRange(1, 200);
    fpsSpin_->setValue(info.fps);
    fpsSpin_->setEnabled(info.enableAcquisitionFps);
    fpsSpin_->setStyleSheet(fieldStyle);

    QWidget* fpsWidget = new QWidget(acquisitionGroup_);
    QHBoxLayout* fpsLayout = new QHBoxLayout(fpsWidget);
    fpsLayout->setContentsMargins(0, 0, 0, 0);
    fpsLayout->setSpacing(10);
    fpsLayout->addWidget(enableFpsCheck_);
    fpsLayout->addWidget(fpsSpin_);
    fpsLayout->addStretch();

    QLabel* fpsLabel = new QLabel("Frame Rate:", acquisitionGroup_);
    fpsLabel->setStyleSheet("color: #8B949E; font-size: 11px;");
    acquisitionLayout->addWidget(fpsLabel, 0, 0, Qt::AlignRight | Qt::AlignTop);
    acquisitionLayout->addWidget(fpsWidget, 0, 1);

    connect(enableFpsCheck_, &QCheckBox::toggled, this, &CameraCard::onFpsEnabledToggled);

    contentLayout_->addWidget(acquisitionGroup_);
    contentLayout_->addStretch();

    // Set initial editable state
    setEditable(false);
}

void CameraCard::setupAnimations() {
    // Shadow effect
    shadowEffect_ = new QGraphicsDropShadowEffect(this);
    shadowEffect_->setBlurRadius(0);
    shadowEffect_->setColor(QColor(0, 0, 0, 0));
    shadowEffect_->setOffset(0, 0);
    setGraphicsEffect(shadowEffect_);

    // Height animation
    heightAnimation_ = new QPropertyAnimation(this, "contentHeight", this);
    heightAnimation_->setDuration(ANIMATION_DURATION);
    heightAnimation_->setEasingCurve(QEasingCurve::OutCubic);

    // Shadow animation
    shadowAnimation_ = new QPropertyAnimation(shadowEffect_, "blurRadius", this);
    shadowAnimation_->setDuration(150);
    shadowAnimation_->setEasingCurve(QEasingCurve::OutQuad);

    // Group for expand/collapse
    expandAnimationGroup_ = new QParallelAnimationGroup(this);
}

void CameraCard::setupStyleSheet() {
    setStyleSheet(
        "CameraCard { "
        "  background-color: #24292E; "
        "  border: 1px solid #30363D; "
        "  border-radius: 12px; "
        "} "
        "CameraCard:hover { "
        "  border-color: #00E5FF; "
        "}"
    );
}

void CameraCard::setExpanded(bool expanded, bool animate) {
    if (isExpanded_ == expanded) return;

    isExpanded_ = expanded;

    // Update expand button icon
    expandButton_->setIcon(isExpanded_
        ? IconManager::instance().collapse(16)
        : IconManager::instance().expand(16));

    if (animate && heightAnimation_) {
        // Animate height change
        int targetHeight = isExpanded_ ? contentWidget_->sizeHint().height() : 0;
        heightAnimation_->setStartValue(contentHeight_);
        heightAnimation_->setEndValue(targetHeight);
        heightAnimation_->start();
    } else {
        contentWidget_->setVisible(isExpanded_);
    }

    emit expansionChanged(isExpanded_);
}

void CameraCard::expandWithAnimation() {
    setExpanded(true, true);
}

void CameraCard::collapseWithAnimation() {
    setExpanded(false, true);
}

void CameraCard::updateSmartState() {
    // Auto-expand cards with issues, collapse healthy ones
    bool hasIssue = (statusColor_ == QColor("#FF5A5A") || statusColor_ == QColor("#E0A800"));

    if (hasIssue != hasIssues_) {
        hasIssues_ = hasIssue;
        setExpanded(hasIssues_);
    }
}

void CameraCard::setEditable(bool editable) {
    sourceCombo_->setEnabled(editable);
    nameEdit_->setEnabled(editable);
    locationEdit_->setEnabled(editable);
    sideCombo_->setEnabled(editable);
    positionSpin_->setEnabled(editable);
    macCombo_->setEnabled(editable);
    subnetEdit_->setEnabled(editable);
    gatewayEdit_->setEnabled(editable);
    writeIpBtn_->setEnabled(editable);
    enableFpsCheck_->setEnabled(editable);
    fpsSpin_->setEnabled(editable && enableFpsCheck_->isChecked());
}

void CameraCard::setDetectedIp(const QString& ip) {
    detectedIpLabel_->setText(ip.isEmpty() ? "Offline" : ip);
}

void CameraCard::setStatus(const QString& text, const QColor& color) {
    statusLabel_->setText(text);
    statusColor_ = color;

    // Update status label style
    QString style = QString(
        "font-size: 12px; font-weight: 600; padding: 4px 12px; "
        "border-radius: 12px; background-color: %1; color: %2;"
    ).arg(color.name()).arg("#FFFFFF");

    statusLabel_->setStyleSheet(style);

    // Update camera icon color based on status - use getIcon for custom colors
    if (color == QColor("#4CAF50")) {
        cameraIcon_->setPixmap(IconManager::instance().getIcon(Icons::CAMERA, 24,
            QColor("#4CAF50"), QColor("#4CAF50")).pixmap(24, 24));
    } else if (color == QColor("#E0A800")) {
        cameraIcon_->setPixmap(IconManager::instance().getIcon(Icons::CAMERA, 24,
            QColor("#E0A800"), QColor("#E0A800")).pixmap(24, 24));
    } else if (color == QColor("#FF5A5A")) {
        cameraIcon_->setPixmap(IconManager::instance().getIcon(Icons::CAMERA, 24,
            QColor("#FF5A5A"), QColor("#FF5A5A")).pixmap(24, 24));
    } else {
        cameraIcon_->setPixmap(IconManager::instance().camera(24).pixmap(24, 24));
    }

    // Update smart state
    updateSmartState();
}

QColor CameraCard::statusColor() const {
    return statusColor_;
}

void CameraCard::updateMacCombo(const QStringList& macs, const QString& current) {
    macCombo_->blockSignals(true);
    macCombo_->clear();
    macCombo_->addItem("None / Auto");
    macCombo_->addItems(macs);

    QString normalized = current.toUpper().remove(':');
    if (normalized.isEmpty()) {
        macCombo_->setCurrentText("None / Auto");
    } else {
        macCombo_->setCurrentText(current);
    }
    macCombo_->blockSignals(false);
}

void CameraCard::onFpsEnabledToggled(bool checked) {
    fpsSpin_->setEnabled(checked && editCheck_->isChecked());
}

void CameraCard::enterEvent(QEvent* event) {
    isHovered_ = true;
    updateElevation(true);
    QFrame::enterEvent(event);
}

void CameraCard::leaveEvent(QEvent* event) {
    isHovered_ = false;
    updateElevation(false);
    QFrame::leaveEvent(event);
}

void CameraCard::mousePressEvent(QMouseEvent* event) {
    // Toggle expand on header click
    if (event->button() == Qt::LeftButton) {
        QWidget* child = childAt(event->pos());
        if (child && child->parent() == this) {
            // Clicked on header area
            setExpanded(!isExpanded_);
        }
    }
    QFrame::mousePressEvent(event);
}

void CameraCard::updateElevation(bool hovered) {
    if (!shadowAnimation_) return;

    shadowAnimation_->stop();
    shadowAnimation_->setStartValue(shadowEffect_->blurRadius());
    shadowAnimation_->setEndValue(hovered ? 20.0 : 0.0);
    shadowAnimation_->start();

    // Update shadow color
    if (hovered) {
        shadowEffect_->setColor(QColor(0, 229, 255, 60));
        shadowEffect_->setOffset(0, 4);
    } else {
        shadowEffect_->setColor(QColor(0, 0, 0, 40));
        shadowEffect_->setOffset(0, 2);
    }
}

void CameraCard::applyTheme(const QColor& primaryColor, const QColor& accentColor) {
    primaryColor_ = primaryColor;
    accentColor_ = accentColor;

    // Update styles with new colors
    setupStyleSheet();

    // Refresh icons
    cameraIcon_->setPixmap(IconManager::instance().camera(24).pixmap(24, 24));
    expandButton_->setIcon(isExpanded_
        ? IconManager::instance().collapse(16)
        : IconManager::instance().expand(16));
}

// Property accessors for animations
void CameraCard::setContentHeight(int height) {
    contentHeight_ = height;
    if (contentWidget_) {
        contentWidget_->setFixedHeight(height);
        contentWidget_->setVisible(height > 0);
    }
}

qreal CameraCard::shadowBlurRadius() const {
    return shadowEffect_ ? shadowEffect_->blurRadius() : 0;
}

void CameraCard::setShadowBlurRadius(qreal radius) {
    if (shadowEffect_) {
        shadowEffect_->setBlurRadius(radius);
    }
}

QColor CameraCard::shadowColor() const {
    return shadowEffect_ ? shadowEffect_->color() : QColor();
}

void CameraCard::setShadowColor(const QColor& color) {
    if (shadowEffect_) {
        shadowEffect_->setColor(color);
    }
}

// Data getters
int CameraCard::sourceType() const {
    return sourceCombo_->currentData().toInt();
}

QString CameraCard::name() const {
    return nameEdit_->text();
}

QString CameraCard::location() const {
    return locationEdit_->text();
}

QString CameraCard::side() const {
    return sideCombo_->currentText();
}

int CameraCard::position() const {
    return positionSpin_->value();
}

QString CameraCard::ipAddress() const {
    return ipLabel_->text();
}

QString CameraCard::macAddress() const {
    return macCombo_->currentText();
}

QString CameraCard::subnetMask() const {
    return subnetEdit_->text();
}

QString CameraCard::gateway() const {
    return gatewayEdit_->text();
}

int CameraCard::fps() const {
    return fpsSpin_->value();
}

bool CameraCard::isAcquisitionFpsEnabled() const {
    return enableFpsCheck_->isChecked();
}
