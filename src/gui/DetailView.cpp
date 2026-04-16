#include "DetailView.h"
#include "../config/CameraConfig.h"
#include "../core/TemperatureStatus.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QDebug>

DetailView::DetailView(QWidget *parent) : QWidget(parent), isAdmin_(false) {
    setupUi();
}

DetailView::~DetailView() {}

void DetailView::setupUi() {
    QHBoxLayout* mainLayout = new QHBoxLayout(this);

    // --- Left Panel: Info + Parameters (Vertical Stack) ---
    QVBoxLayout* leftPanelLayout = new QVBoxLayout();

    // Camera Info Group
    QGroupBox* infoGroup = new QGroupBox("Camera Information", this);
    QFormLayout* infoLayout = new QFormLayout(infoGroup);
    
    lblId_ = new QLabel("-", this);
    lblLocation_ = new QLabel("-", this);
    lblSide_ = new QLabel("-", this);
    lblDescription_ = new QLabel("-", this);
    lblModel_ = new QLabel("-", this);
    lblIP_ = new QLabel("-", this);
    lblImageSize_ = new QLabel("-", this);
    lblFPS_ = new QLabel("-", this);
    lblDisplayFps_ = new QLabel("-", this);
    lblTemp_ = new QLabel("-", this);

    infoLayout->addRow("ID:", lblId_);
    infoLayout->addRow("Location:", lblLocation_);
    infoLayout->addRow("Side:", lblSide_);
    infoLayout->addRow("Description:", lblDescription_);
    infoLayout->addRow("Model:", lblModel_);
    infoLayout->addRow("IP Address:", lblIP_);
    infoLayout->addRow("Image Size:", lblImageSize_);
    infoLayout->addRow("Acquisition FPS:", lblFPS_);
    infoLayout->addRow("Resulting Framerate (Abs) [Hz]:", lblDisplayFps_);
    infoLayout->addRow("Temperature °C:", lblTemp_);

    // Parameters Group (Below Camera Info)
    controlGroup_ = new QGroupBox("Camera Parameters", this);
    QVBoxLayout* controlLayout = new QVBoxLayout(controlGroup_);

    // Gain Group
    QGroupBox* gainGroup = new QGroupBox("Gain", controlGroup_);
    QVBoxLayout* gainLayout = new QVBoxLayout(gainGroup);
    spinGain_ = new QDoubleSpinBox(this);
    spinGain_->setRange(0.0, 24.0);
    spinGain_->setSingleStep(0.1);
    spinGain_->setDecimals(1);
    
    sliderGain_ = new QSlider(Qt::Horizontal, this);
    sliderGain_->setRange(0, 240);  // 0.0 to 24.0 with 0.1 precision
    sliderGain_->setValue(10);
    
    connect(spinGain_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
            [this](double val) { sliderGain_->setValue(static_cast<int>(val * 10)); });
    connect(sliderGain_, &QSlider::valueChanged, 
            [this](int val) {
                // Sync spinbox
                spinGain_->setValue(val / 10.0);
                // Live apply
                emit parameterChanged(currentCameraId_, "Gain", val / 10.0);
            });
    gainLayout->addWidget(spinGain_);
    gainLayout->addWidget(sliderGain_);
    controlLayout->addWidget(gainGroup);

    // Exposure Time Group
    QGroupBox* expGroup = new QGroupBox("Exposure Time", controlGroup_);
    QVBoxLayout* expLayout = new QVBoxLayout(expGroup);
    spinExposure_ = new QSpinBox(this);
    spinExposure_->setRange(100, 100000);  // Will be constrained dynamically
    spinExposure_->setSuffix(" µs");
    
    sliderExposure_ = new QSlider(Qt::Horizontal, this);
    sliderExposure_->setRange(100, 100000);
    sliderExposure_->setValue(5000);
    
    connect(spinExposure_, QOverload<int>::of(&QSpinBox::valueChanged), 
            sliderExposure_, &QSlider::setValue);
    connect(sliderExposure_, &QSlider::valueChanged, 
            [this](int val) {
                spinExposure_->setValue(val);
                // Live apply
                emit parameterChanged(currentCameraId_, "Exposure", val);
            });
    expLayout->addWidget(spinExposure_);
    expLayout->addWidget(sliderExposure_);
    controlLayout->addWidget(expGroup);

    // Gamma Group
    QGroupBox* gammaGroup = new QGroupBox("Gamma", controlGroup_);
    QVBoxLayout* gammaLayout = new QVBoxLayout(gammaGroup);
    spinGamma_ = new QDoubleSpinBox(this);
    spinGamma_->setRange(0.1, 4.0);
    spinGamma_->setSingleStep(0.1);
    spinGamma_->setDecimals(2);
    spinGamma_->setValue(1.0);
    
    sliderGamma_ = new QSlider(Qt::Horizontal, this);
    sliderGamma_->setRange(10, 400);  // 0.1 to 4.0 with 0.01 precision
    sliderGamma_->setValue(100);
    
    connect(spinGamma_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
            [this](double val) { sliderGamma_->setValue(static_cast<int>(val * 100)); });
    connect(sliderGamma_, &QSlider::valueChanged, 
            [this](int val) {
                spinGamma_->setValue(val / 100.0);
                // Live apply
                emit parameterChanged(currentCameraId_, "Gamma", val / 100.0);
            });
    gammaLayout->addWidget(spinGamma_);
    gammaLayout->addWidget(sliderGamma_);
    controlLayout->addWidget(gammaGroup);

    // Contrast Group
    QGroupBox* contrastGroup = new QGroupBox("Contrast", controlGroup_);
    QVBoxLayout* contrastLayout = new QVBoxLayout(contrastGroup);
    spinContrast_ = new QDoubleSpinBox(this);
    spinContrast_->setRange(0.0, 2.0);
    spinContrast_->setSingleStep(0.05);
    spinContrast_->setDecimals(2);
    spinContrast_->setValue(1.0);
    
    sliderContrast_ = new QSlider(Qt::Horizontal, this);
    sliderContrast_->setRange(0, 200);  // 0.0 to 2.0 with 0.01 precision
    sliderContrast_->setValue(100);
    
    connect(spinContrast_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
            [this](double val) { sliderContrast_->setValue(static_cast<int>(val * 100)); });
    connect(sliderContrast_, &QSlider::valueChanged, 
            [this](int val) { 
                spinContrast_->setValue(val / 100.0); 
                // Live apply
                emit parameterChanged(currentCameraId_, "Contrast", val / 100.0);
            });
    contrastLayout->addWidget(spinContrast_);
    contrastLayout->addWidget(sliderContrast_);
    controlLayout->addWidget(contrastGroup);

    // Save & Load Parameter buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLoad_ = new QPushButton("Load Parameters", this);
    btnSave_ = new QPushButton("Save Parameters", this);
    connect(btnLoad_, &QPushButton::clicked, this, &DetailView::onLoadParams);
    connect(btnSave_, &QPushButton::clicked, this, &DetailView::onSaveParams);
    
    btnLayout->addWidget(btnLoad_);
    btnLayout->addWidget(btnSave_);
    controlLayout->addLayout(btnLayout);

    leftPanelLayout->addWidget(infoGroup);
    leftPanelLayout->addWidget(controlGroup_);
    leftPanelLayout->addStretch(); // Push content to top

    // --- Center Panel: Video ---
    cameraWidget_ = new CameraWidget(this);
    cameraWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // Connect camera widget double-click to go back to grid
    connect(cameraWidget_, &CameraWidget::doubleClicked, this, &DetailView::backRequested);

    // Center layout with just the camera widget (no back button for more space)
    QVBoxLayout* centerLayout = new QVBoxLayout();
    centerLayout->addWidget(cameraWidget_);

    // Layout Assembly: 25% Left Panel, 75% Video
    mainLayout->addLayout(leftPanelLayout, 1);
    mainLayout->addLayout(centerLayout, 3); 

    setAdminMode(false); // Default state
}

