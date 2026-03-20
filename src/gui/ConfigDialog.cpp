#include "ConfigDialog.h"
#include "../config/CameraConfig.h"
#include "../core/CameraManager.h"
#include "widgets/ToggleSwitch.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFormLayout>
#include <QPushButton>
#include <QToolBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QApplication>
#include <QScrollArea>
#include <QGroupBox>
#include <QFrame>
#include <QEvent>
#include <QDateTime>
#include <algorithm>

// Suppress scroll-wheel on ANY QSpinBox child widget to prevent accidental changes
bool ConfigDialog::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Wheel && qobject_cast<QSpinBox*>(obj)) {
        // Pass the wheel event to the scroll area so the page still scrolls
        event->ignore();
        return true; // consumed
    }
    return QWidget::eventFilter(obj, event);
}


ConfigDialog::ConfigDialog(QWidget *parent) : QWidget(parent) {
    setWindowTitle("System Configuration");
    
    // Set as an independent window that deletes itself when closed
    setWindowFlags(Qt::Window);
    resize(600, 700);
    
    // Get gigE devices before setting up UI
    currentGigEDevices_ = CameraManager::enumerateGigEDevices();
    
    setupUI();
    loadSettings();
}

void ConfigDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QToolBox* toolBox = new QToolBox(this);
    
    // --- Camera Setup Group (New Dynamic Config) ---
    QWidget* camSetupGroup = new QWidget(this);
    QVBoxLayout* camSetupLayout = new QVBoxLayout(camSetupGroup);
    
    QHBoxLayout* topCamLayout = new QHBoxLayout();
    topCamLayout->addWidget(new QLabel("Configure per-camera settings below:"));
    addCameraBtn_ = new QPushButton("Add New Camera +");
    connect(addCameraBtn_, &QPushButton::clicked, this, &ConfigDialog::onAddCameraConfigClicked);
    
    QPushButton* ipConfigBtn = new QPushButton("Open IP Configurator");
    connect(ipConfigBtn, &QPushButton::clicked, this, &ConfigDialog::onOpenIpConfiguratorClicked);
    
    topCamLayout->addWidget(addCameraBtn_);
    topCamLayout->addWidget(ipConfigBtn);
    topCamLayout->addStretch();
    camSetupLayout->addLayout(topCamLayout);
    
    cameraScrollArea_ = new QScrollArea();
    cameraScrollArea_->setWidgetResizable(true);
    cameraScrollArea_->setFrameShape(QFrame::NoFrame);
    
    cameraScrollWidget_ = new QWidget();
    cameraListLayout_ = new QVBoxLayout(cameraScrollWidget_);
    cameraListLayout_->setAlignment(Qt::AlignTop);
    cameraScrollArea_->setWidget(cameraScrollWidget_);
    
    // --- Camera Connection Logs section ---
    QGroupBox* logsGroup = new QGroupBox("Camera Connection Logs");
    QVBoxLayout* logsLayout = new QVBoxLayout(logsGroup);
    
    QHBoxLayout* logsHeaderLayout = new QHBoxLayout();
    QPushButton* refreshLogsBtn = new QPushButton("Refresh Logs");
    QPushButton* clearLogsBtn = new QPushButton("Clear Logs");
    QPushButton* toggleLogsBtn = new QPushButton("Hide Logs");
    connect(refreshLogsBtn, &QPushButton::clicked, this, &ConfigDialog::onRefreshLogsClicked);
    connect(clearLogsBtn, &QPushButton::clicked, this, &ConfigDialog::onClearLogsClicked);
    logsHeaderLayout->addStretch();
    logsHeaderLayout->addWidget(refreshLogsBtn);
    logsHeaderLayout->addWidget(clearLogsBtn);
    logsHeaderLayout->addWidget(toggleLogsBtn);
    logsLayout->addLayout(logsHeaderLayout);
    
    connectionLogsBrowser_ = new QTextEdit();
    connectionLogsBrowser_->setReadOnly(true);
    connectionLogsBrowser_->setMinimumHeight(120);
    logsLayout->addWidget(connectionLogsBrowser_);
    
    connect(toggleLogsBtn, &QPushButton::clicked, [this, toggleLogsBtn]() {
        if (connectionLogsBrowser_->isVisible()) {
            connectionLogsBrowser_->hide();
            toggleLogsBtn->setText("Show Logs");
        } else {
            connectionLogsBrowser_->show();
            toggleLogsBtn->setText("Hide Logs");
        }
    });
    
    // Call the refresh slot once to initialize logs
    onRefreshLogsClicked();
    
    camSetupLayout->addWidget(cameraScrollArea_, 1); // stretch
    camSetupLayout->addWidget(logsGroup, 0);

    toolBox->addItem(camSetupGroup, "Camera Configurations");
    
    // --- Event Buffer Settings Group (Global) ---
    QWidget* bufferGroup = new QWidget(this);
    QVBoxLayout* bufferLayout = new QVBoxLayout(bufferGroup);
    
    // Global FPS
    QHBoxLayout* fpsLayout = new QHBoxLayout();
    fpsLayout->addWidget(new QLabel("Global Target FPS (fallback):"));
    globalFpsSpin_ = new QSpinBox();
    globalFpsSpin_->setRange(1, 200);
    globalFpsSpin_->setSuffix(" fps");
    fpsLayout->addWidget(globalFpsSpin_);
    bufferLayout->addLayout(fpsLayout);
    
    // Pre-Trigger
    QHBoxLayout* preLayout = new QHBoxLayout();
    preLayout->addWidget(new QLabel("Pre-Trigger Buffer:"));
    preTriggerSpin_ = new QSpinBox();
    preTriggerSpin_->setRange(1, 60);
    preTriggerSpin_->setSuffix(" sec");
    preLayout->addWidget(preTriggerSpin_);
    bufferLayout->addLayout(preLayout);
    
    // Post-Trigger
    QHBoxLayout* postLayout = new QHBoxLayout();
    postLayout->addWidget(new QLabel("Post-Trigger Record:"));
    postTriggerSpin_ = new QSpinBox();
    postTriggerSpin_->setRange(1, 60);
    postTriggerSpin_->setSuffix(" sec");
    postLayout->addWidget(postTriggerSpin_);
    bufferLayout->addLayout(postLayout);
    
    // Defect Config
    QHBoxLayout* defectLayout = new QHBoxLayout();
    defectLayout->addWidget(new QLabel("Enable Defect Detection (Auto-Trigger):"));
    defectCheck_ = new ToggleSwitch(this);
    defectLayout->addWidget(defectCheck_);
    defectLayout->addStretch();
    bufferLayout->addLayout(defectLayout);
    
    toolBox->addItem(bufferGroup, "Global Recording & Events");
    
    // --- UI Preferences Group ---
    QWidget* uiGroup = new QWidget(this);
    QVBoxLayout* uiLayout = new QVBoxLayout(uiGroup);
    
    QHBoxLayout* themeLayout = new QHBoxLayout();
    themeLayout->addWidget(new QLabel("Color Theme:"));
    themeCombo_ = new QComboBox();
    themeCombo_->addItem("Industrial Dark - Cyan", 0);
    themeCombo_->addItem("Classic Dark - Blue", 1);
    themeCombo_->addItem("High Contrast - Orange", 2);
    themeCombo_->addItem("Warning State - Yellow", 3);
    themeCombo_->addItem("Precision - Green", 4);
    themeCombo_->addItem("Visionary - Purple", 5);
    themeCombo_->addItem("Alert - Deep Red", 6);
    themeCombo_->addItem("Contrast Mono - Black & White", 7);
    themeLayout->addWidget(themeCombo_);
    uiLayout->addLayout(themeLayout);
    uiLayout->addStretch();
    
    toolBox->addItem(uiGroup, "UI Preferences");
    
    mainLayout->addWidget(toolBox);
    
    // --- Buttons ---
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    
    QPushButton* closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    
    saveBtn_ = new QPushButton("Save and Apply Settings");
    saveBtn_->setDefault(true);
    connect(saveBtn_, &QPushButton::clicked, this, &ConfigDialog::saveAndApply);
    
    btnLayout->addWidget(closeBtn);
    btnLayout->addWidget(saveBtn_);
    
    mainLayout->addLayout(btnLayout);
}

