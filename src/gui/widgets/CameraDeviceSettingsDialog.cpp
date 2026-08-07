#include "CameraDeviceSettingsDialog.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "IconManager.h"
#include "../../config/CameraConfig.h"
#include "../../core/CameraManager.h"

namespace {
QString formatReadOnlyValue(const QString& value) {
    return value.trimmed().isEmpty() ? QString("Not available") : value;
}

QStringList defaultPixelFormats() {
    return {"Mono8", "Mono12", "Mono16"};
}

QString normalizeMacForCompare(const QString& mac) {
    QString normalized;
    normalized.reserve(mac.size());
    for (const QChar ch : mac) {
        if (ch.isLetterOrNumber()) {
            normalized.append(ch.toUpper());
        }
    }
    return normalized;
}

bool isCameraReachable(CameraManager* cameraManager, int cameraIndex, const CameraInfo& info) {
    if (!cameraManager) {
        return false;
    }

    if (cameraManager->isCameraConnected(cameraIndex) || cameraManager->isCameraOpen(cameraIndex)) {
        return true;
    }

    const CameraManager::CameraParams params = cameraManager->getCameraParams(cameraIndex);
    // CameraParams defaults exposureUs to 5000.0 even with NO camera attached,
    // so exposure alone is not evidence of life. Width/height/fps are 0 when
    // the runtime camera is absent — those are the honest signals.
    if (params.width > 0 || params.height > 0 || params.fps > 0.0) {
        return true;
    }

    const cv::Size resolution = cameraManager->getCameraResolution(cameraIndex);
    if (resolution.width > 0 || resolution.height > 0) {
        return true;
    }

    const std::string model = cameraManager->getModelName(cameraIndex);
    if (!model.empty() && model != "Not Connected" && model != "Unknown Model") {
        return true;
    }

    const std::string ip = cameraManager->getIpAddress(cameraIndex);
    if (!ip.empty() && ip != "Offline") {
        return true;
    }

    const QString wantedMac = normalizeMacForCompare(info.macAddress);
    const QString wantedIp = info.ipAddress.trimmed();
    if (wantedMac.isEmpty() && wantedIp.isEmpty()) {
        return false;
    }
    const auto devices = CameraManager::enumerateGigEDevices();
    for (const auto& device : devices) {
        const QString deviceMac = normalizeMacForCompare(QString::fromStdString(device.macAddress));
        const QString deviceIp = QString::fromStdString(device.ipAddress).trimmed();
        if ((!wantedMac.isEmpty() && deviceMac == wantedMac)
                || (!wantedIp.isEmpty() && deviceIp == wantedIp)) {
            return true;
        }
    }

    return false;
}

bool hasConfiguredRealDevice(const CameraInfo& info) {
    return info.source == 1 && (!info.macAddress.trimmed().isEmpty() || !info.ipAddress.trimmed().isEmpty());
}

void persistSharedCameraSettings(int cameraIndex, const CameraInfo& info) {
    std::vector<CameraInfo> cameras = CameraConfig::getCameras();
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(cameras.size())) {
        return;
    }

    cameras[cameraIndex].pixelFormat = info.pixelFormat;
    cameras[cameraIndex].width = info.width;
    cameras[cameraIndex].height = info.height;
    cameras[cameraIndex].offsetX = info.offsetX;
    cameras[cameraIndex].offsetY = info.offsetY;
    cameras[cameraIndex].exposureTimeAbs = info.exposureTimeAbs;
    cameras[cameraIndex].enableExposureTimeBase = info.enableExposureTimeBase;
    cameras[cameraIndex].exposureTimeBaseAbs = info.exposureTimeBaseAbs;
    cameras[cameraIndex].exposureTimeRaw = info.exposureTimeRaw;
    cameras[cameraIndex].fps = info.fps;
    cameras[cameraIndex].enableAcquisitionFps = info.enableAcquisitionFps;
    cameras[cameraIndex].chunkModeActive = info.chunkModeActive;
    cameras[cameraIndex].enabledChunks = info.enabledChunks;
    CameraConfig::saveCameras(cameras);
}

QLabel* createInfoValueLabel(const QString& text = QString()) {
    QLabel* label = new QLabel(formatReadOnlyValue(text));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    label->setStyleSheet(
        "QLabel { background-color: #1C2128; border: 1px solid #30363D; "
        "border-radius: 6px; padding: 6px 8px; color: #E3E3E3; font-size: 12px; }"
    );
    return label;
}

void addFormRow(QFormLayout* layout, const QString& label, QWidget* field) {
    QLabel* labelWidget = new QLabel(label);
    labelWidget->setStyleSheet("color: #8B949E; font-size: 11px; font-weight: 500;");
    layout->addRow(labelWidget, field);
}

QString groupBoxStyle() {
    return "QGroupBox { font-weight: 600; color: #00E5FF; border: 1px solid #30363D; border-radius: 8px; "
           "margin-top: 6px; padding-top: 8px; font-size: 12px; }"
           "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }";
}
QString comboStyle() {
    return "QComboBox { background-color: #1C2128; border: 1px solid #30363D; border-radius: 6px; "
           "padding: 6px 8px; color: #E3E3E3; font-size: 12px; }"
           "QComboBox:focus { border-color: #00E5FF; }";
}
QString spinStyle() {
    return "QSpinBox { background-color: #1C2128; border: 1px solid #30363D; border-radius: 6px; "
           "padding: 6px 8px; color: #E3E3E3; font-size: 12px; }"
           "QSpinBox:focus { border-color: #00E5FF; }";
}
QString doubleSpinStyle() {
    return "QDoubleSpinBox { background-color: #1C2128; border: 1px solid #30363D; border-radius: 6px; "
           "padding: 6px 8px; color: #E3E3E3; font-size: 12px; }"
           "QDoubleSpinBox:focus { border-color: #00E5FF; }";
}
QFrame* buildCallout(QWidget* parent) {
    QFrame* callout = new QFrame(parent);
    callout->setStyleSheet(
        "QFrame { background-color: rgba(224, 168, 0, 0.08); border: 1px solid #E0A800; border-radius: 6px; }");
    QHBoxLayout* lay = new QHBoxLayout(callout);
    lay->setContentsMargins(10, 6, 10, 6);
    lay->setSpacing(8);
    QLabel* icon = new QLabel(callout);
    icon->setPixmap(IconManager::instance().warning(16).pixmap(16, 16));
    QLabel* text = new QLabel("Changes staged - stop the camera to apply.", callout);
    text->setStyleSheet("color: #E0A800; font-size: 11px; font-weight: 500; background: transparent;");
    QPushButton* stopApply = new QPushButton("Stop & Apply", callout);
    stopApply->setStyleSheet(
        "QPushButton { background: #1C2128; color: #E0A800; border: 1px solid #E0A800; border-radius: 4px; "
        "padding: 3px 8px; font-size: 10px; font-weight: 600; }"
        "QPushButton:hover { background: rgba(224, 168, 0, 0.15); }");
    lay->addWidget(icon);
    lay->addWidget(text, 1);
    lay->addWidget(stopApply);
    callout->setVisible(false);
    return callout;
}

}

