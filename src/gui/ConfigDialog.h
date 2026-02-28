#pragma once

#include <QWidget>
#include <QComboBox>
#include <QSpinBox>
#include "widgets/ToggleSwitch.h"
#include <QPushButton>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>
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
    void onApplyIpClicked();
    void refreshIpCameraList();
    void onAddCameraConfigClicked();
    void onRemoveCameraConfigClicked();

private:
    void setupUI();
    void loadSettings();

    QComboBox* sourceCombo_;
    QSpinBox* fpsSpin_;
    QSpinBox* preTriggerSpin_;
    QSpinBox* postTriggerSpin_;
    ToggleSwitch* defectCheck_;
    QComboBox* themeCombo_;
    QPushButton* saveBtn_;
    
    // Network Configuration UI
    QComboBox* ipCameraCombo_;
    QPushButton* refreshNetBtn_;
    QPushButton* applyIpBtn_;
    
    QScrollArea* ipScrollArea_;
    QWidget* ipScrollWidget_;
    QVBoxLayout* ipListLayout_;
    
    std::vector<IpConfigEntry> activeIpConfigs_;
    
    std::vector<GigEDeviceInfo> currentGigEDevices_;
};
