#include "ConfigDialog.h"
#include "widgets/CameraCard.h"
#include "widgets/CameraDeviceSettingsDialog.h"
#include "widgets/NetworkSummaryHeader.h"
#include "widgets/DeleteConfirmationDialog.h"
#include "widgets/IconManager.h"
#include "../config/CameraConfig.h"
#include "../core/CameraManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFormLayout>
#include <QPushButton>
#include <QListWidget>
#include <QStackedWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QApplication>
#include <QScrollArea>
#include <QGroupBox>
#include <QFrame>
#include <QEvent>
#include <QDateTime>
#include <QDebug>
#include <QMap>
#include <QSet>
#include <QGridLayout>
#include <algorithm>

namespace {
    QString normalizeIp(const QString& ip) {
        return ip.trimmed();
    }

    QString normalizeMac(const QString& mac) {
        QString normalized;
        normalized.reserve(mac.size());
        for (const QChar ch : mac) {
            if (ch.isLetterOrNumber()) {
                normalized.append(ch.toUpper());
            }
        }
        return normalized;
    }

    bool cameraConfigEqual(const CameraInfo& lhs, const CameraInfo& rhs) {
        return lhs.id == rhs.id
            && lhs.source == rhs.source
            && lhs.name == rhs.name
            && lhs.location == rhs.location
            && lhs.side == rhs.side
            && lhs.machinePosition == rhs.machinePosition
            && normalizeIp(lhs.ipAddress) == normalizeIp(rhs.ipAddress)
            && normalizeMac(lhs.macAddress) == normalizeMac(rhs.macAddress)
            && normalizeIp(lhs.subnetMask) == normalizeIp(rhs.subnetMask)
            && normalizeIp(lhs.defaultGateway) == normalizeIp(rhs.defaultGateway)
            && lhs.fps == rhs.fps
            && lhs.enableAcquisitionFps == rhs.enableAcquisitionFps
            && lhs.width == rhs.width
            && lhs.height == rhs.height
            && lhs.offsetX == rhs.offsetX
            && lhs.offsetY == rhs.offsetY
            && lhs.pixelFormat == rhs.pixelFormat
            && lhs.exposureTimeAbs == rhs.exposureTimeAbs
            && lhs.enableExposureTimeBase == rhs.enableExposureTimeBase
            && lhs.exposureTimeBaseAbs == rhs.exposureTimeBaseAbs
            && lhs.exposureTimeRaw == rhs.exposureTimeRaw
            && lhs.chunkModeActive == rhs.chunkModeActive
            && lhs.enabledChunks == rhs.enabledChunks;
    }

    bool cameraConfigListEqual(const std::vector<CameraInfo>& lhs, const std::vector<CameraInfo>& rhs) {
        if (lhs.size() != rhs.size()) {
            return false;
        }

        for (size_t i = 0; i < lhs.size(); ++i) {
            if (!cameraConfigEqual(lhs[i], rhs[i])) {
                return false;
            }
        }

        return true;
    }

    void persistCameraNetworkSelection(int cameraId, int source, const QString& ip, const QString& mac,
                                       const QString& mask, const QString& gateway) {
        std::vector<CameraInfo> cameras = CameraConfig::getCameras();
        for (auto& cam : cameras) {
            if (cam.id != cameraId) continue;

            cam.source = source;
            cam.ipAddress = normalizeIp(ip);
            cam.macAddress = normalizeMac(mac);
            cam.subnetMask = normalizeIp(mask);
            cam.defaultGateway = normalizeIp(gateway);
            CameraConfig::saveCameras(cameras);
            return;
        }
    }

    QString joinCameraIds(const QList<int>& ids) {
        QStringList parts;
        for (int id : ids) {
            parts.append(QString::number(id));
        }
        return parts.join(", ");
    }
}

