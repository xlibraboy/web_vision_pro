#include "MainWindow.h"
#include "CameraInfo.h"
#include "../config/CameraConfig.h"
#include <QToolBar>
#include <QStatusBar>
#include <QDateTime>
#include <QDebug>
#include <QMenu>
#include <QMenuBar>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMessageBox>

// Register cv::Mat for signal/slot
Q_DECLARE_METATYPE(cv::Mat)

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), currentFps_(0.0), frameCount_(0), isAdmin_(false) {
    
    // Initialize throttle flags
    for (int i = 0; i < 16; ++i) {
        framePending_[i] = false;
    }
    
    qRegisterMetaType<cv::Mat>("cv::Mat");
    qRegisterMetaType<int>("int");
    
    setupUi();
    setupCore();

    // FPS Timer
    connect(&fpsTimer_, &QTimer::timeout, [this]() {
        // Simple FPS calculation
        currentFps_ = frameCount_ * 2.0; // Called every 500ms
        frameCount_ = 0;
        liveDashboard_->updateStatus(currentFps_, false); // Need to update this to support Status per view?
        // If in detail view, maybe update detail view stats?
    });
    fpsTimer_.start(500);
}

MainWindow::~MainWindow() {
    // Stop camera before destruction
    if (cameraManager_) {
        cameraManager_->stopAcquisition();
    }
}

void MainWindow::setupUi() {
    resize(1280, 800);
    setWindowTitle("PaperVision System - Industrial Monitor");
    qInfo() << "Setting up UI...";

    // --- Central Widget (Create FIRST) ---
    stackedWidget_ = new QStackedWidget(this);
    setCentralWidget(stackedWidget_);

    // Views
    liveDashboard_ = new LiveDashboard(2, this);
    connect(liveDashboard_, &LiveDashboard::cameraSelected, this, &MainWindow::showDetail);

    analysisView_ = new AnalysisView(CameraConfig::getCameraCount(), this);
    
    // Create Detail View
    detailView_ = new DetailView(this);
    connect(analysisView_, &AnalysisView::recordAllToggled, this, &MainWindow::toggleRecording);
    connect(analysisView_, &AnalysisView::manualTriggerRequested, this, &MainWindow::manualTrigger);
    connect(detailView_, &DetailView::backRequested, this, &MainWindow::showGrid);
    connect(detailView_, &DetailView::analysisRequested, [this]() {
         switchView(ViewMode::Analysis);
    });
    
    // Connect Configuration Update
    connect(analysisView_, &AnalysisView::configApplied, [this](int preSeconds, int postSeconds, int fps, int binning) {
        double fpsD = static_cast<double>(fps);
        int preFrames = static_cast<int>(preSeconds * fpsD);
        int postFrames = static_cast<int>(postSeconds * fpsD);
        
        // 1. Reconfigure Buffer
        EventController::instance().initialize(preFrames, fpsD, postFrames);
        
        // 2. Reconfigure Cameras
        if (cameraManager_) {
            cameraManager_->setGlobalFrameRate(fpsD);
            cameraManager_->setGlobalResolution(binning);
        }
    });

    // Connect Snapshot Request
    connect(detailView_, &DetailView::snapshotRequested, [this](int cameraId) {
        cameraManager_->triggerSnapshot(cameraId);
    });
    stackedWidget_->addWidget(liveDashboard_); // Index 0
    stackedWidget_->addWidget(detailView_);    // Index 1
    stackedWidget_->addWidget(analysisView_);  // Index 2

    // --- Menu Bar (Create AFTER central widget) ---
    QMenuBar* menu = menuBar();
    menu->setNativeMenuBar(false); // Force menu bar to appear in window, not system menu

    // File Menu
    QMenu* fileMenu = menu->addMenu("File");
    QAction* exitAction = fileMenu->addAction("Exit");
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    // View Menu - Grid Layout Options
    QMenu* viewMenu = menu->addMenu("View");
    
    // Grid layouts (removed "Live View" and "Analysis View" from submenu)
    viewMenu->addAction("Grid 1x1", [this]() { changeLayout(1, 1); });
    viewMenu->addAction("Grid 1x2", [this]() { changeLayout(1, 2); });
    viewMenu->addAction("Grid 2x1", [this]() { changeLayout(2, 1); });
    viewMenu->addAction("Grid 2x2", [this]() { changeLayout(2, 2); });
    viewMenu->addAction("Grid 2x3", [this]() { changeLayout(2, 3); });
    viewMenu->addAction("Grid 3x3", [this]() { changeLayout(3, 3); });
    viewMenu->addAction("Grid 4x3", [this]() { changeLayout(4, 3); });
    viewMenu->addAction("Grid 5x4", [this]() { changeLayout(5, 4); });
    viewMenu->addSeparator(); // Divider added
    customLayoutAction_ = viewMenu->addAction("Custom Grid...");
    connect(customLayoutAction_, &QAction::triggered, this, &MainWindow::promptCustomLayout);

    // Settings Menu
    QMenu* settingsMenu = menu->addMenu("Settings");
    adminLoginAction_ = settingsMenu->addAction("Administrator Login");
    connect(adminLoginAction_, &QAction::triggered, this, &MainWindow::toggleAdmin);
    
    settingsMenu->addSeparator();
    
    // Defect Detection Toggle
    defectDetectionAction_ = settingsMenu->addAction("Enable Defect Detection");
    defectDetectionAction_->setCheckable(true);
    defectDetectionAction_->setChecked(false); // Default: disabled
    connect(defectDetectionAction_, &QAction::triggered, [this](bool checked) {
        if (cameraManager_) {
            cameraManager_->setDefectDetectionEnabled(checked);
        }
    });

    // Secure Delete Toggle (Admin Only)
    deleteEnabledAction_ = settingsMenu->addAction("Enable Delete (Analysis)");
    deleteEnabledAction_->setCheckable(true);
    deleteEnabledAction_->setChecked(false);
    deleteEnabledAction_->setEnabled(false); // Default: Disabled until Admin login
    connect(deleteEnabledAction_, &QAction::triggered, [this](bool checked) {
        if (analysisView_) {
            analysisView_->setDeleteEnabled(checked);
        }
    });

    // Help Menu
    QMenu* helpMenu = menu->addMenu("Help");
    aboutAction_ = helpMenu->addAction("About");
    connect(aboutAction_, &QAction::triggered, this, &MainWindow::showAbout);

    // --- Toolbar ---
    QToolBar* toolbar = addToolBar("Main");
    switchViewAction_ = toolbar->addAction("Analysis View");  // Initial text when on Live View
    connect(switchViewAction_, &QAction::triggered, this, &MainWindow::toggleView);

    toolbar->addSeparator(); // Divider between View switch and Trigger

    triggerAction_ = toolbar->addAction("Trigger Record (S)");
    connect(triggerAction_, &QAction::triggered, this, &MainWindow::manualTrigger);
    
    // Add spacer to push title to far right
    QWidget* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    
    // Add title label to far right of toolbar
    QLabel* titleLabel = new QLabel("PaperVision System - Multi-Camera Live Monitor  ", this);
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #333;");
    toolbar->addWidget(titleLabel);

    // Initial Status
    statusBar()->showMessage("System Initialized");
}

