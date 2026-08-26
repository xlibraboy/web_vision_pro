#pragma once

#include <QWidget>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QPushButton>
#include <QFormLayout>
#include <QGroupBox>
#include "widgets/CameraWidget.h"
#include "CameraInfo.h"
#include "../core/CameraManager.h"

class DetailView : public QWidget {
    Q_OBJECT

public:
    explicit DetailView(QWidget *parent = nullptr);
    ~DetailView();

    void setCamera(int cameraId, const CameraInfo& info, CameraWidget* videoSource);
    void clearCamera();
    void setAdminMode(bool isAdmin);
    // Update temperature display live (from background monitor thread via MainWindow)
    void updateTemperature(double temp);
    // Update spinboxes/sliders from live camera readback (e.g. after PFS load)
    void setParameterValues(double gain, double exposureUs, double gamma, double contrast);
    void setGainPresentation(const QString& title, bool isRaw);
    void setGainRange(double minGain, double maxGain);
    void setExposureRange(int minUs, int maxUs);
    void setAcquisitionFps(double fps);
    // Update Display FPS label (called when user selects a camera)
    void setDisplayFps(double fps);
    void updateTheme();

signals:    void backRequested();
    void analysisRequested();
    void parameterChanged(int cameraId, QString param, double value);
    void snapshotRequested(int cameraId); // Signal for CameraManager
    void saveParametersRequested(int cameraId);
    void loadParametersRequested(int cameraId);

public:
    CameraWidget* videoWidget();

private slots:
    void onSaveParams();
    void onLoadParams();
    void onBackClicked();
    void onAnalysisClicked();
    void onSnapshotClicked(); // New slot

private:
    void setupUi();
    void applyLiveViewTypography();

    CameraWidget* cameraWidget_;
    QGroupBox* infoGroup_ = nullptr;
    
    // Camera Info Labels
    QLabel* lblId_;
    QLabel* lblLocation_;
    QLabel* lblSide_;
    QLabel* lblDescription_;
    QLabel* lblModel_;
    QLabel* lblIP_;
    QLabel* lblImageSize_;
    QLabel* lblFPS_;
    QLabel* lblConfiguredFramePeriod_;
    QLabel* lblDisplayFps_;
    QLabel* lblActualFramePeriod_;
    QLabel* lblTemp_;

    // Parameter Controls
    QGroupBox* controlGroup_;
    QGroupBox* gainGroup_ = nullptr;
    QDoubleSpinBox* spinGain_;
    QSlider* sliderGain_;
    
    QSpinBox* spinExposure_;
    QSlider* sliderExposure_;
    
    QDoubleSpinBox* spinGamma_;
    QSlider* sliderGamma_;
    
    QDoubleSpinBox* spinContrast_;
    QSlider* sliderContrast_;
    
    QPushButton* btnSave_;
    QPushButton* btnLoad_;
    QPushButton* btnBack_;
    QPushButton* btnAnalysis_;
    QPushButton* btnSnapshot_; // Added missing member

    bool isAdmin_;
    bool gainIsRaw_ = false;
    int currentCameraId_ = -1;

    // FPS mismatch highlight: configured vs camera-reported rate.
    double acquisitionFps_ = -1.0;
    double displayFps_ = -1.0;
    QString fpsMismatchStyle(double fps);
};
