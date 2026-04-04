#include "MainWindow.h"
#include "CameraInfo.h"
#include "../config/CameraConfig.h"
#include "ConfigDialog.h"
#include <QToolBar>
#include <QStatusBar>
#include <QDateTime>
#include <QDebug>
#include <QMenu>
#include <QMenuBar>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QDesktopWidget>
#include <QApplication>

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
    // Enable clean UI shutdown for child dialogs not parented correctly
    if (configWindow_) {
        configWindow_->close();
    }
    
    // Stop camera before destruction
    if (cameraManager_) {
        cameraManager_->stopAcquisition();
    }
}

void MainWindow::setupUi() {
    QRect screenGeometry = QApplication::desktop()->availableGeometry(this);
    setGeometry(screenGeometry);
    setWindowState(Qt::WindowMaximized);
    setWindowTitle("PaperVision System - Industrial Monitor");
    qInfo() << "Setting up UI...";
    
    // Apply theme globally right at startup
    applyGlobalTheme();

    // --- Central Widget (Create FIRST) ---
    mainTabWidget_ = new QTabWidget(this);
    mainTabWidget_->setTabBarAutoHide(false);
    mainTabWidget_->setDocumentMode(true); // Optional: cleaner look
    setCentralWidget(mainTabWidget_);

    // --- Tab 1: Live Area ---
    QWidget* liveTab = new QWidget();
    QVBoxLayout* liveLayout = new QVBoxLayout(liveTab);
    liveLayout->setContentsMargins(0, 0, 0, 0);

    // Live Controls (Trigger, Snapshot) at the top of Live View Tab
    QHBoxLayout* liveControlsLayout = new QHBoxLayout();
    liveControlsLayout->setContentsMargins(8, 8, 8, 8);
    
    triggerBtn_ = new QPushButton("Trigger Record (S)");
    connect(triggerBtn_, &QPushButton::clicked, this, &MainWindow::manualTrigger);
    
    snapshotBtn_ = new QPushButton("Take Snapshot");
    connect(snapshotBtn_, &QPushButton::clicked, this, [this]() {
        if (stackedWidget_->currentWidget() == detailView_) {
            if (cameraManager_) {
                cameraManager_->triggerSnapshot(detailView_->videoWidget()->cameraId());
                statusBar()->showMessage("Snapshot triggered.", 2000);
            }
        } else {
            QMessageBox::information(this, "Snapshot", "Snapshots can only be taken from the Detail View.");
        }
    });

    liveControlsLayout->addWidget(triggerBtn_);
    liveControlsLayout->addWidget(snapshotBtn_);
    
    pauseBtn_ = new QPushButton("Pause Grab");
    connect(pauseBtn_, &QPushButton::clicked, this, &MainWindow::togglePauseGrab);
    liveControlsLayout->addWidget(pauseBtn_);
    
    // Add spacer before check
    liveControlsLayout->addStretch();
    
    QLabel* defectLabel = new QLabel("Enable Defect Detection:");
    liveControlsLayout->addWidget(defectLabel);
    
    defectDetectionCheck_ = new ToggleSwitch(this);
    defectDetectionCheck_->setEnabled(isAdmin_); // Linked to Admin
    connect(defectDetectionCheck_, &ToggleSwitch::toggled, [this](bool checked) {
        if (cameraManager_) {
            cameraManager_->setDefectDetectionEnabled(checked);
        }
    });
    liveControlsLayout->addWidget(defectDetectionCheck_);
    
    liveLayout->addLayout(liveControlsLayout);

    stackedWidget_ = new QStackedWidget(this);
    
    // Views
    liveDashboard_ = new LiveDashboard(CameraConfig::getCameraCount(), this);
    connect(liveDashboard_, &LiveDashboard::cameraSelected, this, &MainWindow::showDetail);

    detailView_ = new DetailView(this);
    connect(detailView_, &DetailView::backRequested, this, &MainWindow::showGrid);
    connect(detailView_, &DetailView::analysisRequested, [this]() {
         switchView(ViewMode::Analysis);
    });
    connect(detailView_, &DetailView::snapshotRequested, [this](int cameraId) {
        if (cameraManager_) cameraManager_->triggerSnapshot(cameraId);
    });
    // Live parameter adjustment: forward signal to CameraManager
    connect(detailView_, &DetailView::parameterChanged, [this](int cameraId, QString param, double value) {
        if (!cameraManager_) return;
        if (param == "Gain")          cameraManager_->setCameraGain(cameraId, value);
        else if (param == "Exposure") cameraManager_->setCameraExposure(cameraId, value);
        else if (param == "Gamma")    cameraManager_->setCameraGamma(cameraId, value);
        else if (param == "Contrast") cameraManager_->setCameraContrast(cameraId, value);
    });
    connect(detailView_, &DetailView::saveParametersRequested, [this](int cameraId) {
        if (!cameraManager_) return;
        bool ok = cameraManager_->saveParameters(cameraId);
        statusBar()->showMessage(ok ? QString("📁 Parameters saved for Camera %1").arg(cameraId + 1)
                                    : QString("⚠ Failed to save parameters for Camera %1").arg(cameraId + 1), 3000);
    });
    connect(detailView_, &DetailView::loadParametersRequested, [this](int cameraId) {
        if (!cameraManager_) return;
        bool ok = cameraManager_->loadParameters(cameraId);
        if (ok) {
            // Readback current values from Pylon nodemap and refresh UI spinboxes
            auto p = cameraManager_->getCameraParams(cameraId);
            detailView_->setParameterValues(p.gain, p.exposureUs, p.gamma, p.contrast);
            statusBar()->showMessage(QString("✅ Parameters loaded for Camera %1").arg(cameraId + 1), 3000);
        } else {
            statusBar()->showMessage(QString("⚠ No saved parameters found for Camera %1").arg(cameraId + 1), 3000);
        }
    });

    stackedWidget_->addWidget(liveDashboard_); // Index 0
    stackedWidget_->addWidget(detailView_);    // Index 1
    
    liveLayout->addWidget(stackedWidget_);
    mainTabWidget_->addTab(liveTab, "Live View");

    // --- Tab 2: Analysis Area ---
    analysisView_ = new AnalysisView(CameraConfig::getCameraCount(), this);
    connect(analysisView_, &AnalysisView::recordAllToggled, this, &MainWindow::toggleRecording);
    connect(analysisView_, &AnalysisView::manualTriggerRequested, this, &MainWindow::manualTrigger);
    
    mainTabWidget_->addTab(analysisView_, "Analysis View");
    
    // Connect tab change logic
    connect(mainTabWidget_, &QTabWidget::currentChanged, [this](int index) {
        if (index == 0) {
            analysisView_->clearData(); // Memory optimization
            
            statusBar()->showMessage(QString("Live View - Grid %1x%2")
                .arg(liveDashboard_->getCurrentRows())
                .arg(liveDashboard_->getCurrentCols()));
        } else {
            statusBar()->showMessage("Analysis View");
        }
    });

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
    
    // Configuration Window
    configAction_ = settingsMenu->addAction("Configuration...");
    configAction_->setEnabled(isAdmin_); // Grayed out until Admin login
    connect(configAction_, &QAction::triggered, [this]() {
        if (!configWindow_) {
            configWindow_ = new ConfigDialog(cameraManager_.get());
            connect(configWindow_, &ConfigDialog::configUpdated, [this]() {
                // Total Camera Source/MAC change requires restart
                if (cameraManager_) {
                    cameraManager_->stopAcquisition();
                    cameraManager_->initialize();
                    cameraManager_->startAcquisition();
                    
                    // Reload live settings like FPS
                    std::vector<CameraInfo> cams = CameraConfig::getCameras();
                    for (int i = 0; i < (int)cams.size(); ++i) {
                        cameraManager_->setCameraFrameRate(i, cams[i].fps, cams[i].enableAcquisitionFps);
                    }
                    cameraManager_->setDefectDetectionEnabled(CameraConfig::isDefectDetectionEnabled());
                }
                
                // Update camera counts in views to handle newly added cameras
                int newCamCount = CameraConfig::getCameraCount();
                if (liveDashboard_) liveDashboard_->setCameraCount(newCamCount);
                if (analysisView_) analysisView_->setCameraCount(newCamCount);
                
                // Redraw UI to remove empty placeholders for newly configured cameras
                if (liveDashboard_) {
                    liveDashboard_->setGridDimensions(liveDashboard_->getCurrentRows(), liveDashboard_->getCurrentCols());
                }
                
                // Update global tracking configurations
                EventController::instance().initialize(
                    CameraConfig::getPreTriggerSeconds() * 10,  // fallback base rate
                    10, 
                    CameraConfig::getPostTriggerSeconds() * 10
                );
                
                // Reload UI Theme Globally
                this->applyGlobalTheme();
            });
            // Handle cleanup if window is destroyed
            connect(configWindow_, &QObject::destroyed, [this]() {
                configWindow_ = nullptr;
            });
        }
        
        // Push admin state to config window before showing
        configWindow_->setAdminMode(isAdmin_);
        
        // Show and bring to front
        if (configWindow_->isHidden()) {
            configWindow_->show();
        }
        configWindow_->raise();
        configWindow_->activateWindow();
    });

    // Help Menu
    QMenu* helpMenu = menu->addMenu("Help");
    aboutAction_ = helpMenu->addAction("About");
    connect(aboutAction_, &QAction::triggered, this, &MainWindow::showAbout);

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

    // 2. Connect Signals
    // Using QueuedConnection to handle cross-thread signal safely
    connect(this, &MainWindow::frameReady, this, &MainWindow::handleFrame, Qt::QueuedConnection);

    // Status Callback from CameraManager to MainWindow
    cameraManager_->registerStatusCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            statusBar()->showMessage(QString::fromStdString(msg), 5000);
        }, Qt::QueuedConnection);
    });

    // 3. Register Callback
    // THROTTLE: Only emit if GUI is ready for THIS camera. Drops frames if GUI is slow.
    cameraManager_->registerCallback([this](int cameraId, const cv::Mat& frame) {
        if (cameraId >= 0 && cameraId < 16) {
            bool expected = false;
            // Always allow empty frames to pass through to clear UI
            if (frame.empty()) {
                emit frameReady(cameraId, cv::Mat());
                return;
            }
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

    // 5. Register Temperature Alert Callback (Basler App Note AW00138003000)
    cameraManager_->registerTemperatureAlertCallback(
        [this](int camId, double temp, CameraManager::TemperatureStatus status) {
            QMetaObject::invokeMethod(this, [this, camId, temp, status]() {
                // Update grid tile badge
                if (liveDashboard_) {
                    liveDashboard_->updateCameraTemperature(camId, temp, status);
                }
                // Update DetailView if it's currently showing this camera
                if (detailView_ && detailView_->videoWidget() &&
                    detailView_->videoWidget()->cameraId() == camId) {
                    detailView_->updateTemperature(temp);
                }
                // Status bar alert for Critical or Error
                if (status == TempStatus::Error) {
                    statusBar()->showMessage(
                        QString("🔴 OVER-TEMP ERROR: Camera %1 at %2°C — Sensor may power down!").arg(camId + 1).arg(temp, 0, 'f', 1),
                        10000);
                } else if (status == TempStatus::Critical) {
                    statusBar()->showMessage(
                        QString("🟠 CRITICAL TEMP: Camera %1 at %2°C — approaching sensor limit!").arg(camId + 1).arg(temp, 0, 'f', 1),
                        8000);
                }
            }, Qt::QueuedConnection);
        }
    );
}

void MainWindow::handleFrame(int cameraId, const cv::Mat& frame) {
    // Acknowledge receipt immediately so next frame for THIS camera can be queued.
    if (cameraId >= 0 && cameraId < 16) {
        framePending_[cameraId] = false;
    }

    if (frame.empty()) {
        liveDashboard_->clearCameraWidget(cameraId);
        if (detailView_->videoWidget()->cameraId() == cameraId) {
            detailView_->videoWidget()->clearFrame();
        }
        return;
    }

    frameCount_++;
    
    // 1. Add to buffer (only from camera 0 for now)
    if (cameraId == 0) {
        imageBuffer_->addFrame(frame);
    }
    
    // 2. Update GUI
    liveDashboard_->updateFrame(cameraId, frame);
    
    // Always keep DetailView in sync if it's currently focused on this camera
    if (detailView_->videoWidget()->cameraId() == cameraId) {
        detailView_->videoWidget()->updateFrame(frame);
    }
    
    // Also update Analysis View (always, so it has latest frames)
    // USER REQUEST: Remove live view from Analysis View (data record only).
    // QImage qimg(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
    // analysisView_->updateCameraFrame(cameraId, qimg.rgbSwapped().copy());
}

void MainWindow::toggleView() {
    int currentIndex = mainTabWidget_->currentIndex();
    if (currentIndex == 1) {
        mainTabWidget_->setCurrentIndex(0);
    } else {
        mainTabWidget_->setCurrentIndex(1);
    }
}

void MainWindow::showDetail(int cameraId) {
    qDebug() << "Showing detail for camera" << cameraId;
    
    // MEMORY OPTIMIZATION: Switch tab to ensure live stack logic
    if (mainTabWidget_->currentIndex() == 1) {
        mainTabWidget_->setCurrentIndex(0); 
    }
    
    // Get camera info from centralized config
    CameraInfo info = CameraConfig::getCameraInfo(cameraId);
    
    // Smooth transition: Fetch current live image from the grid so it doesn't flash "waiting"
    QImage currentFrame = liveDashboard_->getCameraImage(cameraId);
    if (!currentFrame.isNull()) {
        detailView_->videoWidget()->setImage(currentFrame);
    } else {
        detailView_->videoWidget()->clearFrame();
    }
    
    // OVERRIDE with actual data from Pylon (via CameraManager)
    if (cameraManager_) {
        info.model = QString::fromStdString(cameraManager_->getModelName(cameraId));
        info.ipAddress = QString::fromStdString(cameraManager_->getIpAddress(cameraId));
        cv::Size res = cameraManager_->getCameraResolution(cameraId);
        info.imageSize = QString("%1 x %2").arg(res.width).arg(res.height);
        double f = cameraManager_->getCameraFps(cameraId);
        if (f > 0) info.fps = f; // only override if readable
        info.temperature = cameraManager_->getTemperature(cameraId);

        // Read actual live camera parameters (Gain, Exposure, Gamma) directly from the camera
        CameraManager::CameraParams liveParams = cameraManager_->getCameraParams(cameraId);

        detailView_->setCamera(cameraId, info, nullptr);
        detailView_->setParameterValues(liveParams.gain, liveParams.exposureUs, liveParams.gamma, liveParams.contrast);
    } else {
        detailView_->setCamera(cameraId, info, nullptr);
    }
    
    // Query Display FPS from the CameraWidget's ring buffer
    CameraWidget* widget = liveDashboard_->getCameraWidget(cameraId);
    if (widget) {
        detailView_->setDisplayFps(widget->getActualDisplayFps());
    }
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
        
        if (defectDetectionCheck_) {
            defectDetectionCheck_->setChecked(false);
            defectDetectionCheck_->setEnabled(false);
        }
        if (analysisView_) {
            analysisView_->setAdminMode(false);
        }
        if (configAction_) {
            configAction_->setEnabled(false);
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
            
            if (defectDetectionCheck_) {
                defectDetectionCheck_->setEnabled(true);
            }
            if (analysisView_) {
                analysisView_->setAdminMode(true);
            }
            // Enable Configuration
            if (configAction_) {
                configAction_->setEnabled(true);
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
    // Get number of cameras from config
    int numCameras = CameraConfig::getCameraCount();
    
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
            mainTabWidget_->setCurrentIndex(0);
            stackedWidget_->setCurrentIndex(0);
            break;
        case ViewMode::Detail:
            mainTabWidget_->setCurrentIndex(0);
            stackedWidget_->setCurrentIndex(1);
            break;
        case ViewMode::Analysis:
            mainTabWidget_->setCurrentIndex(1);
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

void MainWindow::togglePauseGrab() {
    if (!cameraManager_) return;
    
    bool isPaused = cameraManager_->isGrabbingPaused();
    cameraManager_->pauseGrabbing(!isPaused);
    
    if (!isPaused) {
        pauseBtn_->setText("Resume Grab");
        // Apply a visual warning tint (light red) to signify paused state. 
        // This takes precedence over global stylesheet.
        pauseBtn_->setStyleSheet("background-color: #ffaaaa; color: #880000;");
        statusBar()->showMessage("Camera Grabbing PAUSED", 3000);
    } else {
        pauseBtn_->setText("Pause Grab");
        // Clear local style to revert to global theme
        pauseBtn_->setStyleSheet("");
        statusBar()->showMessage("Camera Grabbing RESUMED", 3000);
    }
}

void MainWindow::applyGlobalTheme() {
    ThemeColors tc = CameraConfig::getThemeColors();
    const QString& bgColor    = tc.bg;
    const QString& borderColor = tc.border;
    const QString& btnBg      = tc.btnBg;
    const QString& btnHover   = tc.btnHover;
    const QString& primaryColor = tc.primary;
    const QString& sliderBg   = tc.sliderBg;
    const QString& handleColor = tc.handle;
    const QString& textColor  = tc.text;


    QString globalStyle = QString(
        // Base Window & Widget backgrounds
        "QMainWindow, QDialog, QWidget { background-color: %1; color: %8; }"
        "QLabel:disabled, QCheckBox:disabled, QRadioButton:disabled, QGroupBox:disabled, QGroupBox::title:disabled { color: #888888; }"
        // Toolbars and Borders
        "QToolBar, QMenuBar { background-color: %3; border-bottom: 1px solid %2; color: %8; }"
        "QMenu { background-color: %3; border: 1px solid %2; color: %8; }"
        "QMenu::item:selected { background-color: %4; color: %5; }"
        "QMenu::item:disabled { color: #888888; }"
        // Buttons
        "QPushButton { background-color: %3; color: %8; border: 1px solid %2; border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
        "QPushButton:hover { background-color: %4; border-color: %5; }"
        "QPushButton:pressed { background-color: %5; color: %1; }"
        "QPushButton:disabled { background-color: %1; color: #888888; border: 1px solid %2; }"
        // Tables / Grids
        "QTableWidget, QTableView { background-color: %1; alternate-background-color: %3; color: %8; gridline-color: %2; border: 1px solid %2; }"
        "QHeaderView::section { background-color: %3; color: %8; padding: 4px; border: 1px solid %2; }"
        "QTableWidget::item:selected { background-color: %4; color: %5; }"
        // Sliders (For Analysis View)
        "QSlider::groove:horizontal { height: 4px; background: %2; border-radius: 2px; }"
        "QSlider::groove:horizontal:disabled { background: %1; border: 1px solid %2; }"
        "QSlider::handle:horizontal { width: 12px; height: 12px; margin: -4px 0; background: %7; border-radius: 6px; }"
        "QSlider::handle:horizontal:hover { transform: scale(1.2); background: %5; }"
        "QSlider::handle:horizontal:disabled { background: %2; }"
        "QSlider::sub-page:horizontal { background: %6; border-radius: 2px; }"
        "QSlider::sub-page:horizontal:disabled { background: #888888; border-radius: 2px; }"
        "QSlider::add-page:horizontal { background: %2; border-radius: 2px; }"
        // Inputs
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background-color: %3; color: %8; border: 1px solid %2; border-radius: 2px; padding: 2px 4px; }"
        "QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { background-color: %1; color: #888888; border: 1px solid %2; }"
        "QComboBox::drop-down { border-left: 1px solid %2; }"
        // SpinBox buttons — must be styled explicitly when background-color is set
        "QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 16px; border-left: 1px solid %2; background-color: %3; }"
        "QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 16px; border-left: 1px solid %2; border-top: 1px solid %2; background-color: %3; }"
        "QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover { background-color: %4; }"
        "QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background-color: %4; }"
        "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(:/assets/icons/arrow_up.svg); width: 8px; height: 8px; }"
        "QSpinBox::up-arrow:disabled, QDoubleSpinBox::up-arrow:disabled { image: url(:/assets/icons/arrow_up_disabled.svg); }"
        "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(:/assets/icons/arrow_down.svg); width: 8px; height: 8px; }"
        "QSpinBox::down-arrow:disabled, QDoubleSpinBox::down-arrow:disabled { image: url(:/assets/icons/arrow_down_disabled.svg); }"
        // Specific Overrides for Playback Panel to match theme instead of staying transparent
        "QWidget#playbackPanel QPushButton { background-color: %3; border: 1px solid %2; padding: 4px; border-radius: 4px; }"
        "QWidget#playbackPanel QPushButton:hover { background-color: %4; border-color: %5; }"
        "QWidget#playbackPanel QPushButton[active=\"true\"] { background-color: %5; color: %1; }"
        "QWidget#playbackPanel { border-top: 1px solid %2; }"
    ).arg(bgColor, borderColor, btnBg, btnHover, primaryColor, sliderBg, handleColor, textColor);

    qApp->setStyleSheet(globalStyle);
    
    // Sub-components that manage their own local stylesheets using theme variables
    if (liveDashboard_) liveDashboard_->updateTheme();
    if (analysisView_) analysisView_->updateTheme();
}