CameraDeviceSettingsDialog::CameraDeviceSettingsDialog(int cameraIndex, const CameraInfo& info,
                                                       CameraManager* cameraManager,
                                                       bool editable, QWidget* parent)
    : QDialog(parent)
    , cameraIndex_(cameraIndex)
    , originalInfo_(info)
    , currentInfo_(info)
    , cameraManager_(cameraManager)
    , editable_(editable) {
    setupUi();
    if (cameraManager_) {
        liveSettings_ = cameraManager_->readLiveDeviceSettings(cameraIndex_);
    }
    if (liveSettings_.ok) {
        // Show the ACTUAL camera state, not the saved config: the dialog is a
        // setup surface and its fields must reflect what the camera is doing.
        if (!liveSettings_.pixelFormat.isEmpty()) {
            currentInfo_.pixelFormat = liveSettings_.pixelFormat;
            originalInfo_.pixelFormat = liveSettings_.pixelFormat;
        }
        if (liveSettings_.width > 0) {
            currentInfo_.width = liveSettings_.width;
            originalInfo_.width = liveSettings_.width;
        }
        if (liveSettings_.height > 0) {
            currentInfo_.height = liveSettings_.height;
            originalInfo_.height = liveSettings_.height;
        }
        currentInfo_.offsetX = liveSettings_.offsetX;
        originalInfo_.offsetX = liveSettings_.offsetX;
        currentInfo_.offsetY = liveSettings_.offsetY;
        originalInfo_.offsetY = liveSettings_.offsetY;
        if (liveSettings_.exposureUs > 0.0) {
            currentInfo_.exposureTimeAbs = liveSettings_.exposureUs;
            originalInfo_.exposureTimeAbs = liveSettings_.exposureUs;
        }
        if (liveSettings_.exposureTimeBaseAbs > 0.0) {
            currentInfo_.exposureTimeBaseAbs = liveSettings_.exposureTimeBaseAbs;
            originalInfo_.exposureTimeBaseAbs = liveSettings_.exposureTimeBaseAbs;
        }
        if (liveSettings_.exposureTimeRaw > 0) {
            currentInfo_.exposureTimeRaw = liveSettings_.exposureTimeRaw;
            originalInfo_.exposureTimeRaw = liveSettings_.exposureTimeRaw;
        }
        if (liveSettings_.acquisitionFrameRate > 0.0) {
            currentInfo_.fps = liveSettings_.acquisitionFrameRate;
            originalInfo_.fps = liveSettings_.acquisitionFrameRate;
        } else if (liveSettings_.resultingFrameRate > 0.0) {
            currentInfo_.fps = liveSettings_.resultingFrameRate;
            originalInfo_.fps = liveSettings_.resultingFrameRate;
        }
        currentInfo_.enableAcquisitionFps = liveSettings_.acquisitionFrameRateEnable;
        originalInfo_.enableAcquisitionFps = liveSettings_.acquisitionFrameRateEnable;
        currentInfo_.chunkModeActive = liveSettings_.chunkModeActive;
        originalInfo_.chunkModeActive = liveSettings_.chunkModeActive;
        if (!liveSettings_.enabledChunks.isEmpty()) {
            currentInfo_.enabledChunks = liveSettings_.enabledChunks;
            originalInfo_.enabledChunks = liveSettings_.enabledChunks;
        }
        if (liveSettings_.temperature > 0.0) {
            currentInfo_.temperature = liveSettings_.temperature;
        }
    }
    populateUi();
    refreshLiveDeviceInfo();
}

CameraInfo CameraDeviceSettingsDialog::updatedInfo() const {
    return currentInfo_;
}

bool CameraDeviceSettingsDialog::requiresRestart() const {
    return hasStopRequiredChanges();
}

void CameraDeviceSettingsDialog::setupUi() {
    setWindowTitle(QString("Camera %1 - Device Settings").arg(originalInfo_.id));
    setModal(true);
    resize(980, 680);

    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setSpacing(12);
    rootLayout->setContentsMargins(14, 14, 14, 14);

    // Title bar: camera icon + title + live status chip
    QHBoxLayout* titleLayout = new QHBoxLayout();
    titleLayout->setSpacing(10);
    QLabel* titleIcon = new QLabel(this);
    titleIcon->setPixmap(IconManager::instance().camera(20).pixmap(20, 20));
    titleLayout->addWidget(titleIcon);
    QLabel* titleLabel = new QLabel(QString("Camera %1 - Device Settings").arg(originalInfo_.id), this);
    titleLabel->setStyleSheet("font-size: 17px; font-weight: 600; color: #E3E3E3;");
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    statusChipLabel_ = new QLabel(this);
    statusChipLabel_->setStyleSheet(
        "QLabel { color: #8B949E; font-size: 12px; font-weight: 600; "
        "padding: 3px 10px; border: 1px solid #30363D; border-radius: 10px; }");
    titleLayout->addWidget(statusChipLabel_);
    rootLayout->addLayout(titleLayout);

    // Two-pane body
    QHBoxLayout* bodyLayout = new QHBoxLayout();
    bodyLayout->setSpacing(12);
    bodyLayout->addWidget(buildSidebar());
    detailStack_ = new QStackedWidget(this);
    bodyLayout->addWidget(detailStack_, 1);
    rootLayout->addLayout(bodyLayout, 1);

    // Footer
    QHBoxLayout* footerLayout = new QHBoxLayout();
    footerLayout->setSpacing(10);
    footerLayout->addStretch();
    cancelBtn_ = new QPushButton("Cancel", this);
    cancelBtn_->setIcon(IconManager::instance().close(16));
    applyStagedBtn_ = new QPushButton("Apply Staged", this);
    applyStagedBtn_->setIcon(IconManager::instance().save(16));
    applyStagedBtn_->setEnabled(false);
    applyBtn_ = new QPushButton("Close", this);
    applyBtn_->setIcon(IconManager::instance().check(16));
    footerLayout->addWidget(cancelBtn_);
    footerLayout->addWidget(applyStagedBtn_);
    footerLayout->addWidget(applyBtn_);
    rootLayout->addLayout(footerLayout);

    const QString buttonBase =
        "QPushButton { border: 1px solid #30363D; border-radius: 6px; padding: 7px 14px; font-size: 12px; font-weight: 600; } ";
    cancelBtn_->setStyleSheet(buttonBase +
        "QPushButton { background: transparent; color: #E3E3E3; } QPushButton:hover { border-color: #8B949E; }");
    applyStagedBtn_->setStyleSheet(buttonBase +
        "QPushButton { background: #1C2128; color: #E0A800; } QPushButton:hover { border-color: #E0A800; }"
        "QPushButton:disabled { color: #6E7681; border-color: #30363D; }");
    applyBtn_->setStyleSheet(buttonBase +
        "QPushButton { background: #238636; color: white; } QPushButton:hover { background: #2EA043; }");

    buildDetailPages();
    navList_->setCurrentRow(0);

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(2000);
    refreshTimer_->start();

    const auto registerChangeSignal = [this](QObject* obj, const char* signal) {
        connect(obj, signal, this, SLOT(onValueChanged()));
    };
    registerChangeSignal(pixelFormatCombo_, SIGNAL(currentIndexChanged(int)));
    registerChangeSignal(widthSpin_, SIGNAL(valueChanged(int)));
    registerChangeSignal(heightSpin_, SIGNAL(valueChanged(int)));
    registerChangeSignal(offsetXSpin_, SIGNAL(valueChanged(int)));
    registerChangeSignal(offsetYSpin_, SIGNAL(valueChanged(int)));
    registerChangeSignal(exposureTimeAbsSpin_, SIGNAL(valueChanged(double)));
    registerChangeSignal(enableExposureTimeBaseCheck_, SIGNAL(toggled(bool)));
    registerChangeSignal(exposureTimeBaseSpin_, SIGNAL(valueChanged(double)));
    registerChangeSignal(exposureTimeRawSpin_, SIGNAL(valueChanged(int)));
    registerChangeSignal(enableAcquisitionRateCheck_, SIGNAL(toggled(bool)));
    registerChangeSignal(acquisitionRateSpin_, SIGNAL(valueChanged(double)));
    registerChangeSignal(chunkModeActiveCheck_, SIGNAL(toggled(bool)));
    connect(chunkListWidget_, &QListWidget::itemChanged, this, &CameraDeviceSettingsDialog::onValueChanged);
    connect(navList_, &QListWidget::currentRowChanged, this, &CameraDeviceSettingsDialog::onNavChanged);
    connect(cancelBtn_, &QPushButton::clicked, this, &CameraDeviceSettingsDialog::onCancelClicked);
    connect(applyBtn_, &QPushButton::clicked, this, &CameraDeviceSettingsDialog::closeDialog);
    connect(applyStagedBtn_, &QPushButton::clicked, this, &CameraDeviceSettingsDialog::applyStagedChanges);
    connect(runStateBtn_, &QPushButton::clicked, this, &CameraDeviceSettingsDialog::toggleCameraRunState);
    connect(refreshTimer_, &QTimer::timeout, this, &CameraDeviceSettingsDialog::refreshLiveDeviceInfo);

    // Wire Stop & Apply on every callout: stop the camera if running, then apply staged.
    for (auto it = stagedCallouts_.constBegin(); it != stagedCallouts_.constEnd(); ++it) {
        QPushButton* btn = it.value()->findChild<QPushButton*>();
        if (btn) {
            btn->setEnabled(isCameraReachable(cameraManager_, cameraIndex_, currentInfo_));
            connect(btn, &QPushButton::clicked, this, [this]() {
                if (cameraManager_ && cameraManager_->isCameraRunning(cameraIndex_)) {
                    cameraManager_->stopCamera(cameraIndex_);
                }
                applyStagedChanges();
                refreshLiveDeviceInfo();
            });
        }
    }
}

