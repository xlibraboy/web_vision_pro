#pragma once

#include <QWidget>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QPushButton>
#include <QToolButton>
#include <QCheckBox>
#include <QFrame>
#include <QFormLayout>
#include <QGroupBox>
#include <QPainter>
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
    // Refresh the device-identity rows that can change when a camera
    // attaches/detaches (Model / IP / Image Size). Temperature has its own
    // updateTemperature().
    void setDeviceInfo(const QString& model, const QString& ip, const QString& imageSize);
    // Seed the AOI overlay panel with the camera's geometry limits and current
    // AOI (width/height/offsetX/offsetY). maxW/maxH clamp the spin ranges.
    void setAoiInfo(int maxW, int maxH, int width, int height, int offsetX, int offsetY);
    // Seed the software detection ROI (analysis region). Normalized delivered-
    // frame polygon; empty = no region (analysis paused). width/height are the
    // delivered frame dims the region is drawn against (for the overlay).
    void setDetectionRoi(const QVector<QPointF>& roi, bool maskCurves, bool maskHits);
    void updateTheme();

signals:    void backRequested();
    void analysisRequested();
    void parameterChanged(int cameraId, QString param, double value);
    void snapshotRequested(int cameraId); // Signal for CameraManager
    void saveParametersRequested(int cameraId);
    void loadParametersRequested(int cameraId);
    // AOI overlay: user changed the region of interest in the live view.
    void aoiValuesChanged(int cameraId, int width, int height, int offsetX, int offsetY);
    // Software detection ROI changed (drawn / cleared / whole-frame / scope
    // toggles). roi may be empty (= analysis paused for this camera).
    void detectionRoiChanged(int cameraId, const QVector<QPointF>& roi,
                             bool maskCurves, bool maskHits);

public:
    CameraWidget* videoWidget();

private slots:
    void onSaveParams();
    void onLoadParams();
    void onBackClicked();
    void onAnalysisClicked();
    void onSnapshotClicked(); // New slot
    void onAoiChipToggled(bool checked);
    void onAoiApplyClicked();
    void onAoiDrawToggled(bool checked);
    void onAoiRegionDrawn(int width, int height, int offsetX, int offsetY);
    void emitAoiValues();
    void onRoiDrawToggled(bool checked);
    void onRoiWholeFrameClicked();
    void onRoiClearClicked();
    void onRoiScopeChanged();
    void onRoiMaskDrawn(const QVector<QPointF>& roi);
    void emitRoiState();

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

    // AOI overlay (chip + floating panel on the video frame)
    class AoiRegionOverlay;
    AoiRegionOverlay* aoiOverlay_ = nullptr;   // sensor-context frame indicator
    QToolButton* aoiChip_ = nullptr;
    QFrame* aoiPanel_ = nullptr;
    QSpinBox* aoiWidthSpin_ = nullptr;
    QSpinBox* aoiHeightSpin_ = nullptr;
    QSpinBox* aoiOffsetXSpin_ = nullptr;
    QSpinBox* aoiOffsetYSpin_ = nullptr;
    QPushButton* aoiApplyBtn_ = nullptr;
    QToolButton* aoiDrawBtn_ = nullptr;
    int aoiMaxW_ = 1;
    int aoiMaxH_ = 1;
    bool populatingAoi_ = false;
    bool aoiPending_ = false;          // values differ from what the camera has
    int appliedAoiW_ = 0;              // last values applied to the camera
    int appliedAoiH_ = 0;
    int appliedAoiOX_ = 0;
    int appliedAoiOY_ = 0;

    // Software detection ROI (analysis region) widget + controls. The polygon
    // is normalized to the delivered frame (0..1); empty = no region -> the
    // camera's analysis is paused until one is drawn or whole-frame chosen.
    class RoiRegionOverlay;
    RoiRegionOverlay* roiOverlay_ = nullptr;  // draws + edits the ROI on the frame
    QToolButton* roiChip_ = nullptr;          // "ROI" chip opening the ROI panel
    QFrame* roiPanel_ = nullptr;
    QToolButton* roiDrawBtn_ = nullptr;
    QPushButton* roiWholeBtn_ = nullptr;
    QPushButton* roiClearBtn_ = nullptr;
    QCheckBox* roiCurvesCheck_ = nullptr;
    QCheckBox* roiHitsCheck_ = nullptr;
    QLabel* roiStatusLabel_ = nullptr;
    QVector<QPointF> roiNorm_;      // normalized polygon (widget edits land here)
    bool roiMaskCurves_ = true;
    bool roiMaskHits_ = true;
    int roiFrameW_ = 0;             // delivered frame size for the region
    int roiFrameH_ = 0;
    bool roiHasContent_ = false;    // a delivered image is currently displayed

    void buildAoiOverlay();
    void buildRoiControls();
    void repositionAoiOverlay();
    void repositionRoiPanel();
    void refreshRoiOverlay();
    void updateRoiStatus();
    void beginRoiDraw();
    void endRoiDraw();
    // Keep Offset X/Y within "SensorMax - current size" so Offset + Size never
    // exceeds the sensor (the camera would silently clamp the offset to 0).
    void updateAoiOffsetLimits();
    // Push the current AOI values into the sensor-context overlay and repaint.
    void refreshAoiOverlay();
    // Enable/disable + restyle the Apply button based on pending changes.
    void updateAoiApplyState();
    QString aoiOverlayStyle(const QString& bgColor) const;

    // FPS mismatch highlight: configured vs camera-reported rate.
    double acquisitionFps_ = -1.0;
    double displayFps_ = -1.0;
    QString fpsMismatchStyle(double fps);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
};
