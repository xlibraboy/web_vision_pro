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
    void setAdminMode(bool isAdmin);
    // Update temperature display live (from background monitor thread via MainWindow)
    void updateTemperature(double temp);

signals:
    void backRequested();
    void analysisRequested();
    void parameterChanged(int cameraId, QString param, double value);
    void snapshotRequested(int cameraId); // Signal for CameraManager

public:
    CameraWidget* videoWidget();

private slots:
    void onSaveParams();
    void onBackClicked();
    void onAnalysisClicked();
    void onSnapshotClicked(); // New slot

private:
    void setupUi();

    CameraWidget* cameraWidget_;
    
    // Camera Info Labels
    QLabel* lblId_;
    QLabel* lblLocation_;
    QLabel* lblSide_;
    QLabel* lblDescription_;
    QLabel* lblModel_;
    QLabel* lblIP_;
    QLabel* lblImageSize_;
    QLabel* lblFPS_;
    QLabel* lblTemp_;

    // Parameter Controls
    QGroupBox* controlGroup_;
    QDoubleSpinBox* spinGain_;
    QSlider* sliderGain_;
    
    QSpinBox* spinExposure_;
    QSlider* sliderExposure_;
    
    QDoubleSpinBox* spinGamma_;
    QSlider* sliderGamma_;
    
    QDoubleSpinBox* spinContrast_;
    QSlider* sliderContrast_;
    
    QPushButton* btnSave_;
    QPushButton* btnBack_;
    QPushButton* btnAnalysis_;
    QPushButton* btnSnapshot_; // Added missing member

    bool isAdmin_;
    int currentCameraId_ = -1;
};