QWidget* CameraDeviceSettingsDialog::buildSidebar() {
    QWidget* sidebar = new QWidget(this);
    sidebar->setFixedWidth(230);
    QVBoxLayout* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(10);

    statusCard_ = new QFrame(sidebar);
    statusCard_->setStyleSheet(
        "QFrame { background-color: #1C2128; border: 1px solid #30363D; border-radius: 8px; "
        "border-left: 4px solid #30363D; }");
    QVBoxLayout* cardLayout = new QVBoxLayout(statusCard_);
    cardLayout->setContentsMargins(12, 10, 12, 10);
    cardLayout->setSpacing(6);
    statusTitleLabel_ = new QLabel(statusCard_);
    statusTitleLabel_->setWordWrap(true);
    statusTitleLabel_->setStyleSheet(
        "font-size: 13px; font-weight: 600; color: #E3E3E3; line-height: 1.3;");
    const QString rowPill = "background: rgba(33, 38, 45, 0.5); border-radius: 4px; padding: 3px 8px;";
    statusModelLabel_ = new QLabel(statusCard_);
    statusModelLabel_->setStyleSheet("font-size: 11px; color: #8B949E; " + rowPill);
    statusIpLabel_ = new QLabel(statusCard_);
    statusIpLabel_->setStyleSheet("font-size: 11px; color: #8B949E; font-family: 'SF Mono', Monaco, monospace; " + rowPill);
    statusTempLabel_ = new QLabel(statusCard_);
    statusTempLabel_->setStyleSheet("font-size: 11px; color: #8B949E; " + rowPill);

    QFrame* cardSep = new QFrame(statusCard_);
    cardSep->setFrameShape(QFrame::HLine);
    cardSep->setStyleSheet("background-color: #30363D; max-height: 1px; border: none; margin: 2px 0;");

    runStateBtn_ = new QPushButton(statusCard_);
    runStateBtn_->setStyleSheet(
        "QPushButton { border-radius: 6px; padding: 7px 10px; font-size: 12px; font-weight: 600; margin-top: 8px; "
        "color: #6E7681; border: 1px solid #30363D; background: rgba(48, 54, 61, 0.35); }"
        "QPushButton:disabled { color: #6E7681; border: 1px solid #30363D; background: rgba(48, 54, 61, 0.35); }");
    cardLayout->addWidget(statusTitleLabel_);
    cardLayout->addWidget(cardSep);
    cardLayout->addWidget(statusModelLabel_);
    cardLayout->addWidget(statusIpLabel_);
    cardLayout->addWidget(statusTempLabel_);
    cardLayout->addWidget(runStateBtn_);
    sideLayout->addWidget(statusCard_);

    navList_ = new QListWidget(sidebar);
    navList_->setStyleSheet(
        "QListWidget { background: transparent; border: none; outline: 0; }"
        "QListWidget::item { padding: 8px 10px; border-radius: 6px; color: #8B949E; font-size: 12px; font-weight: 500; }"
        "QListWidget::item:hover { background-color: rgba(0, 229, 255, 0.06); color: #E3E3E3; }"
        "QListWidget::item:selected { background-color: rgba(0, 229, 255, 0.12); color: #E3E3E3; border-left: 2px solid #00E5FF; }");
    navList_->setFocusPolicy(Qt::StrongFocus);
    sideLayout->addWidget(navList_, 1);
    return sidebar;
}

