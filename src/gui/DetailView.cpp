#include "DetailView.h"
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
    lblTemp_ = new QLabel("-", this);

    infoLayout->addRow("ID:", lblId_);
    infoLayout->addRow("Location:", lblLocation_);
    infoLayout->addRow("Side:", lblSide_);
    infoLayout->addRow("Description:", lblDescription_);
    infoLayout->addRow("Model:", lblModel_);
    infoLayout->addRow("IP Address:", lblIP_);
    infoLayout->addRow("Image Size:", lblImageSize_);
    infoLayout->addRow("Frequency:", lblFPS_);
    infoLayout->addRow("Temperature °C:", lblTemp_);

    // Parameters Group (Below Camera Info)
    QGroupBox* controlGroup = new QGroupBox("Camera Parameters", this);
    QGridLayout* controlLayout = new QGridLayout(controlGroup);

    // Gain (row 0)
    QLabel* lblGain = new QLabel("Gain:", this);
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
            [this](int val) { spinGain_->setValue(val / 10.0); });

    // Exposure Time (row 1)
    QLabel* lblExposure = new QLabel("Exposure Time [µs]:", this);
    spinExposure_ = new QSpinBox(this);
    spinExposure_->setRange(100, 100000);  // Reduced max for slider usability
    spinExposure_->setSuffix(" µs");
    
    sliderExposure_ = new QSlider(Qt::Horizontal, this);
    sliderExposure_->setRange(100, 100000);
    sliderExposure_->setValue(5000);
    
    connect(spinExposure_, QOverload<int>::of(&QSpinBox::valueChanged), 
            sliderExposure_, &QSlider::setValue);
    connect(sliderExposure_, &QSlider::valueChanged, 
            spinExposure_, &QSpinBox::setValue);

    // Gamma (row 2)
    QLabel* lblGamma = new QLabel("Gamma:", this);
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
            [this](int val) { spinGamma_->setValue(val / 100.0); });

    // Contrast (row 3)
    QLabel* lblContrast = new QLabel("Contrast:", this);
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
            [this](int val) { spinContrast_->setValue(val / 100.0); });

    // Apply button (row 4)
    btnSave_ = new QPushButton("Apply Parameters", this);
    connect(btnSave_, &QPushButton::clicked, this, &DetailView::onSaveParams);

    // Layout: Label | SpinBox (with sliders below each parameter)
    // Gain (rows 0-1)
    controlLayout->addWidget(lblGain, 0, 0);
    controlLayout->addWidget(spinGain_, 0, 1);
    controlLayout->addWidget(sliderGain_, 1, 0, 1, 2);  // Span both columns
    
    // Exposure (rows 2-3)
    controlLayout->addWidget(lblExposure, 2, 0);
    controlLayout->addWidget(spinExposure_, 2, 1);
    controlLayout->addWidget(sliderExposure_, 3, 0, 1, 2);  // Span both columns
    
    // Gamma (rows 4-5)
    controlLayout->addWidget(lblGamma, 4, 0);
    controlLayout->addWidget(spinGamma_, 4, 1);
    controlLayout->addWidget(sliderGamma_, 5, 0, 1, 2);  // Span both columns
    
    // Contrast (rows 6-7)
    controlLayout->addWidget(lblContrast, 6, 0);
    controlLayout->addWidget(spinContrast_, 6, 1);
    controlLayout->addWidget(sliderContrast_, 7, 0, 1, 2);  // Span both columns
    
    // Apply button (row 8)
    controlLayout->addWidget(btnSave_, 8, 0, 1, 2);  // Span both columns
    
    // "Go to Analysis View" button (row 9)
    btnAnalysis_ = new QPushButton("Go to Analysis View", this);
    btnAnalysis_->setStyleSheet("background-color: #007ACC; color: white; font-weight: bold; padding: 5px;");
    connect(btnAnalysis_, &QPushButton::clicked, this, &DetailView::onAnalysisClicked);
    controlLayout->addWidget(btnAnalysis_, 9, 0, 1, 2);

    // Snapshot Button (row 10)
    btnSnapshot_ = new QPushButton("Take Snapshot", this);
    btnSnapshot_->setStyleSheet("background-color: #2E7D32; color: white; font-weight: bold; padding: 5px;");
    connect(btnSnapshot_, &QPushButton::clicked, this, &DetailView::onSnapshotClicked);
    controlLayout->addWidget(btnSnapshot_, 10, 0, 1, 2);

    leftPanelLayout->addWidget(infoGroup);
    leftPanelLayout->addWidget(controlGroup);
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
    lblId_->setText(QString("%1").arg(info.id, 2, 10, QChar('0')));  // Format as 01, 02, etc.
    lblLocation_->setText(info.location);
    lblSide_->setText(info.side);
    lblDescription_->setText(info.description);
    lblModel_->setText(info.model);
    lblIP_->setText(info.ipAddress);
    lblImageSize_->setText(info.imageSize);
    lblFPS_->setText(QString("%1 FPS").arg(info.fps, 0, 'f', 1));
    lblTemp_->setText(QString("%1").arg(info.temperature, 0, 'f', 1));
    
    // Set default parameter values (would come from Pylon in real implementation)
    // TODO: Get actual values from CameraManager/Pylon SDK
    spinGain_->setValue(1.0);
    spinExposure_->setValue(5000);
    spinGamma_->setValue(1.0);
    spinContrast_->setValue(1.0);
}

void DetailView::setAdminMode(bool isAdmin) {
    isAdmin_ = isAdmin;
    
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
}

void DetailView::onSaveParams() {
    qDebug() << "Saving params for camera " << currentCameraId_;
    // TODO: Emit signal to CameraManager to set params
    // emit parameterChanged(currentCameraId_, "Gain", spinGain_->value());
    // emit parameterChanged(currentCameraId_, "Exposure", spinExposure_->value());
    // emit parameterChanged(currentCameraId_, "Gamma", spinGamma_->value());
    // emit parameterChanged(currentCameraId_, "Contrast", spinContrast_->value());
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
