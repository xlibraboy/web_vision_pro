#include "CameraDeviceSettingsDialog.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "IconManager.h"
#include "../../core/CameraManager.h"

namespace {
QString formatReadOnlyValue(const QString& value) {
    return value.trimmed().isEmpty() ? QString("Not available") : value;
}

QStringList defaultPixelFormats() {
    return {"Mono8", "Mono12", "Mono16"};
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
    populateUi();
    refreshLiveDeviceInfo();
    updateImpactBanner();
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
    resize(900, 700);

    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setSpacing(14);
    rootLayout->setContentsMargins(16, 16, 16, 16);

    QLabel* titleLabel = new QLabel(QString("Camera %1 - Device Settings").arg(originalInfo_.id), this);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: 600; color: #E3E3E3;");
    rootLayout->addWidget(titleLabel);

    subtitleLabel_ = new QLabel(this);
    subtitleLabel_->setStyleSheet("font-size: 12px; color: #8B949E;");
    subtitleLabel_->setWordWrap(true);
    rootLayout->addWidget(subtitleLabel_);

    QFrame* impactFrame = new QFrame(this);
    impactFrame->setStyleSheet("QFrame { background-color: #1C2128; border: 1px solid #30363D; border-radius: 8px; }");
    QVBoxLayout* impactLayout = new QVBoxLayout(impactFrame);
    impactLayout->setContentsMargins(12, 10, 12, 10);
    impactLayout->setSpacing(6);
    impactLabel_ = new QLabel(this);
    impactLabel_->setWordWrap(true);
    impactLabel_->setStyleSheet("font-size: 12px; color: #E3E3E3;");
    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    statusLabel_->setStyleSheet("font-size: 11px; color: #8B949E;");
    impactLayout->addWidget(impactLabel_);
    impactLayout->addWidget(statusLabel_);
    rootLayout->addWidget(impactFrame);

    QTabWidget* tabs = new QTabWidget(this);
    tabs->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #30363D; border-radius: 8px; background-color: #24292E; }"
        "QTabBar::tab { background: #1C2128; color: #8B949E; padding: 8px 14px; margin-right: 4px; border-top-left-radius: 6px; border-top-right-radius: 6px; }"
        "QTabBar::tab:selected { background: #24292E; color: #00E5FF; }"
    );
    rootLayout->addWidget(tabs, 1);

    QWidget* imageTab = new QWidget(this);
    QVBoxLayout* imageLayout = new QVBoxLayout(imageTab);
    imageLayout->setContentsMargins(14, 14, 14, 14);
    imageLayout->setSpacing(14);

    QGroupBox* imageFormatGroup = new QGroupBox("Image Format Controls", imageTab);
    imageFormatGroup->setStyleSheet("QGroupBox { font-weight: 600; color: #00E5FF; border: 1px solid #30363D; border-radius: 8px; margin-top: 6px; padding-top: 8px; font-size: 12px; } QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }");
    QFormLayout* imageFormatLayout = new QFormLayout(imageFormatGroup);
    imageFormatLayout->setContentsMargins(14, 16, 14, 14);
    imageFormatLayout->setHorizontalSpacing(14);
    imageFormatLayout->setVerticalSpacing(10);
    pixelFormatCombo_ = new QComboBox(imageFormatGroup);
    pixelFormatCombo_->setEditable(false);
    pixelFormatCombo_->setStyleSheet("QComboBox { background-color: #1C2128; border: 1px solid #30363D; border-radius: 6px; padding: 6px 8px; color: #E3E3E3; font-size: 12px; }");
    addFormRow(imageFormatLayout, "Pixel Format:", pixelFormatCombo_);
    imageLayout->addWidget(imageFormatGroup);

    QGroupBox* roiGroup = new QGroupBox("AOI Controls", imageTab);
    roiGroup->setStyleSheet(imageFormatGroup->styleSheet());
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
    const QString spinStyle = "QSpinBox { background-color: #1C2128; border: 1px solid #30363D; border-radius: 6px; padding: 6px 8px; color: #E3E3E3; font-size: 12px; }";
    widthSpin_->setStyleSheet(spinStyle);
    heightSpin_->setStyleSheet(spinStyle);
    offsetXSpin_->setStyleSheet(spinStyle);
    offsetYSpin_->setStyleSheet(spinStyle);
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
    imageLayout->addWidget(roiGroup);
    imageLayout->addStretch();
    tabs->addTab(imageTab, "Image && ROI");