void ConfigDialog::loadSettings() {
    // 1. Load Camera Setup Configs
    std::vector<CameraInfo> cameras = CameraConfig::getCameras();
    for (const auto& cam : cameras) {
        createCameraWidgetBlock(cam);
    }
    
    // 2. Load Global Settings
    globalFpsSpin_->setValue(CameraConfig::getFps());
    preTriggerSpin_->setValue(CameraConfig::getPreTriggerSeconds());
    postTriggerSpin_->setValue(CameraConfig::getPostTriggerSeconds());
    defectCheck_->setChecked(CameraConfig::isDefectDetectionEnabled());
    
    int themeIdx = themeCombo_->findData(CameraConfig::getThemePreset());
    if (themeIdx != -1) themeCombo_->setCurrentIndex(themeIdx);
}

void ConfigDialog::createCameraWidgetBlock(const CameraInfo& cam) {
    QGroupBox* gb = new QGroupBox(QString("Camera ID: %1").arg(cam.id));
    QGridLayout* grid = new QGridLayout(gb);
    
    CameraConfigWidgets w;
    w.container = gb;
    w.id = cam.id;
    
    // Configuration Edit Toggle — controls ALL writable fields (Move to top)
    w.editParamsCheck = new QCheckBox("Enable Configuration Editing");
    w.editParamsCheck->setChecked(false);
    grid->addWidget(w.editParamsCheck, 0, 0, 1, 4);
    
    // Separator
    QFrame* topParamLine = new QFrame();
    topParamLine->setFrameShape(QFrame::HLine);
    topParamLine->setFrameShadow(QFrame::Sunken);
    grid->addWidget(topParamLine, 1, 0, 1, 4);

    // Helper lambda for adding rows dynamically in 2-column layout
    int row = 2, col = 0;
    auto addField = [&](const QString& labelText, QWidget* widget) {
        if (!labelText.isEmpty()) {
            grid->addWidget(new QLabel(labelText), row, col * 2);
            grid->addWidget(widget, row, col * 2 + 1);
        } else {
            // Span 2 columns if no label
            grid->addWidget(widget, row, col * 2, 1, 2);
        }
        col++;
        if (col > 1) { col = 0; row++; }
    };
    
    // Source
    w.sourceCombo = new QComboBox();
    w.sourceCombo->addItem("Emulated", 0);
    w.sourceCombo->addItem("Real", 1);
    w.sourceCombo->addItem("Disabled", 2);
    w.sourceCombo->setCurrentIndex(w.sourceCombo->findData(cam.source));
    addField("Camera Source:", w.sourceCombo);
    
    // Name
    w.nameEdit = new QLineEdit(cam.name);
    addField("Name:", w.nameEdit);
    
    // Location
    w.locationEdit = new QLineEdit(cam.location);
    addField("Location:", w.locationEdit);
    
    // Side
    w.sideCombo = new QComboBox();
    w.sideCombo->addItem("DRIVE SIDE");
    w.sideCombo->addItem("OPERATOR SIDE");
    w.sideCombo->setCurrentText(cam.side);
    addField("Machine Side:", w.sideCombo);
    
    // Position
    w.positionSpin = new QSpinBox();
    w.positionSpin->setRange(0, 500000);
    w.positionSpin->setSuffix(" mm");
    w.positionSpin->setValue(cam.machinePosition);
    w.positionSpin->setFocusPolicy(Qt::StrongFocus);
    addField("Machine Position:", w.positionSpin);
    
    // IP Address (Read Only)
    w.ipLabel = new QLabel(cam.ipAddress);
    addField("IP Address:", w.ipLabel);
    
    // MAC Address
    w.macCombo = new QComboBox();
    w.macCombo->addItem("None / Auto");
    for (const auto& dev : currentGigEDevices_) {
        w.macCombo->addItem(QString::fromStdString(dev.macAddress));
    }
    w.macCombo->setCurrentText(cam.macAddress);
    w.macCombo->setEditable(true);
    addField("MAC Address:", w.macCombo);
    
    // Subnet Mask
    w.subnetEdit = new QLineEdit(cam.subnetMask);
    addField("Subnet Mask:", w.subnetEdit);
    
    // Default Gateway
    w.gatewayEdit = new QLineEdit(cam.defaultGateway);
    addField("Default Gateway:", w.gatewayEdit);
    
    // Write IP Button
    w.writeIpBtn = new QPushButton("Write IP to Physical Camera");
    connect(w.writeIpBtn, &QPushButton::clicked, this, [w]() {
        QString mac = w.macCombo->currentText();
        QString ip = w.ipLabel->text(); // IP is fixed based on ID
        QString mask = w.subnetEdit->text();
        QString gw = w.gatewayEdit->text();
        
        if (mac.isEmpty() || mac == "None / Auto") {
            QMessageBox::warning(w.container, "Write IP", "Please select or enter a valid MAC Address first.");
            return;
        }
        
        if (CameraManager::applyIpConfiguration(mac.toStdString(), ip.toStdString(), mask.toStdString(), gw.toStdString())) {
            QMessageBox::information(w.container, "Write IP", "Successfully wrote IP configuration to camera " + mac);
        } else {
            QMessageBox::critical(w.container, "Write IP", "Failed to write IP configuration to camera " + mac + ".\nPlease check connection and MAC address.");
        }
    });
    w.writeIpBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    addField("", w.writeIpBtn);
    
    // FPS
    w.fpsSpin = new QSpinBox();
    w.fpsSpin->setRange(1, 200);
    w.fpsSpin->setValue(cam.fps);
    w.fpsSpin->setFocusPolicy(Qt::StrongFocus);
    addField("FPS:", w.fpsSpin);
    
    w.gainSpin = new QSpinBox();
    w.gainSpin->setRange(0, 10000);
    w.gainSpin->setValue(cam.gain);
    w.gainSpin->setFocusPolicy(Qt::StrongFocus);
    addField("Gain (initial):", w.gainSpin);
    
    w.exposureSpin = new QSpinBox();
    w.exposureSpin->setRange(10, 100000);
    w.exposureSpin->setValue(cam.exposureTime);
    w.exposureSpin->setFocusPolicy(Qt::StrongFocus);
    addField("Exposure Time (initial):", w.exposureSpin);
    
    w.gammaSpin = new QSpinBox();
    w.gammaSpin->setRange(0, 100);
    w.gammaSpin->setValue(cam.gamma);
    w.gammaSpin->setFocusPolicy(Qt::StrongFocus);
    addField("Gamma (initial):", w.gammaSpin);
    
    w.contrastSpin = new QSpinBox();
    w.contrastSpin->setRange(0, 100);
    w.contrastSpin->setValue(cam.contrast);
    w.contrastSpin->setFocusPolicy(Qt::StrongFocus);
    addField("Contrast (initial):", w.contrastSpin);
    
    // If we have an odd number of items, move to next row
    if (col != 0) { row++; col = 0; }
    
    // Separator line before Remove
    QFrame* paramLine = new QFrame();
    paramLine->setFrameShape(QFrame::HLine);
    paramLine->setFrameShadow(QFrame::Sunken);
    grid->addWidget(paramLine, row++, 0, 1, 4);

    // Remove Button
    QPushButton* removeBtn = new QPushButton("Delete Camera Config");
    removeBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(removeBtn, &QPushButton::clicked, this, &ConfigDialog::onRemoveCameraConfigClicked);
    removeBtn->setProperty("containerPtr", QVariant::fromValue(static_cast<void*>(gb)));
    grid->addWidget(removeBtn, row++, 0, 1, 2);
    
    // Toggle controls ALL writable fields
    auto setAllEditable = [w](bool en) {
        w.sourceCombo->setEnabled(en);
        w.nameEdit->setEnabled(en);
        w.locationEdit->setEnabled(en);
        w.sideCombo->setEnabled(en);
        w.positionSpin->setEnabled(en);
        w.macCombo->setEnabled(en);
        w.subnetEdit->setEnabled(en);
        w.gatewayEdit->setEnabled(en);
        w.writeIpBtn->setEnabled(en);
        w.fpsSpin->setEnabled(en);
        w.gainSpin->setEnabled(en);
        w.exposureSpin->setEnabled(en);
        w.gammaSpin->setEnabled(en);
        w.contrastSpin->setEnabled(en);
    };
    setAllEditable(false); // start locked
    
    connect(w.editParamsCheck, &QCheckBox::toggled, this, [setAllEditable](bool checked) {
        setAllEditable(checked);
    });
    
    // Wrap to prevent full-width stretching
    QWidget* rowWidget = new QWidget();
    QHBoxLayout* rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->addWidget(gb);
    rowLayout->addStretch(1);
    
    removeBtn->setProperty("wrapperPtr", QVariant::fromValue(static_cast<void*>(rowWidget)));
    
    cameraListLayout_->addWidget(rowWidget);
    activeCameraConfigs_.push_back(w);
}