void MainWindow::setupCore() {
    // 1. Initialize Components
    cameraManager_ = std::make_unique<CameraManager>();
    // MEMORY OPTIMIZATION: Reduced buffer to 200 frames (20s @ 10fps) due to high resolution (1024x1040 BGR = 3MB/frame)
    imageBuffer_ = std::make_unique<ImageBuffer>(200, 1024, 1040); 
    defectDetector_ = std::make_unique<DefectDetector>();
    videoEncoder_ = std::make_unique<VideoEncoder>();
    websocketServer_ = std::make_unique<WebsocketServer>(9000);
    websocketServer_->start();

    // 2. Connect Signals
    // Using QueuedConnection to handle cross-thread signal safely
    connect(this, &MainWindow::frameReady, this, &MainWindow::handleFrame, Qt::QueuedConnection);

    // 3. Register Callback
    // THROTTLE: Only emit if GUI is ready for THIS camera. Drops frames if GUI is slow.
    cameraManager_->registerCallback([this](int cameraId, const cv::Mat& frame) {
        if (cameraId >= 0 && cameraId < 16) {
            bool expected = false;
            if (framePending_[cameraId].compare_exchange_strong(expected, true)) {
                // DEEP COPY: Pylon buffer is temporary, must clone for GUI queue
                emit frameReady(cameraId, frame.clone());
            }
        }
    });

    // 4. Start Camera
    cameraManager_->initialize();
    cameraManager_->startAcquisition();
    statusBar()->showMessage("Acquisition Started");
}