void DetailView::setCamera(int cameraId, const CameraInfo& info, CameraWidget* videoSource) {
    currentCameraId_ = cameraId;
    
    // Set camera ID on our video widget so it receives the correct frames
    cameraWidget_->setCameraId(cameraId);
    
    // Update Info Panel with comprehensive camera information
    lblId_->setText(QString("%1").arg(info.id, 2, 10, QChar('0')));
    lblLocation_->setText(info.location);
    lblSide_->setText(info.side);
    lblDescription_->setText(info.name);
    lblModel_->setText(info.model);
    lblIP_->setText(info.ipAddress);
    lblImageSize_->setText(info.imageSize);
    lblFPS_->setText(QString("%1 FPS").arg(static_cast<double>(info.fps), 0, 'f', 1));
    updateTemperature(info.temperature);
    
    // Set overlay text to match LiveDashboard
    cameraWidget_->setOverlayText(CameraConfig::getCameraLabel(cameraId));
    
    // --- Clamp Exposure Time based on FPS (max exposure = 1,000,000 / fps µs) ---
    // This ensures exposure is always within a range that does not cause frame drops.
    int maxExposure = 100000;
    if (info.fps > 0) {
        maxExposure = static_cast<int>(1000000.0 / info.fps) - 1;
        if (maxExposure < 100) maxExposure = 100;
    }
    spinExposure_->blockSignals(true);
    sliderExposure_->blockSignals(true);
    spinExposure_->setRange(100, maxExposure);
    sliderExposure_->setRange(100, maxExposure);
    spinExposure_->blockSignals(false);
    sliderExposure_->blockSignals(false);
    
    // Load current parameter values from info (config-backed defaults)
    // Block ALL slider AND spinbox signals to prevent parameterChanged from firing
    // during initialization — this avoids triggering StopGrabbing/StartGrabbing
    // on the acquisition loop while the detail view is being set up.
    // NOTE: MainWindow::showDetail calls setParameterValues() right after setCamera()
    // with live values from the camera NodeMap, so we just initialize to safe defaults here.
    spinGain_->blockSignals(true);
    sliderGain_->blockSignals(true);
    spinGain_->setValue(0);
    sliderGain_->setValue(0);
    spinGain_->blockSignals(false);
    sliderGain_->blockSignals(false);

    spinExposure_->blockSignals(true);
    sliderExposure_->blockSignals(true);
    spinExposure_->setValue(100);
    sliderExposure_->setValue(100);
    spinExposure_->blockSignals(false);
    sliderExposure_->blockSignals(false);

    spinGamma_->blockSignals(true);
    sliderGamma_->blockSignals(true);
    spinGamma_->setValue(1.0);
    sliderGamma_->setValue(100);
    spinGamma_->blockSignals(false);
    sliderGamma_->blockSignals(false);

    spinContrast_->blockSignals(true);
    sliderContrast_->blockSignals(true);
    spinContrast_->setValue(1.0);
    sliderContrast_->setValue(100);
    spinContrast_->blockSignals(false);
    sliderContrast_->blockSignals(false);
}