    QWidget* exposureTab = new QWidget(this);
    QVBoxLayout* exposureLayout = new QVBoxLayout(exposureTab);
    exposureLayout->setContentsMargins(14, 14, 14, 14);
    exposureLayout->setSpacing(14);
    QGroupBox* exposureGroup = new QGroupBox("Acquisition Controls", exposureTab);
    exposureGroup->setStyleSheet(imageFormatGroup->styleSheet());
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
    const QString doubleSpinStyle = "QDoubleSpinBox { background-color: #1C2128; border: 1px solid #30363D; border-radius: 6px; padding: 6px 8px; color: #E3E3E3; font-size: 12px; }";
    exposureTimeAbsSpin_->setStyleSheet(doubleSpinStyle);
    exposureTimeBaseSpin_->setStyleSheet(doubleSpinStyle);
    acquisitionRateSpin_->setStyleSheet(doubleSpinStyle);
    exposureTimeRawSpin_->setStyleSheet(spinStyle);
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
    exposureLayout->addWidget(exposureGroup);
    exposureLayout->addStretch();
    tabs->addTab(exposureTab, "Exposure && Rate");

    QWidget* chunkTab = new QWidget(this);
    QVBoxLayout* chunkLayout = new QVBoxLayout(chunkTab);
    chunkLayout->setContentsMargins(14, 14, 14, 14);
    chunkLayout->setSpacing(14);
    QGroupBox* chunkGroup = new QGroupBox("Chunk Data Streams", chunkTab);
    chunkGroup->setStyleSheet(imageFormatGroup->styleSheet());
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
    chunkListWidget_->setStyleSheet("QListWidget { background-color: #1C2128; border: 1px solid #30363D; border-radius: 6px; color: #E3E3E3; } QListWidget::item { padding: 6px; }");
    chunkGroupLayout->addWidget(chunkListWidget_);
    chunkLayout->addWidget(chunkGroup);
    chunkLayout->addStretch();
    tabs->addTab(chunkTab, "Chunk Data");

    QWidget* deviceInfoTab = new QWidget(this);
    QVBoxLayout* deviceInfoLayout = new QVBoxLayout(deviceInfoTab);
    deviceInfoLayout->setContentsMargins(14, 14, 14, 14);
    deviceInfoLayout->setSpacing(14);
    QGroupBox* deviceInfoGroup = new QGroupBox("Device Information", deviceInfoTab);
    deviceInfoGroup->setStyleSheet(imageFormatGroup->styleSheet());
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
    deviceInfoLayout->addWidget(deviceInfoGroup);
    deviceInfoLayout->addStretch();
    tabs->addTab(deviceInfoTab, "Device Info");

    QWidget* serviceTab = new QWidget(this);
    QVBoxLayout* serviceLayout = new QVBoxLayout(serviceTab);
    serviceLayout->setContentsMargins(14, 14, 14, 14);
    serviceLayout->setSpacing(14);
    QGroupBox* serviceGroup = new QGroupBox("Service", serviceTab);
    serviceGroup->setStyleSheet(imageFormatGroup->styleSheet());
    QVBoxLayout* serviceGroupLayout = new QVBoxLayout(serviceGroup);
    serviceGroupLayout->setContentsMargins(14, 16, 14, 14);
    serviceGroupLayout->setSpacing(10);
    QLabel* serviceNote = new QLabel("Reset Device will immediately restart the camera and interrupt acquisition.", serviceGroup);
    serviceNote->setWordWrap(true);
    serviceNote->setStyleSheet("color: #8B949E; font-size: 12px;");
    serviceGroupLayout->addWidget(serviceNote);
    resetDeviceBtn_ = new QPushButton("Reset Device...", serviceGroup);
    resetDeviceBtn_->setIcon(IconManager::instance().warning(16));
    resetDeviceBtn_->setEnabled(false);
    resetDeviceBtn_->setStyleSheet("QPushButton { background-color: transparent; color: #FF5A5A; border: 1px solid #FF5A5A; border-radius: 6px; padding: 8px 12px; font-size: 12px; font-weight: 600; } QPushButton:hover { background-color: rgba(255, 90, 90, 0.1); }");
    serviceGroupLayout->addWidget(resetDeviceBtn_, 0, Qt::AlignLeft);
    cameraRunStateBtn_ = new QPushButton("Stop Camera for Editing", serviceGroup);
    cameraRunStateBtn_->setStyleSheet("QPushButton { background-color: #1C2128; color: #E3E3E3; border: 1px solid #30363D; border-radius: 6px; padding: 8px 12px; font-size: 12px; font-weight: 600; } QPushButton:hover { border-color: #00E5FF; }");
    serviceGroupLayout->addWidget(cameraRunStateBtn_, 0, Qt::AlignLeft);
    serviceLayout->addWidget(serviceGroup);
    serviceLayout->addStretch();
    tabs->addTab(serviceTab, "Service");

