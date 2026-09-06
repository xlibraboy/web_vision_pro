#pragma once

#include <QWidget>
#include <QComboBox>
#include <QListWidget>
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
#include <QStringList>
class QOpcUaClient;
class QOpcUaProvider;
class CameraCard;
class CameraWidget;
class AnalysisVideoWidget;
class NetworkSummaryHeader;
class DeleteConfirmationDialog;
class CameraDeviceSettingsDialog;
class IpConfiguratorPanel;
class FixedIpListPanel;
class MachineGroupsPanel;
class MachineLayoutPanel;
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
    // Switch the left sidebar to the Recording & Triggers page (used when the
    // operator clicks the low-disk badge in the status bar).
    void showRecordingSettingsPage();

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
    void refreshMachineLayout();
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
    void checkRecordingSettingsModified();
    void clearRecordingSettingsModified();
    void refreshStorageStats();
    void refreshOpcUaEndpointDiscovery(bool overwriteExistingEndpoint, bool allowNetworkScan);
    void updateOpcUaDiscoveryStatus(const QString& message, bool detected);
    void updateOpcUaRuntimeStatus(const OpcUaRuntimeStatus& status);
    void refreshOpcUaSpeedDisplay();
    // Async OPC UA server discovery. The probe walks the candidate list one
    // client at a time (750ms per reachable-but-silent host) without ever
    // blocking the UI thread, so switching to System Configuration or pressing
    // Detect Server never freezes on a network timeout.
    void startOpcUaProbe(const QStringList& candidates, bool overwriteExistingEndpoint);
    void probeNextOpcUaCandidate();
    void handleOpcUaProbeOutcome(bool detected, const QString& endpointUrl);
    void handleOpcUaProbeTimeout();
    void clearActiveOpcUaProbeClient();
    void finishOpcUaProbe(bool detected, const QString& endpointUrl, const QString& statusMessage);


    struct UiSettingsSnapshot {
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
        QString analysisDefaultMetadataMode;
        QString analysisMetadataFont;
        int analysisMetadataSize;

        bool operator==(const UiSettingsSnapshot& other) const {
            return themePreset == other.themePreset
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
                && analysisPlaybackSurface == other.analysisPlaybackSurface
                && analysisDefaultMetadataMode == other.analysisDefaultMetadataMode
                && analysisMetadataFont == other.analysisMetadataFont
                && analysisMetadataSize == other.analysisMetadataSize;
        }
        bool operator!=(const UiSettingsSnapshot& other) const {
            return !(*this == other);
        }
    };

    UiSettingsSnapshot captureCurrentSettings() const;

    // --- Recording & Triggers modified tracking ---
    struct RecordingSettingsSnapshot {
        int fps;
        int preTrigger;
        int postTrigger;
        int retention;
        int lowDiskWarningPct;
        int cameraSource;
        QString eventStoragePath;

        bool operator==(const RecordingSettingsSnapshot& other) const {
            return fps == other.fps
                && preTrigger == other.preTrigger
                && postTrigger == other.postTrigger
                && retention == other.retention
                && lowDiskWarningPct == other.lowDiskWarningPct
                && cameraSource == other.cameraSource
                && eventStoragePath == other.eventStoragePath;
        }
        bool operator!=(const RecordingSettingsSnapshot& other) const {
            return !(*this == other);
        }
    };

    RecordingSettingsSnapshot captureRecordingSettings() const;

    // New premium methods
    void setupPremiumUI();
    void updateCameraCardStatuses();
    void updateNetworkSummary();
    void connectCameraCardSignals(CameraCard* card);
    CameraCard* findCameraCard(int cameraId) const;
    CameraCard* findCameraCard(QObject* sender) const;
    // Recording frame-count estimate under the Recording section.
    void updateRecordingInfoLabel();

    // Global settings UI
    QSpinBox* globalFpsSpin_;
    QComboBox* cameraSourceCombo_ = nullptr;
    QSpinBox* preTriggerSpin_;
    QSpinBox* postTriggerSpin_;
    // Live frame-count estimate under the Recording section: informs when the
    // event exceeds the whole-event chart's 600-sample cap.
    QLabel* recordingInfoLabel_ = nullptr;
    QSpinBox* eventRetentionSpin_;
    QSpinBox* lowDiskThresholdSpin_ = nullptr;
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
    QComboBox* analysisDefaultMetadataCombo_ = nullptr;
    QComboBox* analysisMetadataFontCombo_ = nullptr;
    QSpinBox* analysisMetadataSizeSpin_ = nullptr;
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
    RecordingSettingsSnapshot originalRecordingValues_;
    QLabel* recordingUnsavedIndicator_ = nullptr;
    QLabel* storageStatsLabel_ = nullptr;
    // One row per wired sheet-break/trigger sensor. Raised 4 -> 12 so every
    // detector on the machine reference (9 planned) fits with headroom.
    static constexpr int kOpcUaTriggerSlots = 12;

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
    // Speed anchors: one row per machine speed tag. Each row configures a
    // drive's speed tag at its machine position; the app interpolates the local
    // speed between anchors so per-group draw is reflected in defect sync.
    static constexpr int kOpcUaSpeedSlots = 6;
    struct OpcUaSpeedRowWidgets {
        QCheckBox* enabledCheck = nullptr;
        QLineEdit* nameEdit = nullptr;
        QLineEdit* nodeIdEdit = nullptr;
        QComboBox* simulatedCombo = nullptr;
        QDoubleSpinBox* simulatedValueSpin = nullptr;
        QSpinBox* positionMmSpin = nullptr;
        QDoubleSpinBox* scaleSpin = nullptr;
        QDoubleSpinBox* offsetSpin = nullptr;
        QLineEdit* unitEdit = nullptr;
        QSpinBox* staleTimeoutSpin = nullptr;
    };
    std::array<OpcUaSpeedRowWidgets, kOpcUaSpeedSlots> opcUaSpeedRows_{};
    QComboBox* opcUaPositionDirectionCombo_ = nullptr;
    QLabel* opcUaDiscoveryStatusLabel_ = nullptr;
    QPushButton* opcUaDetectEndpointBtn_ = nullptr;
    QPushButton* opcUaSaveBtn_ = nullptr;
    bool opcUaDiscoveryAttempted_ = false;

    // OPC UA probe state (see startOpcUaProbe / probeNextOpcUaCandidate).
    QOpcUaClient* opcUaProbeClient_ = nullptr;
    QOpcUaProvider* opcUaProbeProvider_ = nullptr;
    QTimer* opcUaProbeTimer_ = nullptr;
    QStringList opcUaProbeCandidates_;
    QString opcUaProbeCurrentCandidate_;
    QString opcUaProbeNotFoundMessage_;
    int opcUaProbeCandidateIndex_ = 0;
    bool opcUaProbeActive_ = false;
    bool opcUaProbeOverwrite_ = false;
    QString opcUaProbeBackend_;
    bool opcUaProbeBackendAvailable_ = false;

    // Live Status panel
    QLabel* opcUaStatusClientLabel_ = nullptr;
    QLabel* opcUaStatusSpeedLabel_ = nullptr;
    QTableWidget* opcUaStatusTable_ = nullptr;
    OpcUaClientService* opcUaRuntimeSource_ = nullptr;
    OpcUaRuntimeStatus lastOpcUaRuntimeStatus_;


    // Premium Camera Setup UI
    QListWidget* sidebar_ = nullptr;
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
    MachineLayoutPanel* machineLayoutPanel_ = nullptr;
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
    PresetButtonGroup analysisMetadataPresets_;
    QColor accentColor_;
};
