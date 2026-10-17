#pragma once

#include <QMainWindow>
#include <QCloseEvent>
#include <QStackedWidget>
#include <QTabWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QMutex>
#include <QFutureWatcher>
#include <QSet>
#include <QVector>
#include <atomic>

#include "LiveDashboard.h"
#include "AnalysisView.h"
#include "DetailView.h"
#include "widgets/DocsDialog.h"
#include "widgets/ChangelogDialog.h"
#include "widgets/LiveViewWindow.h"
#include "../core/CameraManager.h"
#include "../processing/ImageBuffer.h"
#include "../processing/DefectDetector.h"
#include "../processing/VideoEncoder.h"
#include "../config/UserStore.h"

class ConfigDialog;
class OpcUaClientService;


class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    enum class ViewMode {
        Live,
        Detail,
        Analysis
    };
    
    void switchView(ViewMode mode);
    void raiseAndActivate();

    // Body of Help > About (static so the regression suite can check that the
    // version line reports the pylon runtime actually in use).
    static QString aboutText();

signals:
    // Signal to bridge non-Qt thread to Qt thread
    void frameReady(int cameraId, const cv::Mat& frame);



private:
    void setupUi();
    void setupCore();
    void applyGlobalTheme();
    // Polls the desktop colour scheme while ThemeMode::System is selected, so a
    // desktop that flips between light and dark is followed without a restart.
    void updateSystemThemeWatch();
    void initializeEventController();
    void applyOpcUaSettings();
    void startCameraLifecycleAsync(bool restart, const QString& reason);
    bool validateSavedCameraConfiguration(QStringList* errors) const;
    bool promptLogin();
    void ensureConfigTab();
    void updateConfigTabAccess();
    // Push the session role out to every gated surface: the status pill, the
    // pause button, the grid-layout menu, the views and the configuration tab.
    void applyRoleToUi();
    void updateRoleStatusIndicator();
    // Re-tint the role pill's padlock: ink while the button is held down
    // (the press fill is the accent itself), otherwise the state tint.
    // Code-drawn pixmaps do not follow the stylesheet's state rules.
    void updateRoleButtonIcon(bool pressed);
    // Pause enabled state = role gate (Engineer and above) combined with what
    // the camera lifecycle currently allows (pauseAllowedByState_).
    void updatePauseButtonEnabled();
    void refreshDiskBadge();
    void openRecordingSettings();

    // GUI
    QTabWidget* mainTabWidget_;
    QStackedWidget* stackedWidget_;
    LiveDashboard* liveDashboard_ = nullptr;
    AnalysisView* analysisView_ = nullptr;
    DetailView* detailView_ = nullptr;
    
    // Actions & Menus
    QPushButton* triggerBtn_;
    QPushButton* snapshotBtn_;
    QPushButton* pauseBtn_;
    // Session role control: a clickable status pill in the bottom right
    // (login/logout). Text, tint and tooltip follow the current role.
    QPushButton* roleStatusButton_ = nullptr;
    QLabel* emulationBadge_ = nullptr;
    // Live capture readout (FPS + recording state) in the status bar, so the
    // most basic monitor facts are visible from every view.
    QLabel* liveStatusLabel_ = nullptr;
    QPushButton* diskBadge_ = nullptr;
    QTimer  diskBadgeTimer_;
    // Re-probes the desktop colour scheme on a timer (see
    // updateSystemThemeWatch); stopped unless ThemeMode::System is selected.
    QTimer  themeSchemeTimer_;
    bool systemSchemeLogged_ = false;
    // Grid-layout picks (View menu): an Engineer capability, so they are
    // enabled and disabled with the session role.
    QVector<QAction*> gridLayoutActions_;
    QAction* customLayoutAction_;
    QAction* configAction_;
    QAction* aboutAction_;
    
    // Windows
    ConfigDialog* configWindow_ = nullptr;
    DocsDialog* docsDialog_ = nullptr;  // lazy-created, kept open while consulting
    ChangelogDialog* changelogDialog_ = nullptr;  // lazy-created, single instance
    int configTabIndex_ = -1;
    // Detachable live-only window (grid <-> single camera, no settings). One
    // instance shared app-wide; opening the Live View Window again raises it.
    LiveViewWindow* liveViewWindow_ = nullptr;

    // State
    std::atomic<bool> framePending_[16]; // Throttle flags per camera for GUI updates
    int frameCount_;
    double currentFps_;
    QTimer fpsTimer_;
    // Current session: Operator on startup (needs no sign-in); Engineer and
    // Administrator are stored accounts signed in with a password.
    AccessRole currentRole_ = AccessRole::Operator;
    QString currentUser_;
    // Whether the camera lifecycle currently allows pausing at all; the role
    // gate is layered on top in updatePauseButtonEnabled().
    bool pauseAllowedByState_ = true;
    bool cameraLifecycleInProgress_ = false;
    QFutureWatcher<bool>* cameraLifecycleWatcher_ = nullptr;
    // Cameras currently flagged with an excessive PTP offset (alert active), so
    // the status-bar warning fires on the rising edge, not every poll.
    QSet<int> ptpOffsetAlertCameras_;
    
    // Core
    std::unique_ptr<CameraManager> cameraManager_;
    std::unique_ptr<ImageBuffer> imageBuffer_;
    std::unique_ptr<DefectDetector> defectDetector_;

    std::unique_ptr<VideoEncoder> videoEncoder_;
    std::unique_ptr<OpcUaClientService> opcUaClientService_;

    


private slots:
    // Slot handling the frame on the main thread
    void handleFrame(int cameraId, const cv::Mat& frame);
    void toggleView();
    void manualTrigger();
    void togglePauseGrab();
    void toggleRecording(bool recording);
    void showDetail(int cameraId);
    void showGrid();
    void openSystemConfiguration();
    void toggleSession();
    void changeLayout(int rows, int cols);
    void promptCustomLayout();
    void showAbout();
    void showDocs();
    void showChangelog();
    void showLiveViewWindow();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
};