void CameraDeviceSettingsDialog::buildDetailPages() {
    // --- Page: Image Format (group 0) ---
    QWidget* imagePage = new QWidget(this);
    QVBoxLayout* imagePageLayout = new QVBoxLayout(imagePage);
    imagePageLayout->setContentsMargins(4, 0, 4, 0);
    imagePageLayout->setSpacing(10);
    QLabel* imageTitle = new QLabel("Image Format", imagePage);
    imageTitle->setStyleSheet("font-size: 14px; font-weight: 600; color: #E3E3E3;");
    imagePageLayout->addWidget(imageTitle);
    QLabel* imageHint = new QLabel("Pixel format of the acquired image.", imagePage);
    imageHint->setStyleSheet("font-size: 11px; color: #8B949E;");
    imagePageLayout->addWidget(imageHint);
    stagedCallouts_.insert(0, buildCallout(imagePage));
    imagePageLayout->addWidget(stagedCallouts_.value(0));

    QGroupBox* imageFormatGroup = new QGroupBox("Image Format Controls", imagePage);
    imageFormatGroup->setStyleSheet(groupBoxStyle());
    QFormLayout* imageFormatLayout = new QFormLayout(imageFormatGroup);
    imageFormatLayout->setContentsMargins(14, 16, 14, 14);
    imageFormatLayout->setHorizontalSpacing(14);
    imageFormatLayout->setVerticalSpacing(10);
    pixelFormatCombo_ = new QComboBox(imageFormatGroup);
    pixelFormatCombo_->setEditable(false);
    pixelFormatCombo_->setStyleSheet(comboStyle());
    addFormRow(imageFormatLayout, "Pixel Format:", pixelFormatCombo_);
    imagePageLayout->addWidget(imageFormatGroup);
    imagePageLayout->addStretch();
    detailStack_->addWidget(imagePage);

    // --- Page: AOI (group 1) ---
    QWidget* aoiPage = new QWidget(this);
    QVBoxLayout* aoiPageLayout = new QVBoxLayout(aoiPage);
    aoiPageLayout->setContentsMargins(4, 0, 4, 0);
    aoiPageLayout->setSpacing(10);
    QLabel* aoiTitle = new QLabel("AOI", aoiPage);
    aoiTitle->setStyleSheet("font-size: 14px; font-weight: 600; color: #E3E3E3;");
    aoiPageLayout->addWidget(aoiTitle);
    QLabel* aoiHint = new QLabel("Region of interest; requires camera stop to apply.", aoiPage);
    aoiHint->setStyleSheet("font-size: 11px; color: #8B949E;");
    aoiPageLayout->addWidget(aoiHint);
    stagedCallouts_.insert(1, buildCallout(aoiPage));
    aoiPageLayout->addWidget(stagedCallouts_.value(1));

    QGroupBox* roiGroup = new QGroupBox("AOI Controls", aoiPage);
    roiGroup->setStyleSheet(groupBoxStyle());
    QFormLayout* roiLayout = new QFormLayout(roiGroup);
    roiLayout->setContentsMargins(14, 16, 14, 14);
    roiLayout->setHorizontalSpacing(14);
    roiLayout->setVerticalSpacing(10);
    widthSpin_ = new QSpinBox(roiGroup);
    widthSpin_->setRange(1, 100000);
    widthSpin_->setSuffix(" px");
    heightSpin_ = new QSpinBox(roiGroup);
    heightSpin_->setRange(1, 100000);
    heightSpin_->setSuffix(" px");
    offsetXSpin_ = new QSpinBox(roiGroup);
    offsetXSpin_->setRange(0, 100000);
    offsetXSpin_->setSuffix(" px");
    offsetYSpin_ = new QSpinBox(roiGroup);
    offsetYSpin_->setRange(0, 100000);
    offsetYSpin_->setSuffix(" px");
    widthSpin_->setStyleSheet(spinStyle());
    heightSpin_->setStyleSheet(spinStyle());
    offsetXSpin_->setStyleSheet(spinStyle());
    offsetYSpin_->setStyleSheet(spinStyle());
    addFormRow(roiLayout, "Width:", widthSpin_);
    addFormRow(roiLayout, "Height:", heightSpin_);
    addFormRow(roiLayout, "Offset X:", offsetXSpin_);
    addFormRow(roiLayout, "Offset Y:", offsetYSpin_);
    sensorWidthValueLabel_ = createInfoValueLabel();
    sensorHeightValueLabel_ = createInfoValueLabel();
    maxWidthValueLabel_ = createInfoValueLabel();
    maxHeightValueLabel_ = createInfoValueLabel();
    addFormRow(roiLayout, "Sensor Width:", sensorWidthValueLabel_);
    addFormRow(roiLayout, "Sensor Height:", sensorHeightValueLabel_);
    addFormRow(roiLayout, "Max Width:", maxWidthValueLabel_);
    addFormRow(roiLayout, "Max Height:", maxHeightValueLabel_);
    aoiPageLayout->addWidget(roiGroup);
    aoiPageLayout->addStretch();
    detailStack_->addWidget(aoiPage);

    // --- Page: Exposure & Rate (group 2) ---
    QWidget* exposurePage = new QWidget(this);
    QVBoxLayout* exposurePageLayout = new QVBoxLayout(exposurePage);
    exposurePageLayout->setContentsMargins(4, 0, 4, 0);
    exposurePageLayout->setSpacing(10);
    QLabel* exposureTitle = new QLabel("Exposure & Rate", exposurePage);
    exposureTitle->setStyleSheet("font-size: 14px; font-weight: 600; color: #E3E3E3;");
    exposurePageLayout->addWidget(exposureTitle);
    QLabel* exposureHint = new QLabel("Exposure and framerate apply live to the camera - no restart needed.", exposurePage);
    exposureHint->setStyleSheet("font-size: 11px; color: #8B949E;");
    exposurePageLayout->addWidget(exposureHint);
    stagedCallouts_.insert(2, buildCallout(exposurePage));
    exposurePageLayout->addWidget(stagedCallouts_.value(2));

    QGroupBox* exposureGroup = new QGroupBox("Acquisition Controls", exposurePage);
    exposureGroup->setStyleSheet(groupBoxStyle());
    QFormLayout* exposureForm = new QFormLayout(exposureGroup);
    exposureForm->setContentsMargins(14, 16, 14, 14);
    exposureForm->setHorizontalSpacing(14);
    exposureForm->setVerticalSpacing(10);
    exposureTimeAbsSpin_ = new QDoubleSpinBox(exposureGroup);
    exposureTimeAbsSpin_->setRange(0.0, 100000000.0);
    exposureTimeAbsSpin_->setDecimals(2);
    exposureTimeAbsSpin_->setSuffix(" us");
    exposureTimeBaseSpin_ = new QDoubleSpinBox(exposureGroup);
    exposureTimeBaseSpin_->setRange(0.0, 1000000.0);
    exposureTimeBaseSpin_->setDecimals(2);
    exposureTimeBaseSpin_->setSuffix(" us");
    exposureTimeRawSpin_ = new QSpinBox(exposureGroup);
    exposureTimeRawSpin_->setRange(0, 100000000);
    enableExposureTimeBaseCheck_ = new QCheckBox("Enable Exposure Time Base", exposureGroup);
    enableAcquisitionRateCheck_ = new QCheckBox("Enable Acquisition Framerate", exposureGroup);
    acquisitionRateSpin_ = new QDoubleSpinBox(exposureGroup);
    acquisitionRateSpin_->setRange(0.0, 10000.0);
    acquisitionRateSpin_->setDecimals(3);
    acquisitionRateSpin_->setSuffix(" Hz");
    exposureTimeAbsSpin_->setStyleSheet(doubleSpinStyle());
    exposureTimeBaseSpin_->setStyleSheet(doubleSpinStyle());
    acquisitionRateSpin_->setStyleSheet(doubleSpinStyle());
    exposureTimeRawSpin_->setStyleSheet(spinStyle());
    enableExposureTimeBaseCheck_->setStyleSheet("color: #E3E3E3; font-size: 12px;");
    enableAcquisitionRateCheck_->setStyleSheet("color: #E3E3E3; font-size: 12px;");
    resultingRateValueLabel_ = createInfoValueLabel();
    addFormRow(exposureForm, "Exposure Time (Abs):", exposureTimeAbsSpin_);
    addFormRow(exposureForm, QString(), enableExposureTimeBaseCheck_);
    addFormRow(exposureForm, "Exposure Time Base:", exposureTimeBaseSpin_);
    addFormRow(exposureForm, "Exposure Time (Raw):", exposureTimeRawSpin_);
    addFormRow(exposureForm, QString(), enableAcquisitionRateCheck_);
    addFormRow(exposureForm, "Acquisition Framerate:", acquisitionRateSpin_);
    addFormRow(exposureForm, "Resulting Framerate:", resultingRateValueLabel_);
    exposurePageLayout->addWidget(exposureGroup);
    exposurePageLayout->addStretch();
    detailStack_->addWidget(exposurePage);

    // --- Page: Chunk Data (group 3) ---
    QWidget* chunkPage = new QWidget(this);
    QVBoxLayout* chunkPageLayout = new QVBoxLayout(chunkPage);
    chunkPageLayout->setContentsMargins(4, 0, 4, 0);
    chunkPageLayout->setSpacing(10);
    QLabel* chunkTitle = new QLabel("Chunk Data", chunkPage);
    chunkTitle->setStyleSheet("font-size: 14px; font-weight: 600; color: #E3E3E3;");
    chunkPageLayout->addWidget(chunkTitle);
    QLabel* chunkHint = new QLabel("Choose chunk items included in the payload; requires camera stop.", chunkPage);
    chunkHint->setStyleSheet("font-size: 11px; color: #8B949E;");
    chunkPageLayout->addWidget(chunkHint);
    stagedCallouts_.insert(3, buildCallout(chunkPage));
    chunkPageLayout->addWidget(stagedCallouts_.value(3));

    QGroupBox* chunkGroup = new QGroupBox("Chunk Data Streams", chunkPage);
    chunkGroup->setStyleSheet(groupBoxStyle());
    QVBoxLayout* chunkGroupLayout = new QVBoxLayout(chunkGroup);
    chunkGroupLayout->setContentsMargins(14, 16, 14, 14);
    chunkGroupLayout->setSpacing(10);
    chunkModeActiveCheck_ = new QCheckBox("Chunk Mode Active", chunkGroup);
    chunkModeActiveCheck_->setStyleSheet("color: #E3E3E3; font-size: 12px;");
    chunkGroupLayout->addWidget(chunkModeActiveCheck_);
    QLabel* chunkHelp = new QLabel("Choose which chunk items should be included in the payload.", chunkGroup);
    chunkHelp->setWordWrap(true);
    chunkHelp->setStyleSheet("color: #8B949E; font-size: 11px;");
    chunkGroupLayout->addWidget(chunkHelp);
    chunkListWidget_ = new QListWidget(chunkGroup);
    chunkListWidget_->setSelectionMode(QAbstractItemView::NoSelection);
    chunkListWidget_->setAlternatingRowColors(true);
    chunkListWidget_->setStyleSheet(
        "QListWidget { background-color: #161B22; border: 1px solid #30363D; border-radius: 6px; color: #E3E3E3; outline: 0; }"
        "QListWidget::item { padding: 8px 10px; }"
        "QListWidget::item:alternate { background-color: #1C2128; }"
        "QListWidget::item:hover { background-color: rgba(0, 229, 255, 0.08); }"
        "QListWidget::item:selected { background-color: rgba(0, 229, 255, 0.15); color: #E3E3E3; }"
        "QScrollBar:vertical { background: #161B22; width: 8px; margin: 0; border-radius: 4px; }"
        "QScrollBar::handle:vertical { background: #30363D; min-height: 24px; border-radius: 4px; }"
        "QScrollBar::handle:vertical:hover { background: #484F58; }"
        "QScrollBar::add-line:vertical { height: 0; background: none; }"
        "QScrollBar::sub-line:vertical { height: 0; background: none; }"
        "QScrollBar::add-page:vertical { background: none; }"
        "QScrollBar::sub-page:vertical { background: none; }");
    chunkListWidget_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    chunkGroupLayout->addWidget(chunkListWidget_);
    chunkPageLayout->addWidget(chunkGroup);
    chunkPageLayout->addStretch();
    detailStack_->addWidget(chunkPage);

    // --- Page: Device Information (group 4) ---
    QWidget* infoPage = new QWidget(this);
    QVBoxLayout* infoPageLayout = new QVBoxLayout(infoPage);
    infoPageLayout->setContentsMargins(4, 0, 4, 0);
    infoPageLayout->setSpacing(10);
    QLabel* infoTitle = new QLabel("Device Information", infoPage);
    infoTitle->setStyleSheet("font-size: 14px; font-weight: 600; color: #E3E3E3;");
    infoPageLayout->addWidget(infoTitle);
    QLabel* infoHint = new QLabel("Read-only device identity and live connection details.", infoPage);
    infoHint->setStyleSheet("font-size: 11px; color: #8B949E;");
    infoPageLayout->addWidget(infoHint);

    QGroupBox* deviceInfoGroup = new QGroupBox("Device Information", infoPage);
    deviceInfoGroup->setStyleSheet(groupBoxStyle());
    QFormLayout* deviceInfoForm = new QFormLayout(deviceInfoGroup);
    deviceInfoForm->setContentsMargins(14, 16, 14, 14);
    deviceInfoForm->setHorizontalSpacing(14);
    deviceInfoForm->setVerticalSpacing(10);
    vendorValueLabel_ = createInfoValueLabel();
    modelInfoValueLabel_ = createInfoValueLabel();
    manufacturerInfoValueLabel_ = createInfoValueLabel();
    deviceVersionValueLabel_ = createInfoValueLabel();
    firmwareVersionValueLabel_ = createInfoValueLabel();
    deviceIdValueLabel_ = createInfoValueLabel();
    modelValueLabel_ = createInfoValueLabel();
    ipValueLabel_ = createInfoValueLabel();
    addFormRow(deviceInfoForm, "Vendor Name:", vendorValueLabel_);
    addFormRow(deviceInfoForm, "Model Name:", modelInfoValueLabel_);
    addFormRow(deviceInfoForm, "Manufacturer Info:", manufacturerInfoValueLabel_);
    addFormRow(deviceInfoForm, "Device Version:", deviceVersionValueLabel_);
    addFormRow(deviceInfoForm, "Firmware Version:", firmwareVersionValueLabel_);
    addFormRow(deviceInfoForm, "Device ID:", deviceIdValueLabel_);
    addFormRow(deviceInfoForm, "Live Model:", modelValueLabel_);
    addFormRow(deviceInfoForm, "Live IP:", ipValueLabel_);
    infoPageLayout->addWidget(deviceInfoGroup);
    infoPageLayout->addStretch();
    detailStack_->addWidget(infoPage);

    // --- Page: Service (group 5) ---
    QWidget* servicePage = new QWidget(this);
    QVBoxLayout* servicePageLayout = new QVBoxLayout(servicePage);
    servicePageLayout->setContentsMargins(4, 0, 4, 0);
    servicePageLayout->setSpacing(10);
    QLabel* serviceTitle = new QLabel("Service", servicePage);
    serviceTitle->setStyleSheet("font-size: 14px; font-weight: 600; color: #E3E3E3;");
    servicePageLayout->addWidget(serviceTitle);
    QLabel* serviceHint = new QLabel("Camera maintenance actions.", servicePage);
    serviceHint->setStyleSheet("font-size: 11px; color: #8B949E;");
    servicePageLayout->addWidget(serviceHint);

    QLabel* serviceNote = new QLabel("Reset Device is not available in this build.", servicePage);
    serviceNote->setWordWrap(true);
    serviceNote->setStyleSheet("color: #8B949E; font-size: 12px;");
    servicePageLayout->addWidget(serviceNote);
    servicePageLayout->addStretch();
    detailStack_->addWidget(servicePage);

    struct NavEntry { int id; const char* icon; const char* label; };
    const NavEntry entries[] = {
        {0, Icons::EDIT, "Image Format"},
        {1, Icons::SETTINGS, "AOI"},
        {2, Icons::SPEED, "Exposure & Rate"},
        {3, Icons::INFO, "Chunk Data"},
        {4, Icons::CAMERA, "Device Info"},
        {5, Icons::SETTINGS, "Service"},
    };
    for (const auto& e : entries) {
        QListWidgetItem* item = new QListWidgetItem(
            IconManager::instance().getIcon(e.icon, 16), QString::fromLatin1(e.label), navList_);
        navItems_.insert(e.id, item);
    }
}

