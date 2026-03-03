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
    topCamLayout->addWidget(addCameraBtn_);
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
    connectionLogsBrowser_ = new QTextEdit();
    connectionLogsBrowser_->setReadOnly(true);
    connectionLogsBrowser_->setMinimumHeight(120);
    // Build initial logs string right away
    QString logsText;
    for (const auto& dev : currentGigEDevices_) {
        logsText += QString("[%1] MAC: %2 | IP: %3 | Subnet: %4 | Gateway: %5\n")
            .arg(QString::fromStdString(dev.friendlyName))
            .arg(QString::fromStdString(dev.macAddress))
            .arg(QString::fromStdString(dev.ipAddress))
            .arg(QString::fromStdString(dev.subnetMask))
            .arg(QString::fromStdString(dev.defaultGateway));
    }
    if(logsText.isEmpty()) logsText = "No online Real cameras detected.";
    connectionLogsBrowser_->setText(logsText);
    logsLayout->addWidget(connectionLogsBrowser_);
    
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
    QFormLayout* form = new QFormLayout(gb);
    
    CameraConfigWidgets w;
    w.container = gb;
    w.id = cam.id;
    
    // Source
    w.sourceCombo = new QComboBox();
    w.sourceCombo->addItem("Emulated", 0);
    w.sourceCombo->addItem("Real", 1);
    w.sourceCombo->addItem("Disabled", 2);
    w.sourceCombo->setCurrentIndex(w.sourceCombo->findData(cam.source));
    form->addRow("Camera Source:", w.sourceCombo);
    
    // Name
    w.nameEdit = new QLineEdit(cam.name);
    form->addRow("Name:", w.nameEdit);
    
    // Location
    w.locationEdit = new QLineEdit(cam.location);
    form->addRow("Location:", w.locationEdit);
    
    // Side
    w.sideCombo = new QComboBox();
    w.sideCombo->addItem("DRIVE SIDE");
    w.sideCombo->addItem("OPERATOR SIDE");
    w.sideCombo->setCurrentText(cam.side);
    form->addRow("Machine Side:", w.sideCombo);
    
    // Position
    w.positionSpin = new QSpinBox();
    w.positionSpin->setRange(0, 500000);
    w.positionSpin->setSuffix(" mm");
    w.positionSpin->setValue(cam.machinePosition);
    w.positionSpin->setFocusPolicy(Qt::StrongFocus);  // Disable wheel scroll
    form->addRow("Machine Position:", w.positionSpin);
    
    // IP Address (Read Only)
    w.ipLabel = new QLabel(cam.ipAddress);
    form->addRow("IP Address:", w.ipLabel);
    
    // MAC Address
    w.macCombo = new QComboBox();
    w.macCombo->addItem("None / Auto");
    for (const auto& dev : currentGigEDevices_) {
        w.macCombo->addItem(QString::fromStdString(dev.macAddress));
    }
    w.macCombo->setCurrentText(cam.macAddress);
    w.macCombo->setEditable(true); // Allow typing a custom MAC if camera is offline
    form->addRow("MAC Address:", w.macCombo);
    
    // Subnet Mask
    w.subnetEdit = new QLineEdit(cam.subnetMask);
    form->addRow("Subnet Mask:", w.subnetEdit);
    
    // Default Gateway
    w.gatewayEdit = new QLineEdit(cam.defaultGateway);
    form->addRow("Default Gateway:", w.gatewayEdit);
    
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
    form->addRow("", w.writeIpBtn);
    
    // FPS
    w.fpsSpin = new QSpinBox();
    w.fpsSpin->setRange(1, 200);
    w.fpsSpin->setValue(cam.fps);
    w.fpsSpin->setFocusPolicy(Qt::StrongFocus);  // Disable wheel scroll
    form->addRow("FPS:", w.fpsSpin);
    
    // Separator line before params
    QFrame* paramLine = new QFrame();
    paramLine->setFrameShape(QFrame::HLine);
    paramLine->setFrameShadow(QFrame::Sunken);
    form->addRow(paramLine);
    
    // Configuration Edit Toggle — controls ALL writable fields
    w.editParamsCheck = new QCheckBox("Enable Configuration Editing");
    w.editParamsCheck->setChecked(false);
    form->addRow("", w.editParamsCheck);
    
    w.gainSpin = new QSpinBox();
    w.gainSpin->setRange(0, 10000);
    w.gainSpin->setValue(cam.gain);
    w.gainSpin->setFocusPolicy(Qt::StrongFocus);
    form->addRow("Gain (initial):", w.gainSpin);
    
    w.exposureSpin = new QSpinBox();
    w.exposureSpin->setRange(10, 100000);
    w.exposureSpin->setValue(cam.exposureTime);
    w.exposureSpin->setFocusPolicy(Qt::StrongFocus);
    form->addRow("Exposure Time (initial):", w.exposureSpin);
    
    w.gammaSpin = new QSpinBox();
    w.gammaSpin->setRange(0, 100);
    w.gammaSpin->setValue(cam.gamma);
    w.gammaSpin->setFocusPolicy(Qt::StrongFocus);
    form->addRow("Gamma (initial):", w.gammaSpin);
    
    w.contrastSpin = new QSpinBox();
    w.contrastSpin->setRange(0, 100);
    w.contrastSpin->setValue(cam.contrast);
    w.contrastSpin->setFocusPolicy(Qt::StrongFocus);
    form->addRow("Contrast (initial):", w.contrastSpin);
    
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
    
    // Remove Button
    QPushButton* removeBtn = new QPushButton("Remove Camera Configuration");
    connect(removeBtn, &QPushButton::clicked, this, &ConfigDialog::onRemoveCameraConfigClicked);
    removeBtn->setProperty("containerPtr", QVariant::fromValue(static_cast<void*>(gb)));
    form->addRow("", removeBtn);
    
    cameraListLayout_->addWidget(gb);
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
    if (!container) return;
    
    // Remove from vector
    activeCameraConfigs_.erase(std::remove_if(activeCameraConfigs_.begin(), activeCameraConfigs_.end(), 
        [container](const CameraConfigWidgets& entry) { return entry.container == container; }), activeCameraConfigs_.end());
        
    // Remove from UI
    cameraListLayout_->removeWidget(container);
    delete container;
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