void ConfigDialog::onAddCameraConfigClicked() {
    CameraInfo cam;
    
    // Compute next ID and IP
    int maxId = 0;
    for (const auto& w : activeCameraConfigs_) {
        if (w.id > maxId) maxId = w.id;
    }
    cam.id = maxId + 1;
    cam.source = 0; // default to emulated for testing safety
    cam.name = QString("DRYER %1").arg(cam.id);
    cam.location = QString("CYLINDER %1").arg(10 + cam.id);
    cam.side = "DRIVE SIDE";
    cam.machinePosition = 16000 + (cam.id * 500);
    cam.ipAddress = QString("172.17.2.%1").arg(cam.id);
    cam.macAddress = "";
    cam.subnetMask = "255.255.255.0";
    cam.defaultGateway = "0.0.0.0";
    cam.gain = 428;
    cam.exposureTime = 541;
    cam.gamma = 23;
    cam.contrast = 4;
    cam.fps = 50;
    cam.temperature = 0.0;
    
    createCameraWidgetBlock(cam);
}

void ConfigDialog::onRemoveCameraConfigClicked() {
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    
    QWidget* container = static_cast<QWidget*>(btn->property("containerPtr").value<void*>());
    QWidget* wrapper = static_cast<QWidget*>(btn->property("wrapperPtr").value<void*>());
    if (!container || !wrapper) return;
    
    // Remove from vector
    activeCameraConfigs_.erase(std::remove_if(activeCameraConfigs_.begin(), activeCameraConfigs_.end(), 
        [container](const CameraConfigWidgets& entry) { return entry.container == container; }), activeCameraConfigs_.end());
        
    // Remove from UI
    cameraListLayout_->removeWidget(wrapper);
    delete wrapper;
}

