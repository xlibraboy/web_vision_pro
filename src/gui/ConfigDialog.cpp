#include "ConfigDialog.h"
#include "../config/CameraConfig.h"
#include "../core/CameraManager.h"
#include "widgets/ToggleSwitch.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolBox>
#include <QLineEdit> // NEW
#include <iostream> // NEW
#include <QMessageBox>
#include <QProcess>
#include <QApplication>
#include <QScrollArea>
#include <QGroupBox>
#include <QRegExpValidator>
#include <QRegExp>

ConfigDialog::ConfigDialog(QWidget *parent) : QWidget(parent) {
    setWindowTitle("System Configuration");
    
    // Set as an independent window that deletes itself when closed
    setWindowFlags(Qt::Window);
    // Removed: setAttribute(Qt::WA_DeleteOnClose);
    resize(400, 350);
    
    setupUI();

    // Connections
    connect(fpsSpin_, QOverload<int>::of(&QSpinBox::valueChanged), [&](int value){ /* auto update could go here */ });
    
    // Network Config Connections
    connect(refreshNetBtn_, &QPushButton::clicked, this, &ConfigDialog::refreshIpCameraList); // Changed refreshNetBtn to refreshNetBtn_
    connect(applyIpBtn_, &QPushButton::clicked, this, &ConfigDialog::onApplyIpClicked);

    // Initial load
    loadSettings();
    refreshIpCameraList(); // NEW
}

void ConfigDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    QToolBox* toolBox = new QToolBox(this);
    
    // --- Camera Settings Group ---
    QWidget* camGroup = new QWidget(this);
    QVBoxLayout* camLayout = new QVBoxLayout(camGroup);
    
    // Source
    QHBoxLayout* sourceLayout = new QHBoxLayout();
    sourceLayout->addWidget(new QLabel("Camera Source:"));
    sourceCombo_ = new QComboBox();
    sourceCombo_->addItem("Emulation (Simulated)", static_cast<int>(CameraConfig::CameraSource::Emulation));
    sourceCombo_->addItem("Real Camera (Basler GigE)", static_cast<int>(CameraConfig::CameraSource::RealCamera));
    sourceLayout->addWidget(sourceCombo_);
    camLayout->addLayout(sourceLayout);
    
    camLayout->addWidget(new QLabel("<i>Note: Changing source requires application restart.</i>"));
    
    // FPS
    QHBoxLayout* fpsLayout = new QHBoxLayout();
    fpsLayout->addWidget(new QLabel("Acquisition FPS:"));
    fpsSpin_ = new QSpinBox();
    fpsSpin_->setRange(1, 100);
    fpsSpin_->setSuffix(" fps");
    fpsLayout->addWidget(fpsSpin_);
    camLayout->addLayout(fpsLayout);
    
    toolBox->addItem(camGroup, "Camera Settings");
    
    // --- Event Buffer Settings Group ---
    QWidget* bufferGroup = new QWidget(this);
    QVBoxLayout* bufferLayout = new QVBoxLayout(bufferGroup);
    
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
    
    toolBox->addItem(bufferGroup, "Event Recording");
    
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
    themeCombo_->addItem("Contrast Mono - Black & White", 7); // NEW
    themeLayout->addWidget(themeCombo_);
    uiLayout->addLayout(themeLayout);
    uiLayout->addStretch();
    
    toolBox->addItem(uiGroup, "UI Preferences");
    
    // --- Network Config Group (GigE) --- // NEW SECTION
    QWidget* netGroup = new QWidget(this);
    QVBoxLayout* netLayout = new QVBoxLayout(netGroup);
    
    // Top-bar for adding cameras
    QHBoxLayout* camSelectLayout = new QHBoxLayout();
    camSelectLayout->addWidget(new QLabel("Select Camera:"));
    ipCameraCombo_ = new QComboBox();
    camSelectLayout->addWidget(ipCameraCombo_);
    
    QPushButton* addNetBtn = new QPushButton("Add Config +");
    connect(addNetBtn, &QPushButton::clicked, this, &ConfigDialog::onAddCameraConfigClicked);
    camSelectLayout->addWidget(addNetBtn);
    
    refreshNetBtn_ = new QPushButton("Refresh List"); 
    camSelectLayout->addWidget(refreshNetBtn_);
    netLayout->addLayout(camSelectLayout);
    
    // Scroll Area for dynamic configs
    ipScrollArea_ = new QScrollArea();
    ipScrollArea_->setWidgetResizable(true);
    ipScrollArea_->setFrameShape(QFrame::NoFrame);
    
    ipScrollWidget_ = new QWidget();
    ipListLayout_ = new QVBoxLayout(ipScrollWidget_);
    ipListLayout_->setAlignment(Qt::AlignTop);
    ipScrollArea_->setWidget(ipScrollWidget_);
    
    netLayout->addWidget(ipScrollArea_);
    
    // Apply all button
    applyIpBtn_ = new QPushButton("Broadcast IP Configuration to All");
    netLayout->addWidget(applyIpBtn_);
    
    toolBox->addItem(netGroup, "Network Configuration (GigE)"); // END NEW SECTION
    
    mainLayout->addWidget(toolBox);
    
    // --- Buttons ---
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    
    QPushButton* closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    
    saveBtn_ = new QPushButton("Save and Apply Settings"); // Changed text
    saveBtn_->setDefault(true);
    connect(saveBtn_, &QPushButton::clicked, this, &ConfigDialog::saveAndApply);
    
    btnLayout->addWidget(closeBtn);
    btnLayout->addWidget(saveBtn_);
    
    mainLayout->addStretch();
    mainLayout->addLayout(btnLayout);
}