void CameraDeviceSettingsDialog::onNavChanged(int row) {
    if (row >= 0 && row < detailStack_->count()) {
        detailStack_->setCurrentIndex(row);
    }
}

int CameraDeviceSettingsDialog::stagedCount() const {
    return stagedFields_.size();
}

QSet<QString> CameraDeviceSettingsDialog::stagedFieldsInGroup(int groupId) const {
    QSet<QString> fields;
    switch (groupId) {
    case 0: if (stagedFields_.contains("pixelFormat")) fields.insert("pixelFormat"); break;
    case 1: for (const char* f : {"width", "height", "offsetX", "offsetY"}) if (stagedFields_.contains(f)) fields.insert(f); break;
    case 2: break;  // Exposure & Rate is fully live — never staged
    case 3: for (const char* f : {"chunkMode", "chunks"}) if (stagedFields_.contains(f)) fields.insert(f); break;
    default: break;
    }
    return fields;
}

void CameraDeviceSettingsDialog::stageField(const QString& field) {
    if (!stagedFields_.contains(field)) {
        stagedFields_.insert(field);
        updateStagedBadges();
        updateStagedCallouts();
        updateApplyStagedEnabled();
    }
}

void CameraDeviceSettingsDialog::clearStaged() {
    stagedFields_.clear();
    updateStagedBadges();
    updateStagedCallouts();
    updateApplyStagedEnabled();
}

void CameraDeviceSettingsDialog::updateStagedBadges() {
    // Amber dot via DecorationRole; empty pixmap removes it.
    const QPixmap amberDot = [this]() {
        QPixmap pm(10, 10);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor("#E0A800"));
        p.setPen(Qt::NoPen);
        p.drawEllipse(1, 1, 8, 8);
        return pm;
    }();
    for (auto it = navItems_.constBegin(); it != navItems_.constEnd(); ++it) {
        const bool hasStaged = !stagedFieldsInGroup(it.key()).isEmpty();
        it.value()->setData(Qt::DecorationRole, hasStaged ? QVariant(amberDot) : QVariant());
    }
}