    QHBoxLayout* footerLayout = new QHBoxLayout();
    footerLayout->setSpacing(10);
    footerLayout->addStretch();
    cancelBtn_ = new QPushButton("Cancel", this);
    cancelBtn_->setIcon(IconManager::instance().close(16));
    applyBtn_ = new QPushButton("Close", this);
    applyBtn_->setIcon(IconManager::instance().close(16));
    applyCloseBtn_ = new QPushButton("Close", this);
    applyCloseBtn_->setIcon(IconManager::instance().close(16));
    footerLayout->addWidget(cancelBtn_);
    footerLayout->addWidget(applyBtn_);
    footerLayout->addWidget(applyCloseBtn_);
    rootLayout->addLayout(footerLayout);

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
    connect(cancelBtn_, &QPushButton::clicked, this, &QDialog::reject);
    connect(applyCloseBtn_, &QPushButton::clicked, this, &CameraDeviceSettingsDialog::closeDialog);
    connect(applyBtn_, &QPushButton::clicked, this, &CameraDeviceSettingsDialog::closeDialog);
    connect(cameraRunStateBtn_, &QPushButton::clicked, this, &CameraDeviceSettingsDialog::toggleCameraRunState);
}

void CameraDeviceSettingsDialog::populateUi() {
    populating_ = true;
    subtitleLabel_->setText(QString("%1 | %2 | %3 mm").arg(originalInfo_.name).arg(originalInfo_.location).arg(originalInfo_.machinePosition));

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
    sensorWidthValueLabel_->setText(QString::number(std::max(0, originalInfo_.width)));
    sensorHeightValueLabel_->setText(QString::number(std::max(0, originalInfo_.height)));
    maxWidthValueLabel_->setText(QString::number(std::max(0, originalInfo_.width)));
    maxHeightValueLabel_->setText(QString::number(std::max(0, originalInfo_.height)));
    modelValueLabel_->setText(formatReadOnlyValue(originalInfo_.model));
    ipValueLabel_->setText(formatReadOnlyValue(originalInfo_.ipAddress));
    resultingRateValueLabel_->setText(QString::number(currentInfo_.fps, 'f', 3) + " Hz");

    if (!cameraManager_ || !cameraManager_->isCameraConnected(cameraIndex_)) {
        statusLabel_->setText("Camera is offline. Only general config values can be edited here.");
        return;
    }

    statusLabel_->setText(cameraManager_->isCameraRunning(cameraIndex_)
        ? "Camera is running. Stop this camera to edit format, ROI, chunk, and stop-required acquisition parameters."
        : "Camera is stopped. All device parameters can be changed and are applied immediately.");
    modelValueLabel_->setText(formatReadOnlyValue(QString::fromStdString(cameraManager_->getModelName(cameraIndex_))));
    ipValueLabel_->setText(formatReadOnlyValue(QString::fromStdString(cameraManager_->getIpAddress(cameraIndex_))));

    CameraManager::CameraParams params = cameraManager_->getCameraParams(cameraIndex_);
    if (params.width > 0) {
        sensorWidthValueLabel_->setText(QString::number(params.width));
        maxWidthValueLabel_->setText(QString::number(params.width));
    }
    if (params.height > 0) {
        sensorHeightValueLabel_->setText(QString::number(params.height));
        maxHeightValueLabel_->setText(QString::number(params.height));
    }
    if (params.fps > 0.0) {
        resultingRateValueLabel_->setText(QString::number(params.fps, 'f', 3) + " Hz");
    }
}

