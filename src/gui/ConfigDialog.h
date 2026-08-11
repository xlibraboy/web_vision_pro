#pragma once

#include <QWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QButtonGroup>
#include "widgets/ToggleSwitch.h"
#include <QPushButton>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QTextEdit>
#include <QMessageBox>
#include <QCheckBox>
#include <QFrame>
#include "CameraInfo.h"
#include "../core/CameraManager.h"
#include "../config/CameraConfig.h"
#include "../communication/OpcUaRuntimeStatus.h"
#include <array>


// Forward declarations for new widgets
#include <QGroupBox>
class CameraCard;
class CameraWidget;
class AnalysisVideoWidget;
class NetworkSummaryHeader;
class DeleteConfirmationDialog;
class CameraDeviceSettingsDialog;
class IpConfiguratorPanel;
class FixedIpListPanel;
class MachineGroupsPanel;
class QTimer;
class OpcUaClientService;
class QTableWidget;

class ConfigDialog : public QWidget {
    Q_OBJECT

public:
    explicit ConfigDialog(CameraManager* cameraManager = nullptr, QWidget *parent = nullptr);
    // ConfigDialog may be constructed before the CameraManager exists
    // (MainWindow: setupUi runs before setupCore). Re-wire it once created.
    void setCameraManager(CameraManager* manager);
    void setAdminMode(bool isAdmin);
    // Wire the live OPC UA runtime status source (owned by MainWindow).
    void setOpcUaRuntimeSource(OpcUaClientService* service);

    ~ConfigDialog() override;

signals:
    // True when camera configuration changed and acquisition should be restarted.
    void configUpdated(bool requiresCameraRestart);
    void themeSelectionChanged();
    void cameraDeviceSettingsChanged(int cameraIndex, const CameraInfo& info);
    // Manual push-hold trigger button: emitted while the operator holds a row's
    // button (true) and on release (false). Carries the row's live config so the
    // trigger fires even before the settings are saved.
    void opcUaManualTriggerRequested(int tagIndex, bool held,
                                     const OpcUaTriggerTagSettings& tagSettings);

private slots:
    void saveCameraConfiguration();
    void saveRecordingSettings();
    void saveOpcUaSettings();
    void saveUiSettings();
    void applyUiSettings();
    void resetLiveViewCardSettings();
    void resetAnalysisViewSettings();
    void onAddCameraConfigClicked();
    void onRemoveCameraConfigClicked();
    void onRefreshLogsClicked();
    void onClearLogsClicked();
    void onToggleLogsClicked();
    void onIpConfiguratorApplyRequested(const QString& mac, const QString& mode, const QString& ip,
                                        const QString& mask, const QString& gateway);
    void onIpConfiguratorForceIpRequested(const QString& mac, const QString& tempIp,
                                          const QString& mask, const QString& gateway);

    // Camera card slots
    void onCameraCardRemoveClicked();
    void onCameraCardEditToggled(bool checked);
    void onCameraCardSourceChanged(int source);
    void onCameraCardMacChanged(const QString& mac);
    void onCameraCardDeviceSettingsClicked();
    void onNetworkRefreshTimerTick();