void DetailView::clearCamera() {
    currentCameraId_ = -1;
    cameraWidget_->setCameraId(-1);
    cameraWidget_->clearFrame();

    lblId_->setText("-");
    lblLocation_->setText("-");
    lblSide_->setText("-");
    lblDescription_->setText("-");
    lblModel_->setText("-");
    lblIP_->setText("-");
    lblImageSize_->setText("-");
    lblFPS_->setText("-");
    lblDisplayFps_->setText("-");
    lblTemp_->setText("-");
    lblTemp_->setStyleSheet("");
}

void DetailView::updateTemperature(double temp) {
    if (!lblTemp_) return;

    TempStatus::Status status = TempStatus::classify(temp);

    QString text;
    QString color;

    switch (status) {
        case TempStatus::Error:
            text  = QString("⛔ %1 °C  ERROR").arg(temp, 0, 'f', 1);
            color = "#ff4444";
            break;
        case TempStatus::Critical:
            text  = QString("⚠ %1 °C  CRITICAL").arg(temp, 0, 'f', 1);
            color = "#ff9900";
            break;
        case TempStatus::Ok:
            text  = QString("%1 °C").arg(temp, 0, 'f', 1);
            color = "";  // Default theme text
            break;
        default:  // Unknown / N/A
            text  = "N/A";
            color = "#888888";
            break;
    }

    lblTemp_->setText(text);
    if (color.isEmpty()) {
        lblTemp_->setStyleSheet("");  // Revert to theme
    } else {
        lblTemp_->setStyleSheet(QString("color: %1; font-weight: bold;").arg(color));
    }
}