void ConfigDialog::loadSettings() {
    // Load from Config
    CameraConfig::CameraSource src = CameraConfig::getCameraSource();
    int idx = sourceCombo_->findData(static_cast<int>(src));
    if (idx != -1) sourceCombo_->setCurrentIndex(idx);
    
    fpsSpin_->setValue(CameraConfig::getFps());
    preTriggerSpin_->setValue(CameraConfig::getPreTriggerSeconds());
    postTriggerSpin_->setValue(CameraConfig::getPostTriggerSeconds());
    defectCheck_->setChecked(CameraConfig::isDefectDetectionEnabled());
    
    int themeIdx = themeCombo_->findData(CameraConfig::getThemePreset());
    if (themeIdx != -1) themeCombo_->setCurrentIndex(themeIdx);
}

void ConfigDialog::saveAndApply() {
    // 1. Check if source changed
    CameraConfig::CameraSource oldSrc = CameraConfig::getCameraSource();
    CameraConfig::CameraSource newSrc = static_cast<CameraConfig::CameraSource>(sourceCombo_->currentData().toInt());
    
    bool sourceChanged = (oldSrc != newSrc);
    
    // 2. Save all
    CameraConfig::setCameraSource(newSrc);
    CameraConfig::setFps(fpsSpin_->value());
    CameraConfig::setPreTriggerSeconds(preTriggerSpin_->value());
    CameraConfig::setPostTriggerSeconds(postTriggerSpin_->value());
    CameraConfig::setDefectDetectionEnabled(defectCheck_->isChecked());
    CameraConfig::setThemePreset(themeCombo_->currentData().toInt());
    
    // 3. Notify main window (which will apply the theme globally)
    emit configUpdated();
    
    if (sourceChanged) {
        // Prompt for restart
        QMessageBox::information(this, "Restart Required", 
            "You have changed the Camera Source.\n\n"
            "The application will now restart for this change to take effect.");
    }
    
    // Restart application to apply changes
    QProcess::startDetached(qApp->arguments()[0], qApp->arguments());
    qApp->quit();
}

// NEW METHODS
void ConfigDialog::refreshIpCameraList() {
    ipCameraCombo_->clear();
    currentGigEDevices_ = CameraManager::enumerateGigEDevices();
    for (const auto& dev : currentGigEDevices_) {
        ipCameraCombo_->addItem(QString::fromStdString(dev.friendlyName + " (" + dev.macAddress + ")"), QString::fromStdString(dev.macAddress));
    }
}

