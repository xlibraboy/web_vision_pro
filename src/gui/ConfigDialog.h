#pragma once

#include <QWidget>
#include <QComboBox>
#include <QSpinBox>
#include "widgets/ToggleSwitch.h"
#include <QPushButton>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QMessageBox>
#include <QCheckBox>
#include "CameraInfo.h"
#include "../core/CameraManager.h"

struct IpConfigEntry {
    QWidget* container;
    QLineEdit* ip;
    QLineEdit* mask;
    QLineEdit* gw;
    std::string mac;
};

class ConfigDialog : public QWidget {
    Q_OBJECT

public:
    explicit ConfigDialog(QWidget *parent = nullptr);
    void setAdminMode(bool isAdmin);

signals:
    // Signal to notify main window that critical settings (like source) changed
    // or just generally that config was updated
    void configUpdated();

private slots:
    void saveAndApply();
    void onAddCameraConfigClicked();
    void onRemoveCameraConfigClicked();
    void onRefreshLogsClicked();
    void onClearLogsClicked();
    void onOpenIpConfiguratorClicked();

private:
    void setupUI();
    void loadSettings();
    void createCameraWidgetBlock(const CameraInfo& cam);
    bool eventFilter(QObject* obj, QEvent* event) override;

    QSpinBox* globalFpsSpin_; // Was fpsSpin_
    QSpinBox* preTriggerSpin_;
    QSpinBox* postTriggerSpin_;
    ToggleSwitch* defectCheck_;
    QComboBox* themeCombo_;
    QPushButton* saveBtn_;
    
    // Per-Camera Setup UI
    QPushButton* addCameraBtn_;
    
    QScrollArea* cameraScrollArea_;
    QWidget* cameraScrollWidget_;
    QVBoxLayout* cameraListLayout_;
    
    struct CameraConfigWidgets {
        QWidget* container;
        int id; // For saving back
        QComboBox* sourceCombo;
        QLineEdit* nameEdit;
        QLineEdit* locationEdit;
        QComboBox* sideCombo;
        QSpinBox* positionSpin;
        QLabel* ipLabel;
        QComboBox* macCombo;
        QLineEdit* subnetEdit;
        QLineEdit* gatewayEdit;
        QPushButton* writeIpBtn;
        QSpinBox* fpsSpin;
        QCheckBox* editParamsCheck;
        QSpinBox* gainSpin;
        QSpinBox* exposureSpin;
        QSpinBox* gammaSpin;
        QSpinBox* contrastSpin;
    };
    
    std::vector<CameraConfigWidgets> activeCameraConfigs_;
    std::vector<GigEDeviceInfo> currentGigEDevices_;
    
    QTextEdit* connectionLogsBrowser_;
};
