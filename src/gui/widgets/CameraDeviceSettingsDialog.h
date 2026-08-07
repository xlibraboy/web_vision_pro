#pragma once

#include <QDialog>
#include <QStringList>
#include <QHash>
#include <QSet>
#include "../CameraInfo.h"

class QCloseEvent;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QFrame;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QTimer;

class CameraManager;

class CameraDeviceSettingsDialog : public QDialog {
    Q_OBJECT

public:
    CameraDeviceSettingsDialog(int cameraIndex, const CameraInfo& info,
                               CameraManager* cameraManager,
                               bool editable, QWidget* parent = nullptr);

    CameraInfo updatedInfo() const;
    bool requiresRestart() const;

signals:
    void settingsApplied(const CameraInfo& info);

protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

private slots:
    void onValueChanged();
    void closeDialog();
    void toggleCameraRunState();
    void onNavChanged(int row);
    void onCancelClicked();
    void applyStagedChanges();

private:
    void setupUi();
    void populateUi();
    void refreshLiveDeviceInfo();
    void updateControlAvailability();
    bool validateInputs(QStringList* errors) const;
    bool hasStopRequiredChanges() const;
    QStringList selectedChunks() const;
    QStringList availableChunkOptions() const;

    QWidget* buildSidebar();
    void buildDetailPages();
    void updateSidebarStatus();
    void updateStagedBadges();
    void updateStagedCallouts();
    void updateApplyStagedEnabled();
    bool confirmStagedClose();
    void stageField(const QString& field);
    void clearStaged();

    int cameraIndex_;
    CameraInfo originalInfo_;
    CameraInfo currentInfo_;
    CameraManager* cameraManager_;
    bool editable_;
    bool populating_ = false;

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

    // Layout
    QListWidget* navList_;
    QStackedWidget* detailStack_;
    QHash<int, QListWidgetItem*> navItems_;    // groupId -> nav item
    QHash<int, QFrame*> stagedCallouts_;       // groupId -> callout frame
    QFrame* statusCard_;
    QLabel* statusChipLabel_;
    QLabel* statusTitleLabel_;
    QLabel* statusModelLabel_;
    QLabel* statusIpLabel_;
    QLabel* statusTempLabel_;
    QPushButton* runStateBtn_;
    QPushButton* applyBtn_;
    QPushButton* applyStagedBtn_;
    QPushButton* cancelBtn_;
    QTimer* refreshTimer_;

    // Staged-change model
    QSet<QString> stagedFields_;
    int stagedCount() const;
    QSet<QString> stagedFieldsInGroup(int groupId) const;
};