void ConfigDialog::onAddCameraConfigClicked() {
    int index = ipCameraCombo_->currentIndex();
    if (index < 0 || index >= (int)currentGigEDevices_.size()) return;
    
    const auto& dev = currentGigEDevices_[index];
    
    // Check if already added
    for (const auto& entry : activeIpConfigs_) {
        if (entry.mac == dev.macAddress) {
            QMessageBox::information(this, "Info", "Configuration for this camera is already listed.");
            return;
        }
    }

    QGroupBox* groupBox = new QGroupBox(QString::fromStdString(dev.friendlyName));
    QVBoxLayout* groupLayout = new QVBoxLayout(groupBox);
    
    // IP Regex Validator (Matches exact IPv4 format)
    QRegExp ipRegex("^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$");
    QRegExpValidator* ipValidator = new QRegExpValidator(ipRegex, this);

    auto createValidatedField = [&](const QString& labelText, const QString& placeholder, const QString& currentVal, QLineEdit*& le) {
        QHBoxLayout* hl = new QHBoxLayout();
        hl->addWidget(new QLabel(labelText));
        le = new QLineEdit(currentVal);
        le->setPlaceholderText(placeholder);
        le->setValidator(ipValidator); // Enforce IP formatting
        hl->addWidget(le);
        groupLayout->addLayout(hl);
    };
    
    IpConfigEntry entry;
    entry.container = groupBox;
    entry.mac = dev.macAddress;
    
    createValidatedField("IP Address:", "e.g. 192.168.1.10", QString::fromStdString(dev.ipAddress), entry.ip);
    createValidatedField("Subnet Mask:", "e.g. 255.255.255.0", QString::fromStdString(dev.subnetMask), entry.mask);
    createValidatedField("Gateway:", "e.g. 0.0.0.0", QString::fromStdString(dev.defaultGateway), entry.gw);
    
    QPushButton* removeBtn = new QPushButton("Remove Configuration");
    connect(removeBtn, &QPushButton::clicked, this, &ConfigDialog::onRemoveCameraConfigClicked);
    
    // Store pointer to container in button so we know which to delete
    removeBtn->setProperty("containerPtr", QVariant::fromValue(static_cast<void*>(groupBox)));
    groupLayout->addWidget(removeBtn);
    
    ipListLayout_->addWidget(groupBox);
    activeIpConfigs_.push_back(entry);
}

void ConfigDialog::onRemoveCameraConfigClicked() {
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    
    QWidget* container = static_cast<QWidget*>(btn->property("containerPtr").value<void*>());
    if (!container) return;
    
    // Remove from vector
    activeIpConfigs_.erase(std::remove_if(activeIpConfigs_.begin(), activeIpConfigs_.end(), 
        [container](const IpConfigEntry& entry) { return entry.container == container; }), activeIpConfigs_.end());
        
    // Remove from UI
    ipListLayout_->removeWidget(container);
    delete container; // Cleans up layout and children
}

void ConfigDialog::onApplyIpClicked() {
    if (activeIpConfigs_.empty()) {
        QMessageBox::information(this, "Info", "No configurations to apply.");
        return;
    }

    int successCount = 0;
    int failCount = 0;

    for (const auto& entry : activeIpConfigs_) {
        // Double check regex format matching just in case (e.g. partial inputs)
        int pos = 0;
        QString ipText = entry.ip->text();
        QString maskText = entry.mask->text();
        QString gwText = entry.gw->text();
        
        if (entry.ip->validator()->validate(ipText, pos) != QValidator::Acceptable ||
            entry.mask->validator()->validate(maskText, pos) != QValidator::Acceptable ||
            entry.gw->validator()->validate(gwText, pos) != QValidator::Acceptable) {
            QMessageBox::warning(this, "Validation Error", QString("Invalid IP format detected for MAC %1. Skipping.").arg(QString::fromStdString(entry.mac)));
            failCount++;
            continue;
        }

        bool success = CameraManager::applyIpConfiguration(
            entry.mac, 
            ipText.toStdString(), 
            maskText.toStdString(), 
            gwText.toStdString()
        );
        
        if (success) successCount++;
        else failCount++;
    }
    
    QMessageBox::information(this, "Result", 
        QString("Broadcast completed.\nSuccessful: %1\nFailed: %2\n\nSuccessful devices may restart their network interfaces.").arg(successCount).arg(failCount));
        
    refreshIpCameraList();
}

void ConfigDialog::setAdminMode(bool isAdmin) {
    sourceCombo_->setEnabled(isAdmin);
    fpsSpin_->setEnabled(isAdmin);
    preTriggerSpin_->setEnabled(isAdmin);
    postTriggerSpin_->setEnabled(isAdmin);
    defectCheck_->setEnabled(isAdmin);
    saveBtn_->setEnabled(isAdmin);
    
    // Theme combo remains enabled for all users.
}
