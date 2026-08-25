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
#include <atomic>

#include "LiveDashboard.h"
#include "AnalysisView.h"
#include "DetailView.h"
#include "widgets/ToggleSwitch.h"
#include "widgets/DocsDialog.h"
#include "../core/CameraManager.h"
#include "../processing/ImageBuffer.h"
#include "../processing/DefectDetector.h"
#include "../processing/VideoEncoder.h"

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

signals:
    // Signal to bridge non-Qt thread to Qt thread
    void frameReady(int cameraId, const cv::Mat& frame);



private:
    void setupUi();
    void setupCore();
    void applyGlobalTheme();
    void initializeEventController();
    void applyOpcUaSettings();
    void startCameraLifecycleAsync(bool restart, const QString& reason);
    bool validateSavedCameraConfiguration(QStringList* errors) const;
    bool promptAdminLogin();
    void ensureConfigTab();
    void updateConfigTabAccess();
    void updateAdminStatusIndicator();
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
    ToggleSwitch* defectDetectionCheck_;
    QLabel* adminStatusLabel_ = nullptr;
    QLabel* emulationBadge_ = nullptr;
    QPushButton* diskBadge_ = nullptr;
    QTimer  diskBadgeTimer_;
    QAction* customLayoutAction_;
    QAction* configAction_;
    QAction* aboutAction_;
    
    // Windows
    ConfigDialog* configWindow_ = nullptr;
    DocsDialog* docsDialog_ = nullptr;  // lazy-created, kept open while consulting
    int configTabIndex_ = -1;

    // State
    std::atomic<bool> framePending_[16]; // Throttle flags per camera for GUI updates
    int frameCount_;
    double currentFps_;
    QTimer fpsTimer_;
    bool isAdmin_ = false;
    bool cameraLifecycleInProgress_ = false;
    QFutureWatcher<bool>* cameraLifecycleWatcher_ = nullptr;
    
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
    void toggleAdmin();
    void changeLayout(int rows, int cols);
    void promptCustomLayout();
    void showAbout();
    void showDocs();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
};