void CameraDeviceSettingsDialog::updateStagedCallouts() {
    const bool reachable = isCameraReachable(cameraManager_, cameraIndex_, currentInfo_);
    for (auto it = stagedCallouts_.constBegin(); it != stagedCallouts_.constEnd(); ++it) {
        it.value()->setVisible(!stagedFieldsInGroup(it.key()).isEmpty());
        if (QPushButton* btn = it.value()->findChild<QPushButton*>()) {
            btn->setEnabled(reachable);
        }
    }
}

void CameraDeviceSettingsDialog::updateApplyStagedEnabled() {
    const bool reachable = isCameraReachable(cameraManager_, cameraIndex_, currentInfo_);
    const bool connected = cameraManager_ && (cameraManager_->isCameraConnected(cameraIndex_) || cameraManager_->isCameraOpen(cameraIndex_));
    const bool running = connected && cameraManager_->isCameraRunning(cameraIndex_);
    applyStagedBtn_->setEnabled(editable_ && reachable && !running && !stagedFields_.isEmpty());
}

bool CameraDeviceSettingsDialog::confirmStagedClose() {
    if (stagedFields_.isEmpty()) {
        return true;
    }
    const bool connected = cameraManager_ && (cameraManager_->isCameraConnected(cameraIndex_) || cameraManager_->isCameraOpen(cameraIndex_));
    const bool running = connected && cameraManager_->isCameraRunning(cameraIndex_);
    QMessageBox box(this);
    box.setWindowTitle("Staged Changes");
    box.setIcon(QMessageBox::Warning);
    box.setText(QString("%1 change(s) are staged but not yet applied to the camera.")
                    .arg(stagedCount()));
    QPushButton* applyAndClose = box.addButton("Apply & Close", QMessageBox::AcceptRole);
    QPushButton* discard = box.addButton("Discard", QMessageBox::DestructiveRole);
    box.addButton("Keep Editing", QMessageBox::RejectRole);
    if (running) {
        applyAndClose->setEnabled(false);
        applyAndClose->setToolTip("Stop the camera to apply staged changes.");
    }
    box.exec();
    if (box.clickedButton() == applyAndClose) {
        applyStagedChanges();
        return stagedFields_.isEmpty();
    }
    if (box.clickedButton() == discard) {
        clearStaged();
        return true;
    }
    return false;  // Keep Editing
}

void CameraDeviceSettingsDialog::onCancelClicked() {
    if (confirmStagedClose()) {
        reject();
    }
}