void ConfigDialog::saveAndApply() {
    // 1. Gather all camera configs
    std::vector<CameraInfo> newCameras;
    for (const auto& w : activeCameraConfigs_) {
        CameraInfo cam;
        cam.id = w.id;
        cam.source = w.sourceCombo->currentData().toInt();
        cam.name = w.nameEdit->text();
        cam.location = w.locationEdit->text();
        cam.side = w.sideCombo->currentText();
        cam.machinePosition = w.positionSpin->value();
        cam.ipAddress = w.ipLabel->text();
        cam.macAddress = w.macCombo->currentText();
        if (cam.macAddress == "None / Auto") cam.macAddress = "";
        cam.subnetMask = w.subnetEdit->text();
        cam.defaultGateway = w.gatewayEdit->text();
        
        // Save initial parameters directly from newly added fields!
        cam.gain = w.gainSpin->value();
        cam.exposureTime = w.exposureSpin->value();
        cam.gamma = w.gammaSpin->value();
        cam.contrast = w.contrastSpin->value();
        
        cam.fps = w.fpsSpin->value();
        cam.temperature = 0.0;
        
        newCameras.push_back(cam);
    }
    
    CameraConfig::saveCameras(newCameras);
    
    // 2. Save Global Configs
    CameraConfig::setFps(globalFpsSpin_->value());
    CameraConfig::setPreTriggerSeconds(preTriggerSpin_->value());
    CameraConfig::setPostTriggerSeconds(postTriggerSpin_->value());
    CameraConfig::setDefectDetectionEnabled(defectCheck_->isChecked());
    CameraConfig::setThemePreset(themeCombo_->currentData().toInt());
    
    // 3. Notify main window to apply dynamics (like FPS) WITHOUT RESTART requested
    emit configUpdated();
    
    // Close the dialog immediately
    close();
}

