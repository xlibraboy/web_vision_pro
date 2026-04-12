#pragma once

#include <QWidget>
#include <QComboBox>
#include <QSpinBox>
#include "widgets/ToggleSwitch.h"
#include <QPushButton>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QTextEdit>
#include <QMessageBox>
#include <QCheckBox>
#include "CameraInfo.h"
#include "../core/CameraManager.h"

// Forward declarations for new widgets
#include <QGroupBox>
class CameraCard;
class NetworkSummaryHeader;
class DeleteConfirmationDialog;
class CameraDeviceSettingsDialog;

class ConfigDialog : public QWidget {
    Q_OBJECT

public:
    explicit ConfigDialog(CameraManager* cameraManager = nullptr, QWidget *parent = nullptr);
    void setAdminMode(bool isAdmin);

    ~ConfigDialog() override;

signals:
    // True when camera configuration changed and acquisition should be restarted.
    void configUpdated(bool requiresCameraRestart);

private slots:
    void saveAndApply();
    void onAddCameraConfigClicked();
    void onRemoveCameraConfigClicked();
    void onRefreshLogsClicked();
    void onClearLogsClicked();
    void onOpenIpConfiguratorClicked();
    void onToggleLogsClicked();

    // Camera card slots
    void onCameraCardRemoveClicked();
    void onCameraCardEditToggled(bool checked);
    void onCameraCardSourceChanged(int source);
    void onCameraCardMacChanged(const QString& mac);
    void onCameraCardWriteIpClicked();
    void onCameraCardDeviceSettingsClicked();

private:
    void setupUI();
    void loadSettings();
    void createCameraWidgetBlock(const CameraInfo& cam);
    void refreshNetworkStatus();
    void relayoutCameraCards();
    bool validateConfiguration(QStringList* errors) const;
    bool eventFilter(QObject* obj, QEvent* event) override;

    // New premium methods
    void setupPremiumUI();
    void updateCameraCardStatuses();
    void updateNetworkSummary();
    void connectCameraCardSignals(CameraCard* card);
    CameraCard* findCameraCard(int cameraId) const;
    CameraCard* findCameraCard(QObject* sender) const;

    // Global settings UI
    QSpinBox* globalFpsSpin_;
    QSpinBox* preTriggerSpin_;
    QSpinBox* postTriggerSpin_;
    QSpinBox* eventRetentionSpin_;
    QComboBox* themeCombo_;
    QPushButton* saveBtn_;

    // Premium Camera Setup UI
    NetworkSummaryHeader* networkSummaryHeader_;
    QPushButton* addCameraBtn_;

    QScrollArea* cameraScrollArea_;
    QWidget* cameraScrollWidget_;
    QGridLayout* cameraListLayout_;
    QLabel* networkSummaryLabel_;

    // New camera cards (replacing CameraConfigWidgets)
    std::vector<CameraCard*> cameraCards_;
    std::vector<GigEDeviceInfo> currentGigEDevices_;

    CameraManager* cameraManager_;
    QTextEdit* connectionLogsBrowser_;
    QGroupBox* logsGroup_;

    // Admin mode
    bool isAdminMode_;

    // Theme colors
    QColor primaryColor_;
    QColor accentColor_;
};