void CameraDeviceSettingsDialog::closeEvent(QCloseEvent* event) {
    if (!confirmStagedClose()) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void CameraDeviceSettingsDialog::reject() {
    if (!confirmStagedClose()) {
        return;
    }
    QDialog::reject();
}

void CameraDeviceSettingsDialog::applyStagedChanges() {
    if (stagedFields_.isEmpty()) {
        return;
    }
    if (!validateInputs(nullptr)) {
        return;
    }

    if (!isCameraReachable(cameraManager_, cameraIndex_, currentInfo_)) {
        QMessageBox::information(this, "Apply Staged",
            "Camera is not reachable - staged changes kept.");
        return;
    }

    const bool connected = cameraManager_ && (cameraManager_->isCameraConnected(cameraIndex_) || cameraManager_->isCameraOpen(cameraIndex_));
    const bool running = connected && cameraManager_->isCameraRunning(cameraIndex_);
    if (!connected) {
        // Reachable on the network but not attached to the acquisition
        // runtime: a live write is impossible. Do NOT silently persist.
        QMessageBox::information(this, "Apply Staged",
            "Camera is online but not attached to the acquisition runtime.\n"
            "Save the camera configuration and restart cameras, then apply staged changes.");
        return;
    }
    if (running) {
        QMessageBox::information(this, "Apply Staged",
            "Stop the camera first (sidebar button) to apply staged changes.");
        return;
    }

    if (cameraManager_ && connected) {
        cameraManager_->applyCameraDeviceSettings(cameraIndex_, currentInfo_);
    }
    persistSharedCameraSettings(cameraIndex_, currentInfo_);
    emit settingsApplied(currentInfo_);
    clearStaged();
    refreshLiveDeviceInfo();
}

void CameraDeviceSettingsDialog::populateUi() {
    populating_ = true;
    statusTitleLabel_->setText(
        QString("<b>%1</b><br><span style='font-size:11px; color:#8B949E;'>%2 · %3 mm</span>")
            .arg(originalInfo_.name.toHtmlEscaped(),
                 originalInfo_.location.toHtmlEscaped(),
                 QString::number(originalInfo_.machinePosition)));

    QStringList formats = defaultPixelFormats();
    if (formats.isEmpty()) {
        formats << "Mono8" << "Mono16";
    }
    formats.removeDuplicates();
    pixelFormatCombo_->addItems(formats);
    if (!currentInfo_.pixelFormat.trimmed().isEmpty() && pixelFormatCombo_->findText(currentInfo_.pixelFormat) == -1) {
        pixelFormatCombo_->addItem(currentInfo_.pixelFormat);
    }
    pixelFormatCombo_->setCurrentText(currentInfo_.pixelFormat.trimmed().isEmpty() ? QString("Mono8") : currentInfo_.pixelFormat);

    widthSpin_->setValue(currentInfo_.width);
    heightSpin_->setValue(currentInfo_.height);
    offsetXSpin_->setValue(currentInfo_.offsetX);
    offsetYSpin_->setValue(currentInfo_.offsetY);
    exposureTimeAbsSpin_->setValue(currentInfo_.exposureTimeAbs);
    enableExposureTimeBaseCheck_->setChecked(currentInfo_.enableExposureTimeBase);
    exposureTimeBaseSpin_->setValue(currentInfo_.exposureTimeBaseAbs);
    exposureTimeRawSpin_->setValue(currentInfo_.exposureTimeRaw);
    enableAcquisitionRateCheck_->setChecked(currentInfo_.enableAcquisitionFps);
    acquisitionRateSpin_->setValue(currentInfo_.fps);
    chunkModeActiveCheck_->setChecked(currentInfo_.chunkModeActive);

    for (const QString& chunk : availableChunkOptions()) {
        QListWidgetItem* item = new QListWidgetItem(chunk, chunkListWidget_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(currentInfo_.enabledChunks.contains(chunk) ? Qt::Checked : Qt::Unchecked);
    }

    populating_ = false;
    updateControlAvailability();
    onValueChanged();
}

void CameraDeviceSettingsDialog::refreshLiveDeviceInfo() {
    vendorValueLabel_->setText("Not available");
    modelInfoValueLabel_->setText(formatReadOnlyValue(originalInfo_.model));
    manufacturerInfoValueLabel_->setText("Not available");
    deviceVersionValueLabel_->setText("Not available");
    firmwareVersionValueLabel_->setText("Not available");
    deviceIdValueLabel_->setText(QString::number(originalInfo_.id));

    // Actual camera identity/details from the live read (direct or runtime).
    if (liveSettings_.ok) {
        vendorValueLabel_->setText(formatReadOnlyValue(liveSettings_.vendorName));
        modelInfoValueLabel_->setText(formatReadOnlyValue(liveSettings_.modelName));
        manufacturerInfoValueLabel_->setText(formatReadOnlyValue(liveSettings_.manufacturerInfo));
        deviceVersionValueLabel_->setText(formatReadOnlyValue(liveSettings_.deviceVersion));
        firmwareVersionValueLabel_->setText(formatReadOnlyValue(liveSettings_.firmwareVersion));
        deviceIdValueLabel_->setText(formatReadOnlyValue(liveSettings_.deviceId.isEmpty()
            ? QString::number(originalInfo_.id) : liveSettings_.deviceId));
    }

    // Sensor geometry: prefer the live read, then the runtime params, then config.
    int sensorW = 0;
    int sensorH = 0;
    if (liveSettings_.ok) {
        sensorW = liveSettings_.width;
        sensorH = liveSettings_.height;
    } else if (cameraManager_ && isCameraReachable(cameraManager_, cameraIndex_, currentInfo_)) {
        const CameraManager::CameraParams params = cameraManager_->getCameraParams(cameraIndex_);
        sensorW = params.width;
        sensorH = params.height;
    }
    if (sensorW <= 0) {
        sensorW = originalInfo_.width;
    }
    if (sensorH <= 0) {
        sensorH = originalInfo_.height;
    }
    sensorWidthValueLabel_->setText(QString::number(std::max(0, sensorW)));
    sensorHeightValueLabel_->setText(QString::number(std::max(0, sensorH)));
    maxWidthValueLabel_->setText(QString::number(std::max(0, sensorW)));
    maxHeightValueLabel_->setText(QString::number(std::max(0, sensorH)));

    if (liveSettings_.ok) {
        modelValueLabel_->setText(formatReadOnlyValue(liveSettings_.modelName));
        ipValueLabel_->setText(formatReadOnlyValue(liveSettings_.ipAddress));
    } else {
        modelValueLabel_->setText(formatReadOnlyValue(cameraManager_ ? QString::fromStdString(cameraManager_->getModelName(cameraIndex_)) : originalInfo_.model));
        ipValueLabel_->setText(formatReadOnlyValue(cameraManager_ ? QString::fromStdString(cameraManager_->getIpAddress(cameraIndex_)) : originalInfo_.ipAddress));
    }
    resultingRateValueLabel_->setText(QString::number(currentInfo_.fps, 'f', 3) + " Hz");
    updateSidebarStatus();
    updateStagedCallouts();
    updateApplyStagedEnabled();
}

void CameraDeviceSettingsDialog::updateSidebarStatus() {
    const bool reachable = isCameraReachable(cameraManager_, cameraIndex_, currentInfo_);
    const bool connected = cameraManager_ && (cameraManager_->isCameraConnected(cameraIndex_) || cameraManager_->isCameraOpen(cameraIndex_));
    const bool running = connected && cameraManager_->isCameraRunning(cameraIndex_);

    // Carried fix: refresh live temperature on the read-only label only.
    // Never overwrite with an unmeaningful reading (getTemperature returns -1.0
    // sentinel when the sensor is not reachable or not reporting).
    if (liveSettings_.temperature > 0.0) {
        currentInfo_.temperature = liveSettings_.temperature;
    } else if (cameraManager_ && reachable) {
        const double temp = cameraManager_->getTemperature(cameraIndex_);
        if (temp > 0.0) {
            currentInfo_.temperature = temp;
        }
    }

    QString chipText;
    QString chipColor;
    if (!reachable) {
        chipText = "Offline";
        chipColor = "#FF5A5A";
    } else if (running) {
        chipText = "Running";
        chipColor = "#2EA043";
    } else if (connected) {
        chipText = "Stopped";
        chipColor = "#E0A800";
    } else {
        chipText = "Online";
        chipColor = "#E0A800";
    }
    statusChipLabel_->setText(chipText);
    statusChipLabel_->setStyleSheet(
        QString("QLabel { color: %1; font-size: 12px; font-weight: 600; padding: 3px 10px; border: 1px solid %1; border-radius: 10px; }")
            .arg(chipColor));

    // Card left-border accent reflects live state
    QString cardAccent = !reachable ? "#FF5A5A" : (running ? "#2EA043" : "#E0A800");
    statusCard_->setStyleSheet(
        QString("QFrame { background-color: #1C2128; border: 1px solid #30363D; border-radius: 8px; "
                "border-left: 4px solid %1; }").arg(cardAccent));

    // Model row with label · value
    QString modelValue;
    if (liveSettings_.ok) {
        modelValue = liveSettings_.modelName;
    } else {
        modelValue = cameraManager_ ? QString::fromStdString(cameraManager_->getModelName(cameraIndex_)) : QString();
    }
    bool modelOk = !modelValue.trimmed().isEmpty()
                   && modelValue != "Not Connected" && modelValue != "Unknown Model";
    statusModelLabel_->setText(
        QString("<span style='color:#8B949E;'>Model</span>  "
                "<span style='color:%1;'>%2</span>")
            .arg(modelOk ? "#E3E3E3" : "#FF5A5A",
                 modelOk ? modelValue.toHtmlEscaped() : "Not Connected"));

    // IP row with label · value
    QString ipValue;
    if (liveSettings_.ok) {
        ipValue = liveSettings_.ipAddress;
    } else {
        ipValue = cameraManager_ ? QString::fromStdString(cameraManager_->getIpAddress(cameraIndex_)) : QString();
    }
    bool ipOk = !ipValue.trimmed().isEmpty() && ipValue != "Offline";
    statusIpLabel_->setText(
        QString("<span style='color:#8B949E;'>IP</span>  "
                "<span style='color:%1; font-family: SF Mono, Monaco, monospace;'>%2</span>")
            .arg(ipOk ? "#E3E3E3" : "#FF5A5A",
                 ipOk ? ipValue.toHtmlEscaped() : "Offline"));

    // Temperature row — show dash when sensor not reporting
    double temp = currentInfo_.temperature;
    if (temp > 0.0) {
        QString tempColor = temp > 50.0 ? "#FF5A5A" : "#E3E3E3";
        statusTempLabel_->setText(
            QString("<span style='color:#8B949E;'>Temp</span>  "
                    "<span style='color:%1;'>%2°C</span>")
                .arg(tempColor, QString::number(temp, 'f', 1)));
    } else {
        statusTempLabel_->setText(
            "<span style='color:#8B949E;'>Temp</span>  "
            "<span style='color:#6E7681;'>—</span>");
    }

    // Run-state button
    if (!reachable) {
        runStateBtn_->setText("Unavailable");
        runStateBtn_->setEnabled(false);
        runStateBtn_->setStyleSheet(
            "QPushButton { border-radius: 6px; padding: 7px 10px; font-size: 12px; font-weight: 600; margin-top: 8px; "
            "color: #6E7681; border: 1px solid #30363D; background: rgba(48, 54, 61, 0.35); }");
        return;
    }
    runStateBtn_->setEnabled(editable_);
    runStateBtn_->setText(running ? "Stop Camera" : "Start Camera");
    if (running) {
        runStateBtn_->setStyleSheet(
            "QPushButton { border-radius: 6px; padding: 7px 10px; font-size: 12px; font-weight: 600; margin-top: 8px; "
            "color: #2EA043; border: 1px solid #2EA043; background: rgba(46, 160, 67, 0.12); }"
            "QPushButton:hover { background: rgba(46, 160, 67, 0.22); border-color: #3FBF5F; }"
            "QPushButton:disabled { color: #6E7681; border: 1px solid #30363D; }");
    } else {
        runStateBtn_->setStyleSheet(
            "QPushButton { border-radius: 6px; padding: 7px 10px; font-size: 12px; font-weight: 600; margin-top: 8px; "
            "color: #E0A800; border: 1px solid #E0A800; background: transparent; }"
            "QPushButton:hover { background: rgba(224, 168, 0, 0.10); border-color: #F0B800; }"
            "QPushButton:disabled { color: #6E7681; border: 1px solid #30363D; }");
    }
}

bool CameraDeviceSettingsDialog::validateInputs(QStringList* errors) const {
    QStringList localErrors;
    if (currentInfo_.width <= 0) {
        localErrors << "Width must be greater than 0.";
    }
    if (currentInfo_.height <= 0) {
        localErrors << "Height must be greater than 0.";
    }
    if (currentInfo_.fps < 0.0) {
        localErrors << "Acquisition framerate cannot be negative.";
    }
    if (currentInfo_.exposureTimeAbs < 0.0) {
        localErrors << "Exposure Time (Abs) cannot be negative.";
    }

    if (errors) {
        *errors = localErrors;
    }

    if (!localErrors.isEmpty()) {
        QMessageBox::warning(const_cast<CameraDeviceSettingsDialog*>(this), "Invalid Device Settings", localErrors.join("\n"));
        return false;
    }
    return true;
}

bool CameraDeviceSettingsDialog::hasStopRequiredChanges() const {
    return currentInfo_.pixelFormat != originalInfo_.pixelFormat
        || currentInfo_.width != originalInfo_.width
        || currentInfo_.height != originalInfo_.height
        || currentInfo_.offsetX != originalInfo_.offsetX
        || currentInfo_.offsetY != originalInfo_.offsetY
        || currentInfo_.chunkModeActive != originalInfo_.chunkModeActive
        || currentInfo_.enabledChunks != originalInfo_.enabledChunks;
}

QStringList CameraDeviceSettingsDialog::selectedChunks() const {
    QStringList chunks;
    for (int i = 0; i < chunkListWidget_->count(); ++i) {
        QListWidgetItem* item = chunkListWidget_->item(i);
        if (item && item->checkState() == Qt::Checked) {
            chunks << item->text();
        }
    }
    return chunks;
}

QStringList CameraDeviceSettingsDialog::availableChunkOptions() const {
    return {
        "Image",
        "OffsetX",
        "OffsetY",
        "Width",
        "Height",
        "PixelFormat",
        "DynamicRangeMax",
        "DynamicRangeMin",
        "Timestamp",
        "Framecounter"
    };
}

void CameraDeviceSettingsDialog::onValueChanged() {
    if (populating_) {
        return;
    }

    const CameraInfo previousInfo = currentInfo_;
    currentInfo_.pixelFormat = pixelFormatCombo_->currentText();
    currentInfo_.width = widthSpin_->value();
    currentInfo_.height = heightSpin_->value();
    currentInfo_.offsetX = offsetXSpin_->value();
    currentInfo_.offsetY = offsetYSpin_->value();
    currentInfo_.exposureTimeAbs = exposureTimeAbsSpin_->value();
    currentInfo_.enableExposureTimeBase = enableExposureTimeBaseCheck_->isChecked();
    currentInfo_.exposureTimeBaseAbs = exposureTimeBaseSpin_->value();
    currentInfo_.exposureTimeRaw = exposureTimeRawSpin_->value();
    currentInfo_.enableAcquisitionFps = enableAcquisitionRateCheck_->isChecked();
    currentInfo_.fps = acquisitionRateSpin_->value();
    currentInfo_.chunkModeActive = chunkModeActiveCheck_->isChecked();
    currentInfo_.enabledChunks = selectedChunks();

    // Staged (stop-required) fields — the camera itself requires an
    // acquisition stop for format/AOI/chunk changes. Exposure and rate
    // nodes (abs, base, raw, framerate) are LIVE: scA780 accepts them
    // while grabbing, no restart needed.
    if (currentInfo_.pixelFormat != previousInfo.pixelFormat) stageField("pixelFormat");
    if (currentInfo_.width != previousInfo.width) stageField("width");
    if (currentInfo_.height != previousInfo.height) stageField("height");
    if (currentInfo_.offsetX != previousInfo.offsetX) stageField("offsetX");
    if (currentInfo_.offsetY != previousInfo.offsetY) stageField("offsetY");
    if (currentInfo_.chunkModeActive != previousInfo.chunkModeActive) stageField("chunkMode");
    if (currentInfo_.enabledChunks != previousInfo.enabledChunks) stageField("chunks");

    // Live (immediate) fields — exposure abs/base/raw + framerate, written
    // whenever the camera is reachable (runtime-attached or direct).
    const bool reachable = cameraManager_ && isCameraReachable(cameraManager_, cameraIndex_, currentInfo_);
    if (reachable) {
        cameraManager_->applyLiveExposureRate(cameraIndex_, currentInfo_);
    }

    persistSharedCameraSettings(cameraIndex_, currentInfo_);
    emit settingsApplied(currentInfo_);

    updateControlAvailability();
    refreshLiveDeviceInfo();
    updateStagedBadges();
    updateStagedCallouts();
    updateApplyStagedEnabled();
}

void CameraDeviceSettingsDialog::closeDialog() {
    if (!validateInputs(nullptr)) {
        return;
    }
    if (!stagedFields_.isEmpty()) {
        const bool connected = cameraManager_ && (cameraManager_->isCameraConnected(cameraIndex_) || cameraManager_->isCameraOpen(cameraIndex_));
        const bool running = connected && cameraManager_->isCameraRunning(cameraIndex_);
        QMessageBox box(this);
        box.setWindowTitle("Staged Changes");
        box.setIcon(QMessageBox::Warning);
        box.setText(QString("%1 change(s) are staged but not yet applied. Apply them before closing?")
                        .arg(stagedCount()));
        QPushButton* applyAndClose = box.addButton("Apply & Close", QMessageBox::AcceptRole);
        QPushButton* closeAnyways = box.addButton("Close without Applying", QMessageBox::DestructiveRole);
        box.addButton("Keep Editing", QMessageBox::RejectRole);
        if (running) {
            applyAndClose->setEnabled(false);
            applyAndClose->setToolTip("Stop the camera to apply staged changes.");
        }
        box.exec();
        if (box.clickedButton() == applyAndClose) {
            applyStagedChanges();
            if (stagedFields_.isEmpty()) {
                accept();
            }
            return;
        }
        if (box.clickedButton() == closeAnyways) {
            clearStaged();
            accept();
            return;
        }
        return;  // Keep Editing
    }
    accept();
}

void CameraDeviceSettingsDialog::toggleCameraRunState() {
    if (!cameraManager_ || !isCameraReachable(cameraManager_, cameraIndex_, currentInfo_)) {
        return;
    }

    if (!cameraManager_->isCameraConnected(cameraIndex_) && !cameraManager_->isCameraOpen(cameraIndex_)) {
        QMessageBox::information(this, "Camera Control", "This camera is visible on the network but is not attached to the live acquisition runtime. Save the camera configuration and restart cameras first.");
        return;
    }

    bool ok = false;
    if (cameraManager_->isCameraRunning(cameraIndex_)) {
        ok = cameraManager_->stopCamera(cameraIndex_);
    } else {
        ok = cameraManager_->startCamera(cameraIndex_, currentInfo_);
    }

    if (!ok) {
        QMessageBox::warning(this, "Camera Control", "Failed to change the selected camera run state.");
    }

    refreshLiveDeviceInfo();
    updateControlAvailability();
}

void CameraDeviceSettingsDialog::updateControlAvailability() {
    const bool reachable = isCameraReachable(cameraManager_, cameraIndex_, currentInfo_);
    const bool configuredReal = hasConfiguredRealDevice(currentInfo_);
    const bool baseEnabled = editable_ && (reachable || configuredReal);

    // Staged fields: editable whenever the camera is editable (even while running)
    pixelFormatCombo_->setEnabled(baseEnabled);
    widthSpin_->setEnabled(baseEnabled);
    heightSpin_->setEnabled(baseEnabled);
    offsetXSpin_->setEnabled(baseEnabled);
    offsetYSpin_->setEnabled(baseEnabled);
    enableExposureTimeBaseCheck_->setEnabled(baseEnabled);
    exposureTimeBaseSpin_->setEnabled(baseEnabled && currentInfo_.enableExposureTimeBase);
    exposureTimeRawSpin_->setEnabled(baseEnabled);
    chunkModeActiveCheck_->setEnabled(baseEnabled);
    chunkListWidget_->setEnabled(baseEnabled && currentInfo_.chunkModeActive);

    // Live fields
    exposureTimeAbsSpin_->setEnabled(baseEnabled);
    enableAcquisitionRateCheck_->setEnabled(baseEnabled);
    acquisitionRateSpin_->setEnabled(baseEnabled && currentInfo_.enableAcquisitionFps);

    updateApplyStagedEnabled();
}