void MainWindow::handleFrame(int cameraId, const cv::Mat& frame) {
    // Acknowledge receipt immediately so next frame for THIS camera can be queued.
    if (cameraId >= 0 && cameraId < 16) {
        framePending_[cameraId] = false;
    }

    frameCount_++;
    
    // 1. Add to buffer (only from camera 0 for now)
    if (cameraId == 0) {
        imageBuffer_->addFrame(frame);

        // 2. Run Detection (only on camera 0)
        bool defect = defectDetector_->detect(frame);
        if (defect) {
            QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
            websocketServer_->broadcastDefect(cameraId, timestamp.toStdString());
            manualTrigger(); // Auto-trigger
            statusBar()->showMessage("DEFECT DETECTED! Recording...", 2000);
        }
    }

    // 3. Update GUI
    QWidget* current = stackedWidget_->currentWidget();
    if (current == liveDashboard_) {
        liveDashboard_->updateFrame(cameraId, frame);
    } 
    else if (current == detailView_) {
        // No, DetailView::videoWidget()->updateFrame() works on the widget.
        // We need to know WHICH camera is detailed.
        // For now, let's just make DetailView expose its widget and update it if ID matches.
        // Wait, DetailView has a `videoWidget()` accessor now.
        if (detailView_->videoWidget()->cameraId() == cameraId) {
            detailView_->videoWidget()->updateFrame(frame);
        }
    }
    
    // Also update Analysis View (always, so it has latest frames)
    // USER REQUEST: Remove live view from Analysis View (data record only).
    // QImage qimg(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
    // analysisView_->updateCameraFrame(cameraId, qimg.rgbSwapped().copy());
}

void MainWindow::toggleView() {
    // Toggle between Live/Detail View and Analysis View
    // Index 0: Live, 1: Detail, 2: Analysis
    int currentIndex = stackedWidget_->currentIndex();
    
    // If currently on Analysis View (2), go to Live (0)
    if (currentIndex == 2) {
        // MEMORY OPTIMIZATION: Clear Analysis data when leaving the view
        analysisView_->clearData();
        
        stackedWidget_->setCurrentWidget(liveDashboard_);
        switchViewAction_->setText("Analysis View");
        statusBar()->showMessage(QString("Live View - Grid %1x%2")
            .arg(liveDashboard_->getCurrentRows())
            .arg(liveDashboard_->getCurrentCols()));
    } else {
        // If on Live (0) or Detail (1), go to Analysis (2)
        stackedWidget_->setCurrentWidget(analysisView_);
        switchViewAction_->setText("Live View");
        statusBar()->showMessage("Analysis View");
        
        // Also ensure Analysis View is in a good state (e.g. Live mode if not reviewing)
        // analysisView_->setLiveMode(); // Optional, but good UX
    }
}

void MainWindow::showDetail(int cameraId) {
    qDebug() << "Showing detail for camera" << cameraId;
    
    // MEMORY OPTIMIZATION: If coming from Analysis View, clear its data
    if (stackedWidget_->currentWidget() == analysisView_) {
        analysisView_->clearData();
        switchViewAction_->setText("Analysis View"); // Create consistency
    }
    
    // Get camera info from centralized config
    CameraInfo info = CameraConfig::getCameraInfo(cameraId);
    
    // OVERRIDE with actual data from Pylon (via CameraManager)
    if (cameraManager_) {
        info.model = QString::fromStdString(cameraManager_->getModelName(cameraId));
        cv::Size res = cameraManager_->getResolution();
        info.imageSize = QString("%1 x %2").arg(res.width).arg(res.height);
    }
    
    detailView_->setCamera(cameraId, info, nullptr);
    stackedWidget_->setCurrentWidget(detailView_);
    
    // Use centralized camera label in status bar
    statusBar()->showMessage(QString("Viewing Details: %1").arg(CameraConfig::getCameraLabel(cameraId)));
}

void MainWindow::showGrid() {
    stackedWidget_->setCurrentWidget(liveDashboard_);
    
    // Show grid info in status bar
    statusBar()->showMessage(QString("Grid View %1x%2")
        .arg(liveDashboard_->getCurrentRows())
        .arg(liveDashboard_->getCurrentCols()));
}

