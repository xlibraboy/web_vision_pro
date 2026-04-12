#pragma once

#include <QDialog>
#include <QStringList>
#include "../CameraInfo.h"

class QLabel;
class QTabWidget;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QListWidget;
class QPushButton;

class CameraManager;

class CameraDeviceSettingsDialog : public QDialog {
    Q_OBJECT

public:
    CameraDeviceSettingsDialog(int cameraIndex, const CameraInfo& info,
                               CameraManager* cameraManager,
                               bool editable, QWidget* parent = nullptr);

    CameraInfo updatedInfo() const;
    bool requiresRestart() const;

private slots:
    void onValueChanged();
    void closeDialog();
    void toggleCameraRunState();

private:
    void setupUi();
    void populateUi();
    void refreshLiveDeviceInfo();
    void updateImpactBanner();
    void updateControlAvailability();
    void applyImmediateChanges(bool includesStopRequiredChanges);
    bool validateInputs(QStringList* errors) const;
    bool hasStopRequiredChanges() const;
    QStringList selectedChunks() const;
    QStringList availableChunkOptions() const;

    int cameraIndex_;
    CameraInfo originalInfo_;
    CameraInfo currentInfo_;
    CameraManager* cameraManager_;
    bool editable_;
    bool populating_ = false;

    QLabel* subtitleLabel_;
    QLabel* impactLabel_;
    QLabel* statusLabel_;
    QLabel* modelValueLabel_;
    QLabel* ipValueLabel_;

    QComboBox* pixelFormatCombo_;
    QSpinBox* widthSpin_;
    QSpinBox* heightSpin_;
    QSpinBox* offsetXSpin_;
    QSpinBox* offsetYSpin_;
    QLabel* sensorWidthValueLabel_;
    QLabel* sensorHeightValueLabel_;
    QLabel* maxWidthValueLabel_;
    QLabel* maxHeightValueLabel_;

    QDoubleSpinBox* exposureTimeAbsSpin_;
    QCheckBox* enableExposureTimeBaseCheck_;
    QDoubleSpinBox* exposureTimeBaseSpin_;
    QSpinBox* exposureTimeRawSpin_;
    QCheckBox* enableAcquisitionRateCheck_;
    QDoubleSpinBox* acquisitionRateSpin_;
    QLabel* resultingRateValueLabel_;

    QCheckBox* chunkModeActiveCheck_;
    QListWidget* chunkListWidget_;

    QLabel* vendorValueLabel_;
    QLabel* modelInfoValueLabel_;
    QLabel* manufacturerInfoValueLabel_;
    QLabel* deviceVersionValueLabel_;
    QLabel* firmwareVersionValueLabel_;
    QLabel* deviceIdValueLabel_;

    QPushButton* applyBtn_;
    QPushButton* applyCloseBtn_;
    QPushButton* cancelBtn_;
    QPushButton* resetDeviceBtn_;
    QPushButton* cameraRunStateBtn_;
};