    // Fixed IP List panel

private:
    void setupUI();
    void loadSettings();
    void setupUiModificationTracking();
    void createCameraWidgetBlock(const CameraInfo& cam);
    void refreshFixedIpList();
    void refreshMachineGroups();
    void refreshNetworkStatus();
    void relayoutCameraCards();
    void relayoutUiPreferencePanels();
    bool validateConfiguration(QStringList* errors) const;
    bool validateAndPrepareEventStorage(QString* normalizedPath, QString* errorMessage) const;
    void emitConfigUpdated(bool requiresCameraRestart);
    bool eventFilter(QObject* obj, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void checkUiSettingsModified();
    void clearUiSettingsModified();
    void refreshOpcUaEndpointDiscovery(bool overwriteExistingEndpoint, bool allowNetworkScan);
    void updateOpcUaDiscoveryStatus(const QString& message, bool detected);
    void updateOpcUaRuntimeStatus(const OpcUaRuntimeStatus& status);
    void refreshOpcUaSpeedDisplay();


    struct UiSettingsSnapshot {
        QString eventStoragePath;
        int themePreset;
        QString liveViewGridTitleFont;
        int liveViewGridTitleSize;
        QString liveViewDetailTitleFont;
        int liveViewDetailTitleSize;
        QString liveViewDetailSectionFont;
        int liveViewDetailSectionSize;
        QString liveViewBackgroundStyle;
        QString analysisVideoTitleFont;
        int analysisVideoTitleSize;
        QString analysisTimestampFont;
        int analysisTimestampSize;
        QString analysisTabFont;
        int analysisTabSize;
        QString analysisPlaybackSurface;

        bool operator==(const UiSettingsSnapshot& other) const {
            return eventStoragePath == other.eventStoragePath
                && themePreset == other.themePreset
                && liveViewGridTitleFont == other.liveViewGridTitleFont
                && liveViewGridTitleSize == other.liveViewGridTitleSize
                && liveViewDetailTitleFont == other.liveViewDetailTitleFont
                && liveViewDetailTitleSize == other.liveViewDetailTitleSize
                && liveViewDetailSectionFont == other.liveViewDetailSectionFont
                && liveViewDetailSectionSize == other.liveViewDetailSectionSize
                && liveViewBackgroundStyle == other.liveViewBackgroundStyle
                && analysisVideoTitleFont == other.analysisVideoTitleFont
                && analysisVideoTitleSize == other.analysisVideoTitleSize
                && analysisTimestampFont == other.analysisTimestampFont
                && analysisTimestampSize == other.analysisTimestampSize
                && analysisTabFont == other.analysisTabFont
                && analysisTabSize == other.analysisTabSize
                && analysisPlaybackSurface == other.analysisPlaybackSurface;
        }
        bool operator!=(const UiSettingsSnapshot& other) const {
            return !(*this == other);
        }
    };

    UiSettingsSnapshot captureCurrentSettings() const;

    // New premium methods
    void setupPremiumUI();
    void updateCameraCardStatuses();
    void updateNetworkSummary();
    void connectCameraCardSignals(CameraCard* card);
    CameraCard* findCameraCard(int cameraId) const;
    CameraCard* findCameraCard(QObject* sender) const;

    // Global settings UI
    QSpinBox* globalFpsSpin_;
    QComboBox* cameraSourceCombo_ = nullptr;
    QSpinBox* preTriggerSpin_;
    QSpinBox* postTriggerSpin_;
    QSpinBox* eventRetentionSpin_;
    QButtonGroup* themeButtonGroup_ = nullptr;
    QPushButton* themeCards_[8] = {};
    int selectedThemeIndex_ = 0;
    QComboBox* themeCombo_ = nullptr;
    QWidget* themeGridWidget_ = nullptr;
    QGridLayout* themeGridLayout_ = nullptr;
    QComboBox* liveViewGridTitleFontCombo_ = nullptr;
    QSpinBox* liveViewGridTitleSizeSpin_ = nullptr;
    QComboBox* liveViewDetailTitleFontCombo_ = nullptr;
    QSpinBox* liveViewDetailTitleSizeSpin_ = nullptr;
    QComboBox* liveViewDetailSectionFontCombo_ = nullptr;
    QSpinBox* liveViewDetailSectionSizeSpin_ = nullptr;
    QComboBox* liveViewBackgroundStyleCombo_ = nullptr;
    QComboBox* analysisVideoTitleFontCombo_ = nullptr;
    QSpinBox* analysisVideoTitleSizeSpin_ = nullptr;
    QComboBox* analysisTimestampFontCombo_ = nullptr;
    QSpinBox* analysisTimestampSizeSpin_ = nullptr;
    QComboBox* analysisTabFontCombo_ = nullptr;
    QSpinBox* analysisTabSizeSpin_ = nullptr;
    QComboBox* analysisPlaybackSurfaceCombo_ = nullptr;
    QScrollArea* uiPreferencesScrollArea_ = nullptr;
    QHBoxLayout* liveViewContentLayout_ = nullptr;
    QHBoxLayout* analysisViewContentLayout_ = nullptr;
    QWidget* livePreviewContainer_ = nullptr;
    QWidget* analysisPreviewContainer_ = nullptr;
    CameraWidget* liveViewGridPreviewWidget_ = nullptr;
    CameraWidget* liveViewDetailPreviewWidget_ = nullptr;
    AnalysisVideoWidget* analysisPreviewVideoWidget_ = nullptr;
    QFrame* analysisPreviewFrame_ = nullptr;
    QLabel* analysisPreviewSectionLabel_ = nullptr;
    QLabel* analysisPreviewTabLabel_ = nullptr;
    QLabel* analysisPreviewFrameLabel_ = nullptr;
    QPushButton* analysisResetBtn_ = nullptr;
    QFrame* liveViewCardPreviewFrame_ = nullptr;
    QLabel* liveViewCardPreviewTitleLabel_ = nullptr;
    QLabel* liveViewCardPreviewMetaLabel_ = nullptr;
    QLabel* liveViewCardPreviewStatusLabel_ = nullptr;
    QGroupBox* liveViewCardPreviewInfoGroup_ = nullptr;
    QGroupBox* liveViewCardPreviewControlGroup_ = nullptr;
    QPushButton* liveViewResetBtn_ = nullptr;
    QLineEdit* eventStoragePathEdit_ = nullptr;
    QPushButton* browseEventStorageBtn_ = nullptr;
    QPushButton* resetEventStorageBtn_ = nullptr;
    QPushButton* cameraSaveBtn_ = nullptr;
    QPushButton* recordingSaveBtn_ = nullptr;
    QPushButton* uiSaveBtn_ = nullptr;
    QPushButton* uiApplyBtn_ = nullptr;
    QLabel* uiUnsavedIndicator_ = nullptr;
    UiSettingsSnapshot originalValues_;
    static constexpr int kOpcUaTriggerSlots = 4;

    struct OpcUaTriggerRowWidgets {
        QCheckBox* enabledCheck = nullptr;
        QLineEdit* nameEdit = nullptr;
        QLineEdit* nodeIdEdit = nullptr;
        QComboBox* simulatedCombo = nullptr;
        QComboBox* groupCombo = nullptr;
        QSpinBox* positionMmSpin = nullptr;
        QPushButton* manualTriggerBtn = nullptr;
        QSpinBox* minimumIntervalSpin = nullptr;
    };

    QCheckBox* opcUaEnabledCheck_ = nullptr;
    QLineEdit* opcUaEndpointEdit_ = nullptr;
    QComboBox* opcUaAuthModeCombo_ = nullptr;
    QLineEdit* opcUaUsernameEdit_ = nullptr;
    QLineEdit* opcUaPasswordEdit_ = nullptr;
    QSpinBox* opcUaPublishIntervalSpin_ = nullptr;
    QSpinBox* opcUaReconnectIntervalSpin_ = nullptr;
    std::array<OpcUaTriggerRowWidgets, kOpcUaTriggerSlots> opcUaTriggerRows_{};
    QCheckBox* opcUaSpeedEnabledCheck_ = nullptr;
    QLineEdit* opcUaSpeedNameEdit_ = nullptr;
    QLineEdit* opcUaSpeedNodeIdEdit_ = nullptr;
    QDoubleSpinBox* opcUaSpeedScaleSpin_ = nullptr;
    QDoubleSpinBox* opcUaSpeedOffsetSpin_ = nullptr;
    QLineEdit* opcUaSpeedUnitEdit_ = nullptr;
    QSpinBox* opcUaSpeedStaleTimeoutSpin_ = nullptr;
    QComboBox* opcUaSpeedSimulatedCombo_ = nullptr;
    QDoubleSpinBox* opcUaSpeedSimulatedValueSpin_ = nullptr;
    QComboBox* opcUaPositionDirectionCombo_ = nullptr;
    QLabel* opcUaDiscoveryStatusLabel_ = nullptr;
    QPushButton* opcUaDetectEndpointBtn_ = nullptr;
    QPushButton* opcUaSaveBtn_ = nullptr;
    bool opcUaDiscoveryAttempted_ = false;

    // Live Status panel
    QLabel* opcUaStatusClientLabel_ = nullptr;
    QLabel* opcUaStatusSpeedLabel_ = nullptr;
    QTableWidget* opcUaStatusTable_ = nullptr;
    OpcUaClientService* opcUaRuntimeSource_ = nullptr;
    OpcUaRuntimeStatus lastOpcUaRuntimeStatus_;


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
    IpConfiguratorPanel* ipConfiguratorPanel_ = nullptr;
    FixedIpListPanel* fixedIpListPanel_ = nullptr;
    MachineGroupsPanel* machineGroupsPanel_ = nullptr;
    QTimer* networkRefreshTimer_ = nullptr;
    QTextEdit* connectionLogsBrowser_;
    QGroupBox* logsGroup_;

    // Admin mode
    bool isAdminMode_;

    // Theme colors
    QColor primaryColor_;

    struct PresetButtonGroup {
        QPushButton* btnS = nullptr;
        QPushButton* btnM = nullptr;
        QPushButton* btnL = nullptr;
        QSpinBox* targetSpin = nullptr;
        int presetS = 0, presetM = 0, presetL = 0;
        QString activeStyle;
        QString inactiveStyle;
        void updateStyles() {
            const int val = targetSpin->value();
            if (btnS) btnS->setStyleSheet(val == presetS ? activeStyle : inactiveStyle);
            if (btnM) btnM->setStyleSheet(val == presetM ? activeStyle : inactiveStyle);
            if (btnL) btnL->setStyleSheet(val == presetL ? activeStyle : inactiveStyle);
        }
    };

    PresetButtonGroup liveViewGridTitlePresets_;
    PresetButtonGroup liveViewDetailTitlePresets_;
    PresetButtonGroup liveViewDetailSectionPresets_;
    PresetButtonGroup analysisVideoTitlePresets_;
    PresetButtonGroup analysisTimestampPresets_;
    PresetButtonGroup analysisTabPresets_;
    QColor accentColor_;
};