void MainWindow::toggleAdmin() {
    if (isAdmin_) {
        // Logout
        isAdmin_ = false;
        adminLoginAction_->setText("Administrator Login");
        statusBar()->showMessage("Administrator Logged Out");
        
        // Disable Secure Delete
        if (deleteEnabledAction_) {
            deleteEnabledAction_->setChecked(false);
            deleteEnabledAction_->setEnabled(false);
        }
        if (analysisView_) {
            analysisView_->setDeleteEnabled(false);
        }
    } else {
        // Login
        bool ok;
        QString text = QInputDialog::getText(this, "Admin Login",
                                             "Password:", QLineEdit::Password,
                                             "", &ok);
        if (ok && text == "admin") { // Simple password
            isAdmin_ = true;
            adminLoginAction_->setText("Logout Administrator");
            statusBar()->showMessage("Administrator Logged In");
            
            // Enable Secure Delete Option
            if (deleteEnabledAction_) {
                deleteEnabledAction_->setEnabled(true);
            }
        } else if (ok) {
            QMessageBox::warning(this, "Login Failed", "Incorrect Password");
        }
    }
    
    if (detailView_) {
        detailView_->setAdminMode(isAdmin_);
    }
}

void MainWindow::changeLayout(int rows, int cols) {
    // Get number of cameras from LiveDashboard (we initialized with 2)
    int numCameras = 2; // This matches the LiveDashboard(2, this) initialization
    
    // Validate grid capacity
    if (rows * cols < numCameras) {
        QMessageBox::warning(this, "Insufficient Grid Capacity",
            QString("Cannot use %1x%2 grid for %3 cameras.\n"
                    "Grid capacity (%4) is less than camera count (%5).\n\n"
                    "Please select a larger grid layout.")
                .arg(rows).arg(cols).arg(numCameras)
                .arg(rows * cols).arg(numCameras));
        return;
    }
    
    liveDashboard_->setGridDimensions(rows, cols);
    statusBar()->showMessage(QString("Layout changed to %1x%2").arg(rows).arg(cols));
}

void MainWindow::promptCustomLayout() {
    bool ok;
    QString text = QInputDialog::getText(this, "Custom Grid Layout",
                                         "Enter rows and columns (e.g., '2 3' for 2 rows, 3 columns):", 
                                         QLineEdit::Normal, "2 2", &ok);
    if (ok && !text.isEmpty()) {
        QStringList parts = text.split(" ", QString::SkipEmptyParts);
        if (parts.size() >= 2) {
            bool rowOk, colOk;
            int rows = parts[0].toInt(&rowOk);
            int cols = parts[1].toInt(&colOk);
            
            if (!rowOk || !colOk) {
                QMessageBox::warning(this, "Input Error", 
                    "Please enter valid numbers.\nExample: '2 3' for 2 rows and 3 columns.");
                return;
            }
            
            if (rows < 1 || cols < 1) {
                QMessageBox::warning(this, "Invalid Dimensions", 
                    "Rows and columns must be at least 1.");
                return;
            }
            
            if (rows > 10 || cols > 10) {
                QMessageBox::warning(this, "Dimensions Too Large", 
                    "Maximum allowed is 10x10 grid.\nLarger grids may cause display issues.");
                return;
            }
            
            changeLayout(rows, cols);
        } else {
            QMessageBox::warning(this, "Invalid Format", 
                "Please enter two numbers separated by space.\nExample: '2 3' for 2 rows and 3 columns.");
        }
    }
}

void MainWindow::showAbout() {
    QMessageBox::about(this, "About PaperVision",
                       "PaperVision System v1.0\n\n"
                       "Industrial Vision System for Paper Machine Monitoring.\n"
                       "Built with Qt 5, OpenCV 4, and Basler Pylon 6.");
}

void MainWindow::switchView(ViewMode mode) {
    switch (mode) {
        case ViewMode::Live:
            stackedWidget_->setCurrentIndex(0);
            // liveDashboard_->resume(); // Not implemented yet
            break;
        case ViewMode::Detail:
            stackedWidget_->setCurrentIndex(1);
            break;
        case ViewMode::Analysis:
            stackedWidget_->setCurrentIndex(2);
            break;
    }
}

void MainWindow::toggleRecording(bool recording) {
    if (recording) {
        statusBar()->showMessage("RECORDING STARTED for all cameras", 0); // 0 means persistent
    } else {
        statusBar()->showMessage("Recording stopped.", 3000);
    }
}

void MainWindow::manualTrigger() {
    std::cout << "[MainWindow] Manual trigger requested." << std::endl;
    EventController::instance().triggerEvent();
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_S) {
        manualTrigger();
    }
    QMainWindow::keyPressEvent(event);
}
