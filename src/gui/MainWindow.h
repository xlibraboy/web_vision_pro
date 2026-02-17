#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QTimer>
#include <QMutex>
#include <atomic>

#include "LiveDashboard.h"
#include "AnalysisView.h"
#include "DetailView.h"
#include "../core/CameraManager.h"
#include "../processing/ImageBuffer.h"
#include "../processing/DefectDetector.h"
#include "../processing/VideoEncoder.h"
#include "../communication/WebsocketServer.h" // Added dependency

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

signals:
    // Signal to bridge non-Qt thread to Qt thread
    void frameReady(int cameraId, const cv::Mat& frame);



private:
    void setupUi();
    void setupCore();

    // GUI
    QStackedWidget* stackedWidget_;
    LiveDashboard* liveDashboard_;
    AnalysisView* analysisView_; // Existing view, maybe we keep it for playback?
    DetailView* detailView_;     // New view
    
    // Actions & Menus
    QAction* switchViewAction_;
    QAction* triggerAction_;
    QAction* adminLoginAction_; // Login toggle
    QAction* defectDetectionAction_; // Defect detection toggle
    QAction* deleteEnabledAction_; // Checkable action for Secure Delete
    QAction* customLayoutAction_;
    QAction* aboutAction_;

    // State
    std::atomic<bool> framePending_[16]; // Throttle flags per camera for GUI updates
    int frameCount_;
    double currentFps_;
    QTimer fpsTimer_;
    bool isAdmin_ = false;
    
    // Core
    std::unique_ptr<CameraManager> cameraManager_;
    std::unique_ptr<ImageBuffer> imageBuffer_;
    std::unique_ptr<DefectDetector> defectDetector_;
    std::unique_ptr<VideoEncoder> videoEncoder_;
    std::unique_ptr<WebsocketServer> websocketServer_;
    


private slots:
    // Slot handling the frame on the main thread
    void handleFrame(int cameraId, const cv::Mat& frame);
    void toggleView();
    void manualTrigger();
    void toggleRecording(bool recording);
    void showDetail(int cameraId);
    void showGrid();
    void toggleAdmin();
    void changeLayout(int rows, int cols);
    void promptCustomLayout();
    void showAbout();

protected:
    void keyPressEvent(QKeyEvent *event) override;
};