void DetailView::setAdminMode(bool isAdmin) {
    isAdmin_ = isAdmin;
    
    if (controlGroup_) {
        controlGroup_->setEnabled(isAdmin);
    }
    
    // Enable/disable spinboxes
    spinGain_->setEnabled(isAdmin);
    spinExposure_->setEnabled(isAdmin);
    spinGamma_->setEnabled(isAdmin);
    spinContrast_->setEnabled(isAdmin);
    
    // Enable/disable sliders
    sliderGain_->setEnabled(isAdmin);
    sliderExposure_->setEnabled(isAdmin);
    sliderGamma_->setEnabled(isAdmin);
    sliderContrast_->setEnabled(isAdmin);
    
    btnSave_->setEnabled(isAdmin);
    btnLoad_->setEnabled(isAdmin);
}

void DetailView::setParameterValues(double gain, double exposureUs, double gamma, double contrast) {
    // Block signals while we update controls so we don't emit parameterChanged back
    spinGain_->blockSignals(true);
    sliderGain_->blockSignals(true);
    spinGain_->setValue(gain);
    sliderGain_->setValue(static_cast<int>(gain * 10));
    spinGain_->blockSignals(false);
    sliderGain_->blockSignals(false);

    int expClamped = qBound(spinExposure_->minimum(), static_cast<int>(exposureUs), spinExposure_->maximum());
    spinExposure_->blockSignals(true);
    sliderExposure_->blockSignals(true);
    spinExposure_->setValue(expClamped);
    sliderExposure_->setValue(expClamped);
    spinExposure_->blockSignals(false);
    sliderExposure_->blockSignals(false);

    spinGamma_->blockSignals(true);
    sliderGamma_->blockSignals(true);
    spinGamma_->setValue(gamma);
    sliderGamma_->setValue(static_cast<int>(gamma * 100));
    spinGamma_->blockSignals(false);
    sliderGamma_->blockSignals(false);

    spinContrast_->blockSignals(true);
    sliderContrast_->blockSignals(true);
    spinContrast_->setValue(contrast);
    sliderContrast_->setValue(static_cast<int>(contrast * 100));
    spinContrast_->blockSignals(false);
    sliderContrast_->blockSignals(false);
}

void DetailView::onSaveParams() {
    qDebug() << "[DetailView] Save parameters for camera" << currentCameraId_;
    emit saveParametersRequested(currentCameraId_);
}

void DetailView::onLoadParams() {
    qDebug() << "[DetailView] Load parameters for camera" << currentCameraId_;
    emit loadParametersRequested(currentCameraId_);
}

void DetailView::onBackClicked() {
    emit backRequested();
}

// Accessor for MainWindow to connect signals
CameraWidget* DetailView::videoWidget() {
    return cameraWidget_;
}

void DetailView::onAnalysisClicked() {
    emit analysisRequested();
}

void DetailView::onSnapshotClicked() {
    emit snapshotRequested(currentCameraId_);
}

void DetailView::setDisplayFps(double fps) {
    if (fps < 0) {
        lblDisplayFps_->setText("N/A");
    } else {
        lblDisplayFps_->setText(QString::number(fps, 'f', 1));
    }
}

void DetailView::setAcquisitionFps(double fps) {
    if (fps < 0) {
        lblFPS_->setText("N/A");
    } else {
        lblFPS_->setText(QString("%1 FPS").arg(fps, 0, 'f', 1));
    }
}