void ConfigDialog::setAdminMode(bool isAdmin) {
    // Global settings: controlled by admin mode
    addCameraBtn_->setEnabled(isAdmin);
    globalFpsSpin_->setEnabled(isAdmin);
    preTriggerSpin_->setEnabled(isAdmin);
    postTriggerSpin_->setEnabled(isAdmin);
    defectCheck_->setEnabled(isAdmin);
    saveBtn_->setEnabled(isAdmin);
    
    // Per-camera: only the toggle checkbox is admin-gated.
    // The checkbox itself controls whether fields are accessible.
    for (const auto& w : activeCameraConfigs_) {
        w.editParamsCheck->setEnabled(isAdmin);
        if (!isAdmin) {
            // Locking admin mode collapses all fields
            w.editParamsCheck->setChecked(false);
            // Fields are disabled by the toggled() signal triggered above
        }
    }
}

void ConfigDialog::onRefreshLogsClicked() {
    currentGigEDevices_ = CameraManager::enumerateGigEDevices();
    QString refreshTs = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    connectionLogsBrowser_->append(QString("--- Refresh at %1 ---").arg(refreshTs));
    
    // Static map to remember when a camera was first connected/detected
    static QMap<QString, QString> cameraConnectionTimes;
    
    for (const auto& dev : currentGigEDevices_) {
        QString mac = QString::fromStdString(dev.macAddress);
        
        // If this is the first time we see this MAC, record the timestamp
        if (!cameraConnectionTimes.contains(mac)) {
            cameraConnectionTimes.insert(mac, QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
        }
        
        QString devTs = cameraConnectionTimes.value(mac);
        
        connectionLogsBrowser_->append(
            QString("[%1] %2 MAC: %3 | IP: %4 | Subnet: %5 | Gateway: %6")
            .arg(devTs)
            .arg(QString::fromStdString(dev.friendlyName))
            .arg(QString::fromStdString(dev.macAddress))
            .arg(QString::fromStdString(dev.ipAddress))
            .arg(QString::fromStdString(dev.subnetMask))
            .arg(QString::fromStdString(dev.defaultGateway))
        );
    }
    if(currentGigEDevices_.empty()) {
        cameraConnectionTimes.clear(); // Clear so next time they are "new" if they reconnect
        QString emptyTs = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        connectionLogsBrowser_->append(
            QString("[%1] No online Real cameras detected.").arg(emptyTs)
        );
    }
    
    // Update MAC Address dropdowns in active camera configs
    for (auto& w : activeCameraConfigs_) {
        QString currentText = w.macCombo->currentText();
        w.macCombo->blockSignals(true);
        w.macCombo->clear();
        w.macCombo->addItem("None / Auto");
        for (const auto& dev : currentGigEDevices_) {
            w.macCombo->addItem(QString::fromStdString(dev.macAddress));
        }
        w.macCombo->setCurrentText(currentText);
        w.macCombo->blockSignals(false);
    }
}

void ConfigDialog::onClearLogsClicked() {
    connectionLogsBrowser_->clear();
}

void ConfigDialog::onOpenIpConfiguratorClicked() {
    QProcess::startDetached("/opt/pylon/bin/IpConfigurator", QStringList());
}