void CameraDeviceSettingsDialog::updateImpactBanner() {
    const bool hasChanges = currentInfo_.pixelFormat != originalInfo_.pixelFormat
        || currentInfo_.width != originalInfo_.width
        || currentInfo_.height != originalInfo_.height
        || currentInfo_.offsetX != originalInfo_.offsetX
        || currentInfo_.offsetY != originalInfo_.offsetY
        || currentInfo_.exposureTimeAbs != originalInfo_.exposureTimeAbs
        || currentInfo_.enableExposureTimeBase != originalInfo_.enableExposureTimeBase
        || currentInfo_.exposureTimeBaseAbs != originalInfo_.exposureTimeBaseAbs
        || currentInfo_.exposureTimeRaw != originalInfo_.exposureTimeRaw
        || currentInfo_.enableAcquisitionFps != originalInfo_.enableAcquisitionFps
        || currentInfo_.fps != originalInfo_.fps
        || currentInfo_.chunkModeActive != originalInfo_.chunkModeActive
        || currentInfo_.enabledChunks != originalInfo_.enabledChunks;

    if (!hasChanges) {
        impactLabel_->setText("Device settings are in sync.");
        return;
    }

    impactLabel_->setText(hasStopRequiredChanges()
        ? "Some edited parameters require this camera to be stopped before they can be applied."
        : "Changes are applied immediately to the selected camera.");
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
        || currentInfo_.enableExposureTimeBase != originalInfo_.enableExposureTimeBase
        || currentInfo_.exposureTimeBaseAbs != originalInfo_.exposureTimeBaseAbs
        || currentInfo_.exposureTimeRaw != originalInfo_.exposureTimeRaw
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

    const bool includesStopRequiredChanges = currentInfo_.pixelFormat != previousInfo.pixelFormat
        || currentInfo_.width != previousInfo.width
        || currentInfo_.height != previousInfo.height
        || currentInfo_.offsetX != previousInfo.offsetX
        || currentInfo_.offsetY != previousInfo.offsetY
        || currentInfo_.enableExposureTimeBase != previousInfo.enableExposureTimeBase
        || currentInfo_.exposureTimeBaseAbs != previousInfo.exposureTimeBaseAbs
        || currentInfo_.exposureTimeRaw != previousInfo.exposureTimeRaw
        || currentInfo_.chunkModeActive != previousInfo.chunkModeActive
        || currentInfo_.enabledChunks != previousInfo.enabledChunks;

    applyImmediateChanges(includesStopRequiredChanges);
    updateControlAvailability();
    refreshLiveDeviceInfo();
    updateImpactBanner();
}

void CameraDeviceSettingsDialog::closeDialog() {
    if (!validateInputs(nullptr)) {
        return;
    }

    accept();
}

void CameraDeviceSettingsDialog::toggleCameraRunState() {
    if (!cameraManager_ || !cameraManager_->isCameraConnected(cameraIndex_)) {
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
    updateImpactBanner();
}

void CameraDeviceSettingsDialog::updateControlAvailability() {
    const bool connected = cameraManager_ && cameraManager_->isCameraConnected(cameraIndex_);
    const bool running = connected && cameraManager_->isCameraRunning(cameraIndex_);
    const bool baseEnabled = editable_ && connected;
    const bool stopRequiredEditable = baseEnabled && !running;

    pixelFormatCombo_->setEnabled(stopRequiredEditable);
    widthSpin_->setEnabled(stopRequiredEditable);
    heightSpin_->setEnabled(stopRequiredEditable);
    offsetXSpin_->setEnabled(stopRequiredEditable);
    offsetYSpin_->setEnabled(stopRequiredEditable);
    enableExposureTimeBaseCheck_->setEnabled(stopRequiredEditable);
    exposureTimeBaseSpin_->setEnabled(stopRequiredEditable && currentInfo_.enableExposureTimeBase);
    exposureTimeRawSpin_->setEnabled(stopRequiredEditable);
    chunkModeActiveCheck_->setEnabled(stopRequiredEditable);
    chunkListWidget_->setEnabled(stopRequiredEditable && currentInfo_.chunkModeActive);

    exposureTimeAbsSpin_->setEnabled(baseEnabled);
    enableAcquisitionRateCheck_->setEnabled(baseEnabled);
    acquisitionRateSpin_->setEnabled(baseEnabled && currentInfo_.enableAcquisitionFps);

    applyBtn_->setEnabled(true);
    applyCloseBtn_->setVisible(false);
    cancelBtn_->setText("Cancel");

    if (cameraRunStateBtn_) {
        cameraRunStateBtn_->setEnabled(baseEnabled);
        cameraRunStateBtn_->setText(running ? "Stop Camera for Editing" : "Start Camera");
    }
}

void CameraDeviceSettingsDialog::applyImmediateChanges(bool includesStopRequiredChanges) {
    if (!cameraManager_ || !cameraManager_->isCameraConnected(cameraIndex_)) {
        return;
    }
    if (!validateInputs(nullptr)) {
        return;
    }

    const bool running = cameraManager_->isCameraRunning(cameraIndex_);
    if (includesStopRequiredChanges && running) {
        return;
    }

    if (includesStopRequiredChanges) {
        cameraManager_->applyCameraDeviceSettings(cameraIndex_, currentInfo_);
    } else {
        cameraManager_->setCameraExposure(cameraIndex_, currentInfo_.exposureTimeAbs);
        cameraManager_->setCameraFrameRate(cameraIndex_, currentInfo_.fps, currentInfo_.enableAcquisitionFps);
    }
}