bool ConfigDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == cameraScrollWidget_ && event->type() == QEvent::Resize) {
        relayoutCameraCards();
    }

    if (event->type() == QEvent::Wheel && qobject_cast<QSpinBox*>(obj)) {
        event->ignore();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

ConfigDialog::ConfigDialog(CameraManager* cameraManager, QWidget *parent)
    : QWidget(parent)
    , cameraManager_(cameraManager)
    , networkSummaryHeader_(nullptr)
    , isAdminMode_(false)
    , primaryColor_("#E3E3E3")
    , accentColor_("#00E5FF") {

    setWindowTitle("System Configuration");
    resize(1440, 860);

    currentGigEDevices_ = CameraManager::enumerateGigEDevices();

    setupUI();
    loadSettings();
}

ConfigDialog::~ConfigDialog() = default;

void ConfigDialog::setupUI() {
    constexpr int kPageMargin = 16;
    constexpr int kSectionSpacing = 16;
    constexpr int kControlSpacing = 12;
    constexpr int kSidebarWidth = 184;

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(kSectionSpacing);
    mainLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);

    ThemeColors tc = CameraConfig::getThemeColors();
    
    // Create list widget for sidebar navigation
    QListWidget* sidebar = new QListWidget(this);
    sidebar->setFixedWidth(kSidebarWidth);
    sidebar->setStyleSheet(QString(
        "QListWidget { "
        "  background-color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 8px; "
        "  outline: 0; "
        "} "
        "QListWidget::item { "
        "  padding: 10px 12px; "
        "  color: %3; "
        "  font-size: 14px; "
        "  font-weight: 600; "
        "  border-bottom: 1px solid %2; "
        "} "
        "QListWidget::item:selected { "
        "  background-color: %4; "
        "  border-left: 4px solid %5; "
        "} "
        "QListWidget::item:hover:!selected { "
        "  background-color: %4; "
        "}"
    ).arg(tc.btnBg, tc.border, tc.text, tc.bg, tc.primary));

    QStackedWidget* stackedWidget = new QStackedWidget(this);
    stackedWidget->setStyleSheet("QStackedWidget { background: transparent; }");

    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(kSectionSpacing);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->addWidget(sidebar);
    contentLayout->addWidget(stackedWidget, 1);


    // Camera Configuration Tab
    QWidget* camSetupGroup = new QWidget(this);
    QVBoxLayout* camSetupLayout = new QVBoxLayout(camSetupGroup);
    camSetupLayout->setSpacing(kSectionSpacing);
    camSetupLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);

    // Premium Network Summary Header
    networkSummaryHeader_ = new NetworkSummaryHeader(this);
    connect(networkSummaryHeader_, &NetworkSummaryHeader::refreshRequested,
            this, &ConfigDialog::onRefreshLogsClicked);
    // Removed clear/toggle signals as they belong to diagnostics tab now
    connect(networkSummaryHeader_, &NetworkSummaryHeader::addCameraRequested,
            this, &ConfigDialog::onAddCameraConfigClicked);
    camSetupLayout->addWidget(networkSummaryHeader_);

    // Scroll area for camera cards
    cameraScrollArea_ = new QScrollArea(this);
    cameraScrollArea_->setWidgetResizable(true);
    cameraScrollArea_->setFrameShape(QFrame::NoFrame);
    cameraScrollArea_->setStyleSheet(QString(
        "QScrollArea { border: none; background: transparent; } "
        "QScrollBar:vertical { "
        "  background-color: %1; "
        "  width: 12px; "
        "  border-radius: 6px; "
        "} "
        "QScrollBar::handle:vertical { "
        "  background-color: %2; "
        "  border-radius: 6px; "
        "  min-height: 30px; "
        "} "
        "QScrollBar::handle:vertical:hover { "
        "  background-color: %3; "
        "} "
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { "
        "  height: 0px; "
        "}"
    ).arg(tc.bg, tc.btnBg, tc.primary));

    cameraScrollWidget_ = new QWidget();
    cameraListLayout_ = new QGridLayout(cameraScrollWidget_);
    cameraListLayout_->setSpacing(kSectionSpacing);
    cameraListLayout_->setContentsMargins(0, 0, 8, 0);
    cameraListLayout_->setAlignment(Qt::AlignTop);
    cameraScrollWidget_->installEventFilter(this);

    cameraScrollArea_->setWidget(cameraScrollWidget_);
    camSetupLayout->addWidget(cameraScrollArea_, 1);

    QListWidgetItem* camSetupItem = new QListWidgetItem(IconManager::instance().settings(20), "Camera Configuration");
    sidebar->addItem(camSetupItem);
    stackedWidget->addWidget(camSetupGroup);

    // Recording & Triggers Tab
    QWidget* bufferGroup = new QWidget(this);
    QVBoxLayout* bufferLayout = new QVBoxLayout(bufferGroup);
    bufferLayout->setSpacing(kSectionSpacing);
    bufferLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);

    const QString sectionStyle = QString(
        "QGroupBox { font-weight: 600; color: %1; border: 1px solid %2; "
        "border-radius: 8px; margin-top: 8px; padding-top: 8px; font-size: 12px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
    ).arg(tc.primary, tc.border);

    auto createSectionForm = [&](const QString& title) {
        QGroupBox* group = new QGroupBox(title, bufferGroup);
        group->setStyleSheet(sectionStyle);
        QFormLayout* form = new QFormLayout(group);
        form->setSpacing(kControlSpacing);
        form->setContentsMargins(14, 18, 14, 14);
        form->setHorizontalSpacing(kSectionSpacing);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bufferLayout->addWidget(group);
        return form;
    };

    QFormLayout* recordingForm = createSectionForm("Recording");

    // Global FPS
    globalFpsSpin_ = new QSpinBox(bufferGroup);
    globalFpsSpin_->setRange(1, 200);
    globalFpsSpin_->setSuffix(" fps");
    globalFpsSpin_->setStyleSheet(QString(
        "QSpinBox { "
        "  background-color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 6px; "
        "  padding: 6px 10px; "
        "  color: %3; "
        "  min-width: 100px; "
        "} "
        "QSpinBox:hover { border-color: %4; } "
        "QSpinBox:focus { border-color: %4; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));
    recordingForm->addRow("Fallback FPS:", globalFpsSpin_);

    // Pre-Trigger
    preTriggerSpin_ = new QSpinBox(bufferGroup);
    preTriggerSpin_->setRange(1, 60);
    preTriggerSpin_->setSuffix(" sec");
    preTriggerSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    recordingForm->addRow("Pre-Trigger Buffer:", preTriggerSpin_);

    // Post-Trigger
    postTriggerSpin_ = new QSpinBox(bufferGroup);
    postTriggerSpin_->setRange(1, 60);
    postTriggerSpin_->setSuffix(" sec");
    postTriggerSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    recordingForm->addRow("Post-Trigger Recording:", postTriggerSpin_);

    QFormLayout* retentionForm = createSectionForm("Record Storage");

    eventRetentionSpin_ = new QSpinBox(bufferGroup);
    eventRetentionSpin_->setRange(1, 10000);
    eventRetentionSpin_->setSuffix(" records");
    eventRetentionSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    retentionForm->addRow("Keep Recent Records:", eventRetentionSpin_);

    QFormLayout* triggerForm = createSectionForm("Triggering");

    QLabel* defectNote = new QLabel("Defect trigger is controlled from the Live screen for immediate operation.", bufferGroup);
    defectNote->setWordWrap(true);
    defectNote->setStyleSheet(QString("color: %1; padding-top: 4px;").arg(tc.text));
    triggerForm->addRow("Defect Trigger:", defectNote);

    bufferLayout->addStretch();

    QListWidgetItem* globalGroupItem = new QListWidgetItem(IconManager::instance().warning(20), "Recording & Triggers");
    sidebar->addItem(globalGroupItem);
    stackedWidget->addWidget(bufferGroup);

    // UI Preferences Tab
    QWidget* uiGroup = new QWidget(this);
    QFormLayout* uiLayout = new QFormLayout(uiGroup);
    uiLayout->setSpacing(kControlSpacing);
    uiLayout->setHorizontalSpacing(kSectionSpacing);
    uiLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    uiLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    themeCombo_ = new QComboBox(uiGroup);
    themeCombo_->addItem("Industrial Dark - Cyan", 0);
    themeCombo_->addItem("Classic Dark - Blue", 1);
    themeCombo_->addItem("High Contrast - Orange", 2);
    themeCombo_->addItem("Warning State - Yellow", 3);
    themeCombo_->addItem("Precision - Green", 4);
    themeCombo_->addItem("Visionary - Purple", 5);
    themeCombo_->addItem("Alert - Deep Red", 6);
    themeCombo_->addItem("Contrast Mono - Black & White", 7);
    themeCombo_->setStyleSheet(QString(
        "QComboBox { "
        "  background-color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 6px; "
        "  padding: 6px 10px; "
        "  color: %3; "
        "  min-width: 200px; "
        "} "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; width: 24px; } "
        "QComboBox::down-arrow { image: none; border: none; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));
    uiLayout->addRow("Color Theme:", themeCombo_);

    QListWidgetItem* uiGroupItem = new QListWidgetItem(IconManager::instance().settings(20), "UI Preferences");
    sidebar->addItem(uiGroupItem);
    stackedWidget->addWidget(uiGroup);

    // Diagnostics Tab
    QWidget* diagnosticsGroup = new QWidget(this);
    QVBoxLayout* diagnosticsLayout = new QVBoxLayout(diagnosticsGroup);
    diagnosticsLayout->setSpacing(kSectionSpacing);
    diagnosticsLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);

    QGroupBox* diagLogsGroup = new QGroupBox("Connection Diagnostics", diagnosticsGroup);
    diagLogsGroup->setStyleSheet(QString(
        "QGroupBox { font-weight: 600; color: %1; border: 1px solid %2; "
        "border-radius: 8px; margin-top: 8px; padding-top: 8px; font-size: 12px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
    ).arg(tc.primary, tc.border));
    
    QVBoxLayout* diagLogsLayout = new QVBoxLayout(diagLogsGroup);
    diagLogsLayout->setSpacing(kControlSpacing);
    diagLogsLayout->setContentsMargins(14, 18, 14, 14);
    
    connectionLogsBrowser_ = new QTextEdit(diagLogsGroup);
    connectionLogsBrowser_->setReadOnly(true);
    connectionLogsBrowser_->setStyleSheet(QString(
        "QTextEdit { "
        "  background-color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 6px; "
        "  color: %3; "
        "  font-family: 'SF Mono', Monaco, Consolas, monospace; "
        "  font-size: 12px; "
        "  padding: 8px; "
        "}"
    ).arg(tc.bg, tc.border, tc.text));
    diagLogsLayout->addWidget(connectionLogsBrowser_);
    
    QHBoxLayout* diagBtnsLayout = new QHBoxLayout();
    diagBtnsLayout->setSpacing(kControlSpacing);
    
    QPushButton* diagRefreshBtn = new QPushButton("Refresh Network", diagLogsGroup);
    diagRefreshBtn->setIcon(IconManager::instance().refresh(16));
    diagRefreshBtn->setStyleSheet(QString(
        "QPushButton { "
        "  background-color: %1; "
        "  color: %2; "
        "  border: none; "
        "  border-radius: 6px; "
        "  padding: 6px 12px; "
        "  font-size: 12px; "
        "  font-weight: 500; "
        "} "
        "QPushButton:hover { background-color: %3; }"
    ).arg(tc.primary, tc.bg, tc.btnHover));
    connect(diagRefreshBtn, &QPushButton::clicked, this, &ConfigDialog::onRefreshLogsClicked);
    
    QPushButton* diagClearBtn = new QPushButton("Clear Logs", diagLogsGroup);
    diagClearBtn->setIcon(IconManager::instance().trash(16));
    diagClearBtn->setStyleSheet(QString(
        "QPushButton { "
        "  background-color: transparent; "
        "  color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 6px; "
        "  padding: 6px 12px; "
        "  font-size: 12px; "
        "  font-weight: 500; "
        "} "
        "QPushButton:hover { background-color: rgba(255, 90, 90, 0.1); border-color: #FF5A5A; color: #FF5A5A; }"
    ).arg(tc.text, tc.border));
    connect(diagClearBtn, &QPushButton::clicked, this, &ConfigDialog::onClearLogsClicked);
    
    diagBtnsLayout->addWidget(diagRefreshBtn);
    diagBtnsLayout->addWidget(diagClearBtn);
    diagBtnsLayout->addStretch();
    diagLogsLayout->addLayout(diagBtnsLayout);
    
    diagnosticsLayout->addWidget(diagLogsGroup);
    
    QListWidgetItem* diagnosticsItem = new QListWidgetItem(IconManager::instance().info(20), "Diagnostics");
    sidebar->addItem(diagnosticsItem);
    stackedWidget->addWidget(diagnosticsGroup);

    mainLayout->addLayout(contentLayout, 1);
    
    connect(sidebar, &QListWidget::currentRowChanged, stackedWidget, &QStackedWidget::setCurrentIndex);
    sidebar->setCurrentRow(0);

    // Bottom buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(kControlSpacing);
    btnLayout->setContentsMargins(0, 0, 0, 0);
    btnLayout->addStretch();

    QPushButton* closeBtn = new QPushButton("Close", this);
    closeBtn->setIcon(IconManager::instance().close(16));
    closeBtn->setStyleSheet(QString(
        "QPushButton { "
        "  background-color: transparent; "
        "  color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 8px; "
        "  padding: 8px 16px; "
        "  font-size: 13px; "
        "  font-weight: 500; "
        "} "
        "QPushButton:hover { "
        "  background-color: %3; "
        "  border-color: %4; "
        "}"
    ).arg(tc.text, tc.border, tc.btnHover, tc.primary));
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    btnLayout->addWidget(closeBtn);

    saveBtn_ = new QPushButton("Save and Apply Settings", this);
    saveBtn_->setIcon(IconManager::instance().save(16));
    saveBtn_->setDefault(true);
    saveBtn_->setStyleSheet(QString(
        "QPushButton { "
        "  background-color: %1; "
        "  color: %2; "
        "  border: none; "
        "  border-radius: 8px; "
        "  padding: 8px 16px; "
        "  font-size: 13px; "
        "  font-weight: 600; "
        "} "
        "QPushButton:hover { "
        "  background-color: %3; "
        "} "
        "QPushButton:pressed { "
        "  background-color: %1; "
        "}"
    ).arg(tc.primary, tc.bg, tc.btnHover));
    connect(saveBtn_, &QPushButton::clicked, this, &ConfigDialog::saveAndApply);
    btnLayout->addWidget(saveBtn_);

    mainLayout->addLayout(btnLayout);

    // Initialize logs
    onRefreshLogsClicked();
}

void ConfigDialog::loadSettings() {
    // Load camera configurations and create cards
    std::vector<CameraInfo> cameras = CameraConfig::getCameras();
    for (const auto& cam : cameras) {
        createCameraWidgetBlock(cam);
    }

    // Load global settings
    globalFpsSpin_->setValue(CameraConfig::getFps());
    preTriggerSpin_->setValue(CameraConfig::getPreTriggerSeconds());
    postTriggerSpin_->setValue(CameraConfig::getPostTriggerSeconds());
    eventRetentionSpin_->setValue(CameraConfig::getEventRetentionCount());
    int themeIdx = themeCombo_->findData(CameraConfig::getThemePreset());
    if (themeIdx != -1) themeCombo_->setCurrentIndex(themeIdx);

    // Initial network status update
    refreshNetworkStatus();
}

void ConfigDialog::createCameraWidgetBlock(const CameraInfo& cam) {
    CameraCard* card = new CameraCard(cam, cameraScrollWidget_);
    connectCameraCardSignals(card);

    cameraCards_.push_back(card);
    relayoutCameraCards();
}

void ConfigDialog::relayoutCameraCards() {
    if (!cameraListLayout_ || !cameraScrollWidget_) {
        return;
    }

    while (QLayoutItem* item = cameraListLayout_->takeAt(0)) {
        if (item->widget()) {
            item->widget()->setParent(cameraScrollWidget_);
        }
        delete item;
    }

    const int availableWidth = cameraScrollArea_ ? cameraScrollArea_->viewport()->width() : cameraScrollWidget_->width();

    int columnCount = 1;
    int columnMinWidth = 720;

    if (availableWidth >= 1400) {
        columnCount = 3;
        columnMinWidth = 420;
    } else if (availableWidth >= 980) {
        columnCount = 2;
        columnMinWidth = 0;
    }

    for (int i = 0; i < columnCount; ++i) {
        cameraListLayout_->setColumnStretch(i, 1);
        cameraListLayout_->setColumnMinimumWidth(i, columnMinWidth);
    }

    for (int index = columnCount; index < 4; ++index) {
        cameraListLayout_->setColumnStretch(index, 0);
        cameraListLayout_->setColumnMinimumWidth(index, 0);
    }

    for (int index = 0; index < static_cast<int>(cameraCards_.size()); ++index) {
        CameraCard* card = cameraCards_[index];
        const int row = index / columnCount;
        const int column = index % columnCount;
        cameraListLayout_->addWidget(card, row, column, Qt::AlignTop);
    }
}

void ConfigDialog::connectCameraCardSignals(CameraCard* card) {
    connect(card, &CameraCard::editToggled, this, &ConfigDialog::onCameraCardEditToggled);
    connect(card, &CameraCard::removeClicked, this, &ConfigDialog::onCameraCardRemoveClicked);
    connect(card, &CameraCard::sourceChanged, this, &ConfigDialog::onCameraCardSourceChanged);
    connect(card, &CameraCard::macChanged, this, &ConfigDialog::onCameraCardMacChanged);
    connect(card, &CameraCard::writeIpClicked, this, &ConfigDialog::onCameraCardWriteIpClicked);
    connect(card, &CameraCard::deviceSettingsClicked, this, &ConfigDialog::onCameraCardDeviceSettingsClicked);
}

CameraCard* ConfigDialog::findCameraCard(int cameraId) const {
    for (auto* card : cameraCards_) {
        if (card->cameraId() == cameraId) {
            return card;
        }
    }
    return nullptr;
}

CameraCard* ConfigDialog::findCameraCard(QObject* sender) const {
    for (auto* card : cameraCards_) {
        if (card == sender || card->findChild<QObject*>(sender->objectName()) == sender) {
            return card;
        }
    }
    return qobject_cast<CameraCard*>(sender);
}

void ConfigDialog::onCameraCardEditToggled(bool checked) {
    if (CameraCard* card = findCameraCard(sender())) {
        card->setEditable(checked && isAdminMode_);
    }
}

void ConfigDialog::onCameraCardRemoveClicked() {
    CameraCard* card = findCameraCard(sender());
    if (!card) return;

    // Show premium delete confirmation dialog
    DeleteConfirmationDialog dialog(
        QString("Camera %1: %2").arg(card->cameraId()).arg(card->name()),
        this
    );

    if (dialog.exec() == QDialog::Accepted) {
        // Remove from vector
        cameraCards_.erase(std::remove(cameraCards_.begin(), cameraCards_.end(), card),
                          cameraCards_.end());

        // Remove from UI
        cameraListLayout_->removeWidget(card);
        delete card;

        relayoutCameraCards();

        // Update network status
        refreshNetworkStatus();
    }
}

void ConfigDialog::onCameraCardSourceChanged(int) {
    refreshNetworkStatus();
}

void ConfigDialog::onCameraCardMacChanged(const QString&) {
    QSet<QString> reservedMacs;
    for (auto* card : cameraCards_) {
        const QString configuredMac = normalizeMac(card->macAddress());
        if (!configuredMac.isEmpty() && configuredMac != "NONE/AUTO") {
            reservedMacs.insert(configuredMac);
        }
    }

    for (auto* card : cameraCards_) {
        card->updateMacCombo(currentGigEDevices_, card->macAddress(), reservedMacs);
    }

    refreshNetworkStatus();
}

void ConfigDialog::onCameraCardWriteIpClicked() {
    CameraCard* card = findCameraCard(sender());
    if (!card) return;

    // Write IP logic (similar to original)
    if (card->sourceType() != 1) {
        QMessageBox::warning(this, "Write IP", "IP writing is only available for cameras configured as Real.");
        return;
    }

    QString mac = card->macAddress();
    QString ip = card->ipAddress();
    QString mask = card->subnetMask();
    QString gw = card->gateway();
    const QString normalizedMac = normalizeMac(mac);

    if (normalizedMac.isEmpty()) {
        QMessageBox::warning(this, "Write IP", "Please select or enter a valid MAC Address first.");
        return;
    }

    bool macVisible = false;
    for (const auto& dev : currentGigEDevices_) {
        if (normalizeMac(QString::fromStdString(dev.macAddress)) == normalizedMac) {
            macVisible = true;
            break;
        }
    }

    if (!macVisible) {
        QMessageBox::warning(this, "Write IP",
            "The selected MAC is not currently visible in GigE discovery. "
            "Refresh discovery and verify the physical camera is connected before writing its IP.");
        return;
    }

    persistCameraNetworkSelection(
        card->cameraId(),
        card->sourceType(),
        ip, mac, mask, gw
    );

    bool wasRunning = false;
    if (cameraManager_) {
        cameraManager_->stopAcquisition();
        wasRunning = true;
    }

    bool writeOk = CameraManager::applyIpConfiguration(
        normalizedMac.toStdString(),
        ip.toStdString(),
        mask.toStdString(),
        gw.toStdString()
    );

    if (!writeOk) {
        QMessageBox::critical(this, "Write IP",
            "Failed to write IP configuration to camera " + mac + ".\n"
            "Please check connection and MAC address.");
        if (wasRunning && cameraManager_) {
            cameraManager_->startAcquisition();
        }
        return;
    }

    onRefreshLogsClicked();
    refreshNetworkStatus();

    // Check if IP matches
    QString detectedIp = "Offline";
    for (const auto& dev : currentGigEDevices_) {
        if (normalizeMac(QString::fromStdString(dev.macAddress)) == normalizeMac(mac)) {
            detectedIp = QString::fromStdString(dev.ipAddress);
            break;
        }
    }

    if (normalizeIp(detectedIp) == normalizeIp(ip)) {
        QMessageBox::information(this, "Write IP",
            "Camera " + mac + " is now detected at " + ip + ".");
    } else {
        QMessageBox::warning(this, "Write IP",
            "IP write sent to camera " + mac + ", but it has not been rediscovered at " + ip + " yet.\n"
            "Refresh after reconnecting the camera if needed.");
    }

    if (wasRunning && cameraManager_) {
        cameraManager_->startAcquisition();
    }
}

void ConfigDialog::onAddCameraConfigClicked() {
    CameraInfo cam;

    int maxId = 0;
    for (auto* card : cameraCards_) {
        if (card->cameraId() > maxId) maxId = card->cameraId();
    }
    cam.id = maxId + 1;
    cam.source = 0;
    cam.name = QString("DRYER %1").arg(cam.id);
    cam.location = QString("CYLINDER %1").arg(10 + cam.id);
    cam.side = "DRIVE SIDE";
    cam.machinePosition = 16000 + (cam.id * 500);
    cam.ipAddress = QString("172.20.2.%1").arg(cam.id);
    cam.macAddress = "";
    cam.subnetMask = "255.255.255.0";
    cam.defaultGateway = "0.0.0.0";
    cam.fps = 50;
    cam.enableAcquisitionFps = false;
    cam.width = 780;
    cam.height = 580;
    cam.offsetX = 0;
    cam.offsetY = 0;
    cam.pixelFormat = "Mono8";
    cam.exposureTimeAbs = 40880.0;
    cam.enableExposureTimeBase = false;
    cam.exposureTimeBaseAbs = 20.0;
    cam.exposureTimeRaw = 2044;
    cam.chunkModeActive = false;
    cam.enabledChunks = QStringList() << "Timestamp" << "Framecounter";
    cam.temperature = 0.0;

    createCameraWidgetBlock(cam);

    // Scroll to the new card
    cameraScrollArea_->ensureWidgetVisible(cameraCards_.back());
}

void ConfigDialog::onCameraCardDeviceSettingsClicked() {
    CameraCard* card = findCameraCard(sender());
    if (!card) return;

    const auto it = std::find(cameraCards_.begin(), cameraCards_.end(), card);
    const int cameraIndex = it == cameraCards_.end()
        ? 0
        : static_cast<int>(std::distance(cameraCards_.begin(), it));

    CameraDeviceSettingsDialog dialog(cameraIndex, card->cameraInfo(), cameraManager_, isAdminMode_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    card->setCameraInfo(dialog.updatedInfo());
}

void ConfigDialog::onRemoveCameraConfigClicked() {
    // Handled by CameraCard signals
}

void ConfigDialog::saveAndApply() {
    qInfo() << "[ConfigDialog] Save requested. cardCount=" << cameraCards_.size();

    QStringList validationErrors;
    if (!validateConfiguration(&validationErrors)) {
        qWarning() << "[ConfigDialog] Validation failed:" << validationErrors;
        QMessageBox::warning(this, "Invalid Camera Configuration", validationErrors.join("\n"));
        return;
    }

    const std::vector<CameraInfo> previousCameras = CameraConfig::getCameras();

    // Gather all camera configs
    std::vector<CameraInfo> newCameras;
    for (auto* card : cameraCards_) {
        CameraInfo cam = card->cameraInfo();
        cam.id = card->cameraId();
        cam.source = card->sourceType();
        cam.name = card->name();
        cam.location = card->location();
        cam.side = card->side();
        cam.machinePosition = card->position();
        cam.ipAddress = card->ipAddress();
        cam.macAddress = normalizeMac(card->macAddress());
        if (cam.macAddress.isEmpty()) cam.macAddress = "";
        cam.subnetMask = card->subnetMask();
        cam.defaultGateway = card->gateway();
        cam.temperature = 0.0;
        qInfo() << "[ConfigDialog] Camera to save"
                << "id=" << cam.id
                << "source=" << cam.source
                << "name=" << cam.name
                << "ip=" << cam.ipAddress
                << "mac=" << cam.macAddress
                << "pixelFormat=" << cam.pixelFormat
                << "fps=" << cam.fps
                << "size=" << QString("%1x%2").arg(cam.width).arg(cam.height)
                << "offset=" << QString("%1,%2").arg(cam.offsetX).arg(cam.offsetY);
        newCameras.push_back(cam);
    }

    qInfo() << "[ConfigDialog] Persisting camera configuration";
    CameraConfig::saveCameras(newCameras);

    // Save global configs
    qInfo() << "[ConfigDialog] Persisting global settings"
            << "globalFps=" << globalFpsSpin_->value()
            << "preTrigger=" << preTriggerSpin_->value()
            << "postTrigger=" << postTriggerSpin_->value()
            << "retention=" << eventRetentionSpin_->value()
            << "theme=" << themeCombo_->currentData().toInt();
    CameraConfig::setFps(globalFpsSpin_->value());
    CameraConfig::setPreTriggerSeconds(preTriggerSpin_->value());
    CameraConfig::setPostTriggerSeconds(postTriggerSpin_->value());
    CameraConfig::setEventRetentionCount(eventRetentionSpin_->value());
    CameraConfig::setThemePreset(themeCombo_->currentData().toInt());

    const bool requiresCameraRestart = !cameraConfigListEqual(previousCameras, newCameras);
    qInfo() << "[ConfigDialog] Save complete. requiresCameraRestart=" << requiresCameraRestart;

    // Let the main window restart/reapply after the dialog save completes.
    // Avoid touching live camera node maps here because Save may also change
    // camera topology/network configuration in the same action.
    QMetaObject::invokeMethod(this, [this, requiresCameraRestart]() {
        qInfo() << "[ConfigDialog] Emitting configUpdated" << requiresCameraRestart;
        emit configUpdated(requiresCameraRestart);
    }, Qt::QueuedConnection);
}

void ConfigDialog::setAdminMode(bool isAdmin) {
    isAdminMode_ = isAdmin;

    // Global settings
    if (networkSummaryHeader_) {
        // Update add button visibility
    }
    globalFpsSpin_->setEnabled(isAdmin);
    preTriggerSpin_->setEnabled(isAdmin);
    postTriggerSpin_->setEnabled(isAdmin);
    eventRetentionSpin_->setEnabled(isAdmin);
    saveBtn_->setEnabled(isAdmin);

    // Per-camera: only the edit checkbox is admin-gated
    for (auto* card : cameraCards_) {
        // Card handles its own edit state
    }
}

void ConfigDialog::onRefreshLogsClicked() {
    networkSummaryHeader_->setRefreshing(true);

    currentGigEDevices_ = CameraManager::enumerateGigEDevices();
    QString refreshTs = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    connectionLogsBrowser_->append(QString("--- Refresh at %1 ---").arg(refreshTs));

    static QMap<QString, QString> cameraConnectionTimes;

    for (const auto& dev : currentGigEDevices_) {
        QString mac = QString::fromStdString(dev.macAddress);

        if (!cameraConnectionTimes.contains(mac)) {
            cameraConnectionTimes.insert(mac, QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
        }

        QString devTs = cameraConnectionTimes.value(mac);
        connectionLogsBrowser_->append(
            QString("[%1] %2 | MAC: %3 | IP: %4 | Subnet: %5 | Gateway: %6")
            .arg(devTs)
            .arg(QString::fromStdString(dev.friendlyName))
            .arg(QString::fromStdString(dev.macAddress))
            .arg(QString::fromStdString(dev.ipAddress))
            .arg(QString::fromStdString(dev.subnetMask))
            .arg(QString::fromStdString(dev.defaultGateway))
        );
    }

    if (currentGigEDevices_.empty()) {
        cameraConnectionTimes.clear();
        QString emptyTs = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        connectionLogsBrowser_->append(QString("[%1] No online Real cameras detected.").arg(emptyTs));
    }

    QSet<QString> reservedMacs;
    for (auto* card : cameraCards_) {
        const QString configuredMac = normalizeMac(card->macAddress());
        if (!configuredMac.isEmpty() && configuredMac != "NONE/AUTO") {
            reservedMacs.insert(configuredMac);
        }
    }

    for (auto* card : cameraCards_) {
        card->updateMacCombo(currentGigEDevices_, card->macAddress(), reservedMacs);
    }

    refreshNetworkStatus();
    networkSummaryHeader_->setRefreshing(false);
}

void ConfigDialog::onClearLogsClicked() {
    connectionLogsBrowser_->clear();
}

void ConfigDialog::onToggleLogsClicked() {
    // Left empty or we can remove the slot. Currently not used as logs are always visible in Diagnostics tab.
}

void ConfigDialog::onOpenIpConfiguratorClicked() {
    QProcess::startDetached("/opt/pylon/bin/IpConfigurator", QStringList());
}

bool ConfigDialog::validateConfiguration(QStringList* errors) const {
    QMap<QString, QList<int>> ipUsage;
    QMap<QString, QList<int>> macUsage;

    for (auto* card : cameraCards_) {
        int source = card->sourceType();
        if (source == 2) continue;

        QString configuredIp = normalizeIp(card->ipAddress());
        if (!configuredIp.isEmpty()) {
            ipUsage[configuredIp].append(card->cameraId());
        }

        if (source == 1) {
            QString configuredMac = normalizeMac(card->macAddress());
            if (configuredMac.isEmpty() || configuredMac == "NONE/AUTO") {
                if (errors) {
                    errors->append(QString("Camera ID %1 is set to Real but has no MAC assigned.").arg(card->cameraId()));
                }
            } else {
                macUsage[configuredMac].append(card->cameraId());
            }
        }
    }

    for (auto it = ipUsage.cbegin(); it != ipUsage.cend(); ++it) {
        if (it.value().size() > 1 && errors) {
            errors->append(QString("Configured IP %1 is assigned to multiple camera IDs (%2).")
                          .arg(it.key(), joinCameraIds(it.value())));
        }
    }

    for (auto it = macUsage.cbegin(); it != macUsage.cend(); ++it) {
        if (it.value().size() > 1 && errors) {
            errors->append(QString("MAC %1 is assigned to multiple camera IDs (%2).")
                          .arg(it.key(), joinCameraIds(it.value())));
        }
    }

    return !errors || errors->isEmpty();
}

void ConfigDialog::refreshNetworkStatus() {
    QMap<QString, QList<QString>> liveIpToMacs;
    QMap<QString, GigEDeviceInfo> macToDevice;

    for (const auto& dev : currentGigEDevices_) {
        QString ip = normalizeIp(QString::fromStdString(dev.ipAddress));
        QString mac = normalizeMac(QString::fromStdString(dev.macAddress));
        if (!mac.isEmpty()) {
            macToDevice.insert(mac, dev);
        }
        if (!ip.isEmpty()) {
            liveIpToMacs[ip].append(mac);
        }
    }

    QSet<QString> duplicateConfiguredIps;
    QSet<QString> duplicateConfiguredMacs;
    QMap<QString, int> configuredIpCounts;
    QMap<QString, int> configuredMacCounts;

    for (auto* card : cameraCards_) {
        if (card->sourceType() == 2) continue;

        QString configuredIp = normalizeIp(card->ipAddress());
        if (!configuredIp.isEmpty()) {
            configuredIpCounts[configuredIp] += 1;
        }

        if (card->sourceType() == 1) {
            QString configuredMac = normalizeMac(card->macAddress());
            if (!configuredMac.isEmpty() && configuredMac != "NONE/AUTO") {
                configuredMacCounts[configuredMac] += 1;
            }
        }
    }

    for (auto it = configuredIpCounts.cbegin(); it != configuredIpCounts.cend(); ++it) {
        if (it.value() > 1) {
            duplicateConfiguredIps.insert(it.key());
        }
    }

    for (auto it = configuredMacCounts.cbegin(); it != configuredMacCounts.cend(); ++it) {
        if (it.value() > 1) {
            duplicateConfiguredMacs.insert(it.key());
        }
    }

    int mismatchCount = 0;
    int missingCount = 0;
    int blockingCount = 0;
    bool liveDuplicateSeen = false;
    int onlineCount = 0;
    int warningCount = 0;
    int errorCount = 0;
    int offlineCount = 0;

    for (auto* card : cameraCards_) {
        int source = card->sourceType();
        QString configuredIp = normalizeIp(card->ipAddress());
        QString configuredMac = normalizeMac(card->macAddress());

        QString detectedIp = "Offline";
        QString statusText = "Disabled";
        QColor statusColor("#888888");

        if (source != 2) {
            statusText = "Unassigned MAC";
            statusColor = QColor("#E0A800");

            if (duplicateConfiguredIps.contains(configuredIp)) {
                statusText = "Duplicate IP";
                statusColor = QColor("#FF5A5A");
                blockingCount++;
                errorCount++;
            } else if (source == 1 && (configuredMac.isEmpty() || configuredMac == "NONE/AUTO")) {
                missingCount++;
                warningCount++;
            } else if (source == 1 && duplicateConfiguredMacs.contains(configuredMac)) {
                statusText = "Duplicate MAC";
                statusColor = QColor("#FF5A5A");
                blockingCount++;
                errorCount++;
            } else if (source == 0) {
                statusText = "Emulated";
                statusColor = QColor("#4CAF50");
                onlineCount++;
            } else if (macToDevice.contains(configuredMac)) {
                const GigEDeviceInfo& dev = macToDevice[configuredMac];
                detectedIp = QString::fromStdString(dev.ipAddress);
                QString normalizedDetectedIp = normalizeIp(detectedIp);

                if (liveIpToMacs.value(normalizedDetectedIp).size() > 1) {
                    statusText = "Duplicate live IP";
                    statusColor = QColor("#FF5A5A");
                    liveDuplicateSeen = true;
                    blockingCount++;
                    errorCount++;
                } else if (normalizedDetectedIp == configuredIp) {
                    statusText = "Online";
                    statusColor = QColor("#4CAF50");
                    onlineCount++;
                } else {
                    statusText = "IP mismatch";
                    statusColor = QColor("#E0A800");
                    mismatchCount++;
                    warningCount++;
                }
            } else if (source == 1) {
                statusText = "Offline";
                statusColor = QColor("#6E7681");
                missingCount++;
                offlineCount++;
            }
        } else {
            offlineCount++;
        }

        card->setDetectedIp(detectedIp);
        card->setStatus(statusText, statusColor);
    }

    // Update network summary header
    int totalCount = cameraCards_.size();
    networkSummaryHeader_->setCameraCounts(totalCount, onlineCount, warningCount, errorCount, offlineCount);

    // Update summary text
    QStringList summary;
    QColor summaryColor = QColor("#4CAF50");

    if (blockingCount > 0) {
        summaryColor = QColor("#FF5A5A");
        if (!duplicateConfiguredIps.isEmpty()) {
            summary << "Duplicate configured IPs detected";
        }
        if (!duplicateConfiguredMacs.isEmpty()) {
            summary << "Duplicate configured MACs detected";
        }
        if (liveDuplicateSeen) {
            summary << "Duplicate live IP detected";
        }
    }

    if (mismatchCount > 0) {
        if (summaryColor != QColor("#FF5A5A")) {
            summaryColor = QColor("#E0A800");
        }
        summary << QString("%1 camera%2 have IP mismatch").arg(mismatchCount).arg(mismatchCount == 1 ? "" : "s");
    }

    if (missingCount > 0) {
        if (summaryColor == QColor("#4CAF50")) {
            summaryColor = QColor("#E0A800");
        }
        summary << QString("%1 camera%2 not visible").arg(missingCount).arg(missingCount == 1 ? "" : "s");
    }

    if (summary.isEmpty()) {
        networkSummaryHeader_->setNetworkStatus("Network OK: All cameras configured correctly", QColor("#4CAF50"));
    } else {
        networkSummaryHeader_->setNetworkStatus(summary.join(" | "), summaryColor);
    }
}
