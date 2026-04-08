#include "ConfigDialog.h"
#include "widgets/CameraCard.h"
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
    setWindowFlags(Qt::Window);
    resize(800, 900);

    currentGigEDevices_ = CameraManager::enumerateGigEDevices();

    setupUI();
    loadSettings();
}

ConfigDialog::~ConfigDialog() = default;

void ConfigDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(20);
    mainLayout->setContentsMargins(24, 24, 24, 24);

    ThemeColors tc = CameraConfig::getThemeColors();
    
    // Create list widget for sidebar navigation
    QListWidget* sidebar = new QListWidget(this);
    sidebar->setFixedWidth(220);
    sidebar->setStyleSheet(QString(
        "QListWidget { "
        "  background-color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 8px; "
        "  outline: 0; "
        "} "
        "QListWidget::item { "
        "  padding: 12px 16px; "
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
    
    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->addWidget(sidebar);
    contentLayout->addWidget(stackedWidget, 1);


    // Camera Configuration Tab
    QWidget* camSetupGroup = new QWidget(this);
    QVBoxLayout* camSetupLayout = new QVBoxLayout(camSetupGroup);
    camSetupLayout->setSpacing(16);
    camSetupLayout->setContentsMargins(16, 16, 16, 16);

    // Premium Network Summary Header
    networkSummaryHeader_ = new NetworkSummaryHeader(this);
    connect(networkSummaryHeader_, &NetworkSummaryHeader::refreshRequested,
            this, &ConfigDialog::onRefreshLogsClicked);
    connect(networkSummaryHeader_, &NetworkSummaryHeader::clearLogsRequested,
            this, &ConfigDialog::onClearLogsClicked);
    connect(networkSummaryHeader_, &NetworkSummaryHeader::toggleLogsRequested,
            this, &ConfigDialog::onToggleLogsClicked);
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
    cameraListLayout_ = new QVBoxLayout(cameraScrollWidget_);
    cameraListLayout_->setSpacing(16);
    cameraListLayout_->setAlignment(Qt::AlignTop);
    cameraListLayout_->setContentsMargins(0, 8, 12, 8);

    cameraScrollArea_->setWidget(cameraScrollWidget_);
    camSetupLayout->addWidget(cameraScrollArea_, 1);

    // Camera Connection Logs
    logsGroup_ = new QGroupBox("Camera Connection Logs", this);
    logsGroup_->setStyleSheet(QString(
        "QGroupBox { font-weight: 600; color: %1; border: 1px solid %2; "
        "border-radius: 8px; margin-top: 12px; padding-top: 8px; font-size: 12px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 8px; }"
    ).arg(tc.primary, tc.border));
    QVBoxLayout* logsLayout = new QVBoxLayout(logsGroup_);
    logsLayout->setContentsMargins(12, 16, 12, 12);

    connectionLogsBrowser_ = new QTextEdit(this);
    connectionLogsBrowser_->setReadOnly(true);
    connectionLogsBrowser_->setMinimumHeight(120);
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
    logsLayout->addWidget(connectionLogsBrowser_);
    
    // Hide logs group by default
    logsGroup_->setVisible(false);
    
    camSetupLayout->addWidget(logsGroup_, 0);

    QListWidgetItem* camSetupItem = new QListWidgetItem(IconManager::instance().settings(20), "Camera Configuration");
    sidebar->addItem(camSetupItem);
    stackedWidget->addWidget(camSetupGroup);

    // Global Recording & Events Tab
    QWidget* bufferGroup = new QWidget(this);
    QFormLayout* bufferLayout = new QFormLayout(bufferGroup);
    bufferLayout->setSpacing(16);
    bufferLayout->setContentsMargins(16, 16, 16, 16);

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
    bufferLayout->addRow("Global Target FPS (fallback):", globalFpsSpin_);

    // Pre-Trigger
    preTriggerSpin_ = new QSpinBox(bufferGroup);
    preTriggerSpin_->setRange(1, 60);
    preTriggerSpin_->setSuffix(" sec");
    preTriggerSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    bufferLayout->addRow("Pre-Trigger Buffer:", preTriggerSpin_);

    // Post-Trigger
    postTriggerSpin_ = new QSpinBox(bufferGroup);
    postTriggerSpin_->setRange(1, 60);
    postTriggerSpin_->setSuffix(" sec");
    postTriggerSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    bufferLayout->addRow("Post-Trigger Record:", postTriggerSpin_);

    // Defect Detection
    defectCheck_ = new ToggleSwitch(bufferGroup);
    bufferLayout->addRow("Enable Defect Detection (Auto-Trigger):", defectCheck_);

    QListWidgetItem* globalGroupItem = new QListWidgetItem(IconManager::instance().warning(20), "Global Recording & Events");
    sidebar->addItem(globalGroupItem);
    stackedWidget->addWidget(bufferGroup);

    // UI Preferences Tab
    QWidget* uiGroup = new QWidget(this);
    QFormLayout* uiLayout = new QFormLayout(uiGroup);
    uiLayout->setSpacing(16);
    uiLayout->setContentsMargins(16, 16, 16, 16);

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

    mainLayout->addLayout(contentLayout, 1);
    
    connect(sidebar, &QListWidget::currentRowChanged, stackedWidget, &QStackedWidget::setCurrentIndex);
    sidebar->setCurrentRow(0);

    // Bottom buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(12);
    btnLayout->addStretch();

    QPushButton* closeBtn = new QPushButton("Close", this);
    closeBtn->setIcon(IconManager::instance().close(16));
    closeBtn->setStyleSheet(QString(
        "QPushButton { "
        "  background-color: transparent; "
        "  color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 8px; "
        "  padding: 10px 20px; "
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
        "  padding: 10px 20px; "
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
    defectCheck_->setChecked(CameraConfig::isDefectDetectionEnabled());

    int themeIdx = themeCombo_->findData(CameraConfig::getThemePreset());
    if (themeIdx != -1) themeCombo_->setCurrentIndex(themeIdx);

    // Initial network status update
    refreshNetworkStatus();
}

void ConfigDialog::createCameraWidgetBlock(const CameraInfo& cam) {
    CameraCard* card = new CameraCard(cam, cameraScrollWidget_);
    connectCameraCardSignals(card);

    cameraListLayout_->addWidget(card);
    cameraCards_.push_back(card);
}

void ConfigDialog::connectCameraCardSignals(CameraCard* card) {
    connect(card, &CameraCard::editToggled, this, &ConfigDialog::onCameraCardEditToggled);
    connect(card, &CameraCard::removeClicked, this, &ConfigDialog::onCameraCardRemoveClicked);
    connect(card, &CameraCard::sourceChanged, this, &ConfigDialog::onCameraCardSourceChanged);
    connect(card, &CameraCard::macChanged, this, &ConfigDialog::onCameraCardMacChanged);
    connect(card, &CameraCard::writeIpClicked, this, &ConfigDialog::onCameraCardWriteIpClicked);
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

        // Update network status
        refreshNetworkStatus();
    }
}

void ConfigDialog::onCameraCardSourceChanged(int) {
    refreshNetworkStatus();
}

void ConfigDialog::onCameraCardMacChanged(const QString&) {
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
    cam.temperature = 0.0;

    createCameraWidgetBlock(cam);

    // Scroll to the new card
    cameraScrollArea_->ensureWidgetVisible(cameraCards_.back());
}

void ConfigDialog::onRemoveCameraConfigClicked() {
    // Handled by CameraCard signals
}

void ConfigDialog::saveAndApply() {
    QStringList validationErrors;
    if (!validateConfiguration(&validationErrors)) {
        QMessageBox::warning(this, "Invalid Camera Configuration", validationErrors.join("\n"));
        return;
    }

    // Gather all camera configs
    std::vector<CameraInfo> newCameras;
    for (auto* card : cameraCards_) {
        CameraInfo cam;
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
        cam.fps = card->fps();
        cam.enableAcquisitionFps = card->isAcquisitionFpsEnabled();
        cam.temperature = 0.0;
        newCameras.push_back(cam);
    }

    CameraConfig::saveCameras(newCameras);

    // Save global configs
    CameraConfig::setFps(globalFpsSpin_->value());
    CameraConfig::setPreTriggerSeconds(preTriggerSpin_->value());
    CameraConfig::setPostTriggerSeconds(postTriggerSpin_->value());
    CameraConfig::setDefectDetectionEnabled(defectCheck_->isChecked());
    CameraConfig::setThemePreset(themeCombo_->currentData().toInt());

    // Save .pfs for all real connected cameras
    if (cameraManager_) {
        cameraManager_->saveParametersForAll(newCameras);
    }

    emit configUpdated();
    close();
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
    defectCheck_->setEnabled(isAdmin);
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

    // Update MAC Address dropdowns in camera cards
    QStringList availableMacs;
    for (const auto& dev : currentGigEDevices_) {
        availableMacs.append(QString::fromStdString(dev.macAddress));
    }

    for (auto* card : cameraCards_) {
        card->updateMacCombo(availableMacs, card->macAddress());
    }

    refreshNetworkStatus();
    networkSummaryHeader_->setRefreshing(false);
}

void ConfigDialog::onClearLogsClicked() {
    connectionLogsBrowser_->clear();
}

void ConfigDialog::onToggleLogsClicked() {
    logsGroup_->setVisible(!logsGroup_->isVisible());
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
