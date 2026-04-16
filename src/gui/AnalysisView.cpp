#include "AnalysisView.h"
#include <QKeyEvent>
#include "widgets/AnalysisVideoWidget.h"
#include "../config/CameraConfig.h"
#include "../core/EventController.h"
#include "../core/EventDatabase.h"
#include "../core/EventDatabase.h"
#include "../core/VideoStreamReader.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QDateTime>
#include <QTimer>
#include <QIcon>
#include <QMetaObject>
#include <QFileInfo>
#include <QDir>
#include <QShortcut>
#include <QMenu>
#include <QKeySequence>

// Helper: build paperBreakTable stylesheet from theme colors
static QString makeTableStyle(const ThemeColors& tc, bool deleteMode) {
    QString selNormal  = QString("QTableWidget::item:selected { background-color: %1; color: white; }")
                         .arg(tc.btnHover);
    QString selDelete  = "QTableWidget::item:selected { background-color: #D32F2F; color: white; }";
    return QString(
        "QTableWidget { background-color: %1; alternate-background-color: %2; color: #E0E0E0; "
        "gridline-color: #2a2a2a; font-size: 11px; font-family: sans-serif; border: none; outline: none; }"
        "QHeaderView::section { background-color: %1; color: #E0E0E0; padding: 3px 4px; border: none; border-bottom: 1px solid #2a2a2a; text-align: left; font-size: 11px; font-family: sans-serif; }"
        "QHeaderView::section:checked, QHeaderView::section:pressed, "
        "QHeaderView::section:hover, QHeaderView::section:disabled "
        "{ background-color: %1; color: #E0E0E0; }"
        "QTableWidget::item { padding: 1px 4px; border: none; color: #E0E0E0; font-size: 11px; font-family: sans-serif; }"
    ).arg(tc.bg, tc.btnBg)
     + (deleteMode ? selDelete : selNormal);
}

AnalysisView::AnalysisView(int numCameras, QWidget *parent) 
    : QWidget(parent), numCameras_(numCameras), selectedCameraId_(-1),
      serverRunning_(false), isRecording_(false), isPlaying_(false), isReviewMode_(false),
      currentFrame_(0), totalFrames_(1000), playbackSpeed_(1.0), triggerFrameIndex_(0),
      isStreamingMode_(false), baseWidth_(782), baseHeight_(582) {
    
    // Readers initialized per camera dynamically
    
    // Setup playback timer
    playbackTimer_ = new QTimer(this);
    connect(playbackTimer_, &QTimer::timeout, this, &AnalysisView::onPlaybackTick);
    
    setupUI();
    
    // Initialize EventDatabase and load historical events
    EventDatabase::instance().initialize("../data");
    reloadEventTables();
    selectLatestEvent();
    
    // Register callback for EventController
    // Callback receives metadata only - frames loaded from disk to avoid memory spike
    EventController::instance().setEventSavedCallback([this](const std::string& timestamp, int triggerIndex, int totalFrames) {
        QMetaObject::invokeMethod(this, [this, timestamp, triggerIndex, totalFrames]() {
            addPaperBreakEvent(timestamp, triggerIndex, totalFrames);
        }, Qt::QueuedConnection);
    });
}

AnalysisView::~AnalysisView() {}

void AnalysisView::setupUI() {
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);
    
    // Top Controls Layout (for Delete Enable Toggle)
    auto topControlsLayout = new QHBoxLayout();
    topControlsLayout->setContentsMargins(0, 0, 0, 0);
    
    auto deleteLabel = new QLabel("Enable Event Deletion:", this);
    enableDeleteCheck_ = new ToggleSwitch(this);
    enableDeleteCheck_->setEnabled(false); // Locked by default
    connect(enableDeleteCheck_, &ToggleSwitch::toggled, this, &AnalysisView::setDeleteEnabled);
    
    // Add stretch on left to push it to the right, or keep it left-aligned?
    // User mockup shows it near the top left or right depending on tabs. Let's keep it left.
    topControlsLayout->addWidget(deleteLabel);
    topControlsLayout->addWidget(enableDeleteCheck_);
    topControlsLayout->addStretch();
    
    mainLayout->addLayout(topControlsLayout);
    
    // Create main splitter (horizontal: left sidebar | main area)
    mainSplitter_ = new QSplitter(Qt::Horizontal, this);
    mainSplitter_->setHandleWidth(3);
    
    // Setup components
    setupLeftSidebar();
    
    // Setup playback controls first (so mainArea can use them)
    setupPlaybackControls();
    
    setupMainArea();
    
    mainSplitter_->addWidget(leftSidebar_);
    mainSplitter_->addWidget(mainArea_);
    mainSplitter_->setStretchFactor(0, 0);  // Left sidebar fixed
    mainSplitter_->setStretchFactor(1, 1);  // Main area stretches
    mainSplitter_->setSizes({220, 800});    // Initial sizes
    
    mainLayout->addWidget(mainSplitter_, 1);
    
    // Initialize controls to disabled state (no data yet)
    updatePlaybackControlsState();
}

void AnalysisView::startReviewFromFile(const QString& videoPath, int triggerIndex) {
    std::cout << "[AnalysisView] Starting review from file: " << videoPath.toStdString() << std::endl;
    
    // Check if this is a TIFF directory (ends with _tiff)
    if (videoPath.endsWith("_tiff")) {
        std::cout << "[AnalysisView] Delegating to specialized loader: " << videoPath.toStdString() << std::endl;
        startReview(videoPath, triggerIndex);
        return;
    }
    
    // Open video files for all possible cameras (up to max configured)
    int maxCameras = CameraConfig::getCameraCount();
    videoReaders_.clear();
    
    bool anyOpened = false;
    for (int i = 0; i < maxCameras; ++i) {
        int camId = i + 1;
        QString camPath = videoPath;
        
        if (camPath.contains("_cam")) {
            int idx = camPath.lastIndexOf("_cam");
            camPath = camPath.left(idx) + QString("_cam%1.bin").arg(camId);
        } else if (camPath.endsWith(".bin")) {
             // Legacy fallback - only 1 camera file available
             if (i > 0) continue;
        }

        if (QFile::exists(camPath)) {
            auto reader = std::make_unique<VideoStreamReader>();
            if (reader->open(camPath)) {
                videoReaders_[i] = std::move(reader);
                anyOpened = true;
            }
        }
    }

    if (!anyOpened) {
        std::cerr << "[AnalysisView] Failed to open any video files for event!" << std::endl;
        return;
    }

    // Clear widgets for cameras that have no video file in this event
    // (prevents stale freeze-frames from a previously viewed event)
    for (int i = 0; i < (int)cameraWidgets_.size(); ++i) {
        if (videoReaders_.find(i) == videoReaders_.end()) {
            cameraWidgets_[i]->clear();
        }
    }

    // Switch to streaming mode
    isReviewMode_ = true;
    isStreamingMode_ = true;
    recordedSequence_.clear();  // Clear in-memory sequence as we're loading from disk
    
    // Get video properties from first available reader
    totalFrames_ = 0;
    if (!videoReaders_.empty()) {
        totalFrames_ = videoReaders_.begin()->second->getTotalFrames() - 1;
    }
    
    // Set trigger index
    if (triggerIndex < 0 || triggerIndex > totalFrames_) {
        triggerFrameIndex_ = totalFrames_;
    } else {
        triggerFrameIndex_ = triggerIndex;
    }
    
    // Initial position is at trigger (0 relative)
    currentFrame_ = triggerFrameIndex_;
    
    // Update UI with relative range
    int minRange = -triggerFrameIndex_ * 10;
    int maxRange = (totalFrames_ - triggerFrameIndex_) * 10;
    
    playbackSlider_->setRange(minRange, maxRange);
    playbackSlider_->setValue(0);
    frameInput_->setText("0.0");
    
    // Preload chunk around trigger point for all active readers
    for (auto& pair : videoReaders_) {
        pair.second->preloadChunk(triggerFrameIndex_, 25);
    }
    
    // Update display
    onSliderMoved(0);
    
    // Stop playback initially
    isPlaying_ = false;
    playbackTimer_->stop();
    playPauseButton_->setText("▶");
    
    updatePlaybackControlsState();
    updateSliderZeroMarker();  // Position the zero marker
    
    std::cout << "[AnalysisView] Review loaded from file: " << totalFrames_ + 1 
              << " frames, trigger at " << triggerFrameIndex_ << std::endl;
}

void AnalysisView::setupLeftSidebar() {
    leftSidebar_ = new QWidget(this);
    leftSidebar_->setFixedWidth(280); // Fixed width as requested
    // leftSidebar_->setMaximumWidth(280); // Removed range
    
    auto layout = new QVBoxLayout(leftSidebar_);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(8);

    // Server Controls Group
    auto controlsGroup = new QGroupBox("Control Panel", leftSidebar_);
    controlsGroup->setStyleSheet("QGroupBox { font-weight: bold; font-size: 11px; padding-top: 10px; }");
    auto controlsLayout = new QHBoxLayout(controlsGroup); // Changed to horizontal
    controlsLayout->setSpacing(4);
    controlsLayout->setContentsMargins(4, 8, 4, 4);
    
    ThemeColors tc = CameraConfig::getThemeColors();

    // 1. Server Toggle (FIRST - left side)
    serverButton_ = new QPushButton("Server: OFF", controlsGroup);
    serverButton_->setCheckable(true);
    serverButton_->setToolTip("Toggle Server Connection");
    serverButton_->setStyleSheet(QString(
        "QPushButton { padding: 4px; font-size: 10px; background: %1; color: %2; border-radius: 3px; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:pressed { background: %4; }"
        "QPushButton:checked { background: #4CAF50; }"
        "QPushButton:checked:hover { background: #66BB6A; }"
    ).arg(tc.btnBg, tc.text, tc.btnHover, tc.border));
    connect(serverButton_, &QPushButton::clicked, this, &AnalysisView::onServerButtonClicked);
    
    // 2. Admin Login (SECOND - right side)
    adminButton_ = new QPushButton("Login", controlsGroup);
    adminButton_->setToolTip("Admin Login");
    adminButton_->setStyleSheet(QString(
        "QPushButton { padding: 4px; font-size: 10px; background: %1; color: %2; border-radius: 3px; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:pressed { background: %4; }"
    ).arg(tc.primary, tc.bg, tc.btnHover, tc.border));
    connect(adminButton_, &QPushButton::clicked, this, &AnalysisView::onAdminButtonClicked);
    
    // Server Button Context Menu (for settings)
    serverButton_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(serverButton_, &QPushButton::customContextMenuRequested, [this](const QPoint& pos) {
        QMenu menu;
        // Raw Mode toggle removed - Always Raw now.
        
        QAction* linkAction = menu.addAction("Link All Cameras");
        linkAction->setCheckable(true);
        linkAction->setChecked(true); // Default
        // connect(linkAction, &QAction::toggled, this, &AnalysisView::onLinkCamerasToggled); // TODO: Verify connection if needed
        
        menu.exec(serverButton_->mapToGlobal(pos));
    });

    // Add buttons in order: Server, Login
    controlsLayout->addWidget(serverButton_);
    controlsLayout->addWidget(adminButton_);

    layout->addWidget(controlsGroup);
    
    // Paper Break Log Group
    auto logGroup = new QGroupBox("Paper Break Log", leftSidebar_);
    logGroup->setStyleSheet("QGroupBox { font-weight: bold; font-size: 11px; padding-top: 10px; }");
    auto logLayout = new QVBoxLayout(logGroup);
    logLayout->setContentsMargins(4, 8, 4, 4);
    logLayout->setSpacing(4);
    
    paperBreakTable_ = createLogTable(logGroup, false);
    permanentPaperBreakTable_ = createLogTable(logGroup, false);

    auto normalLabel = new QLabel("Recent Records", logGroup);
    normalLabel->setStyleSheet(QString("color: %1; font-weight: 600;").arg(tc.text));
    logLayout->addWidget(normalLabel);
    logLayout->addWidget(paperBreakTable_);

    togglePermanentTableButton_ = new QPushButton("Show Permanent Storage", logGroup);
    togglePermanentTableButton_->setCheckable(true);
    togglePermanentTableButton_->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 6px; font-weight: 600; }"
        "QPushButton:hover { background-color: %4; }"
        "QPushButton:checked { background-color: %4; }"
    ).arg(tc.btnBg, tc.text, tc.border, tc.btnHover));
    connect(togglePermanentTableButton_, &QPushButton::toggled, this, [this](bool checked) {
        permanentSectionWidget_->setVisible(checked);
        togglePermanentTableButton_->setText(checked ? "Hide Permanent Storage" : "Show Permanent Storage");
        permanentPaperBreakTable_->setSizePolicy(QSizePolicy::Expanding, checked ? QSizePolicy::Expanding : QSizePolicy::Preferred);
        leftSidebar_->updateGeometry();
    });
    logLayout->addWidget(togglePermanentTableButton_);

    permanentSectionWidget_ = new QWidget(logGroup);
    permanentSectionWidget_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto permanentLayout = new QVBoxLayout(permanentSectionWidget_);
    permanentLayout->setContentsMargins(0, 0, 0, 0);
    permanentLayout->setSpacing(2);
    auto permanentLabel = new QLabel("Permanent Storage", permanentSectionWidget_);
    permanentLabel->setStyleSheet(QString("color: %1; font-weight: 600;").arg(tc.primary));
    permanentLayout->addWidget(permanentLabel);
    permanentLayout->addWidget(permanentPaperBreakTable_, 1);
    permanentSectionWidget_->setVisible(false);
    logLayout->addWidget(permanentSectionWidget_);
    
    // Delete Button (Initially Disabled/Hidden)
    auto buttonRow = new QHBoxLayout();

    permanentButton_ = new QPushButton("Mark Permanent", logGroup);
    permanentButton_->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: %2; font-weight: bold; padding: 6px; border-radius: 4px; border: 1px solid %3; }"
        "QPushButton:hover { background-color: %4; }"
        "QPushButton:pressed { background-color: %1; }"
        "QPushButton:disabled { background-color: %5; color: %3; }"
    ).arg(tc.btnBg, tc.text, tc.primary, tc.btnHover, tc.bg));
    permanentButton_->setEnabled(false);
    connect(permanentButton_, &QPushButton::clicked, this, &AnalysisView::onTogglePermanentClicked);
    buttonRow->addWidget(permanentButton_);

    deleteButton_ = new QPushButton("Delete Selected", logGroup);
    deleteButton_->setStyleSheet(QString(
        "QPushButton { background-color: #D32F2F; color: white; font-weight: bold; padding: 6px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #E53935; }"
        "QPushButton:pressed { background-color: #C62828; }"
        "QPushButton:disabled { background-color: %1; color: %2; }"
    ).arg(tc.btnBg, tc.border));
    deleteButton_->setEnabled(false); // Only admin
    deleteButton_->setVisible(false); // Hide until admin mode
    connect(deleteButton_, &QPushButton::clicked, this, &AnalysisView::onDeleteClicked);

    buttonRow->addWidget(deleteButton_);
    logLayout->addLayout(buttonRow);
    
    paperBreakTable_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    permanentPaperBreakTable_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    logLayout->setStretch(1, 3);
    logLayout->setStretch(3, 2);
    layout->addWidget(logGroup, 1);  // Stretches to fill remaining space
    
    // No hidden columns
}

void AnalysisView::setupMainArea() {
    mainArea_ = new QWidget(this);
    auto layout = new QVBoxLayout(mainArea_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    
    // Tab Widget
    tabWidget_ = new QTabWidget(mainArea_);
    ThemeColors tc = CameraConfig::getThemeColors();
    tabWidget_->setStyleSheet(QString(
        "QTabWidget::pane { border: 1px solid %1; background: %2; }"
        "QTabBar::tab { background: %3; color: %4; padding: 8px 16px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: %2; color: %5; border-bottom: 2px solid %6; }"
        "QTabBar::tab:hover { background: %3; }"
    ).arg(tc.border, tc.bg, tc.btnBg, tc.text, tc.text, tc.primary));
    
    // Tab 1: All Camera
    allCameraTab_ = new QWidget();
    setupCameraGrid(allCameraTab_);
    tabWidget_->addTab(allCameraTab_, "All Camera");
    
    // Tab 2: Single Camera (dynamic name)
    singleCameraTab_ = new QWidget();
    auto singleLayout = new QVBoxLayout(singleCameraTab_);
    singleLayout->setContentsMargins(4, 4, 4, 4);
    
    selectedCameraWidget_ = new AnalysisVideoWidget(-1, "Select a camera", singleCameraTab_);
    singleLayout->addWidget(selectedCameraWidget_);
    tabWidget_->addTab(singleCameraTab_, "Camera");
    
    // Tab 3: Diagnostic — all-camera live data table (built once)
    diagnosticTab_ = new QWidget();
    setupDiagnosticTab();
    tabWidget_->addTab(diagnosticTab_, "Diagnostic");

    // Tab 4: Configuration - REMOVED (Moved to System Settings Dialog)
    
    layout->addWidget(tabWidget_, 1);
    
    // Add playback panel here (camera area)
    layout->addWidget(playbackPanel_);

    // Connect slider signals
    // sliderMoved is for dragging (continuous update)
    connect(playbackSlider_, &QSlider::sliderMoved, this, &AnalysisView::onSliderMoved);
    // valueChanged is for clicking (jump) and programmed updates
    connect(playbackSlider_, &QSlider::valueChanged, this, &AnalysisView::onSliderValueChanged);
    
    // Note: onSliderMoved and onSliderValueChanged need to coordinate to avoid double-updates 
    // or loops during playback.

    connect(tabWidget_, &QTabWidget::currentChanged, this, &AnalysisView::onTabChanged);
}

void AnalysisView::setupCameraGrid(QWidget* container) {
    auto layout = new QGridLayout(container);
    layout->setSpacing(4);
    layout->setContentsMargins(4, 4, 4, 4);
    
    // Calculate grid dimensions
    int cols = 4;
    int rows = (numCameras_ + cols - 1) / cols;
    
    for (int i = 0; i < numCameras_; ++i) {
        QString label = CameraConfig::getCameraLabel(i);
        auto widget = new AnalysisVideoWidget(i, label, container);
        connect(widget, &AnalysisVideoWidget::clicked, this, &AnalysisView::onCameraClicked);
        
        int row = i / cols;
        int col = i % cols;
        layout->addWidget(widget, row, col);
        cameraWidgets_.push_back(widget);
    }
    
    // Set equal stretch for all rows and columns
    for (int i = 0; i < rows; ++i) layout->setRowStretch(i, 1);
    for (int i = 0; i < cols; ++i) layout->setColumnStretch(i, 1);
}

void AnalysisView::setCameraCount(int count) {
    if (count == numCameras_) return;
    
    auto layout = qobject_cast<QGridLayout*>(allCameraTab_->layout());
    if (!layout) return;
    
    int cols = 4;
    int rows = (std::max(count, 1) + cols - 1) / cols;
    
    if (count > numCameras_) {
        for (int i = numCameras_; i < count; ++i) {
            QString label = CameraConfig::getCameraLabel(i);
            auto widget = new AnalysisVideoWidget(i, label, allCameraTab_);
            connect(widget, &AnalysisVideoWidget::clicked, this, &AnalysisView::onCameraClicked);
            
            int row = i / cols;
            int col = i % cols;
            layout->addWidget(widget, row, col);
            cameraWidgets_.push_back(widget);
        }
    } else {
        for (int i = numCameras_ - 1; i >= count; --i) {
            auto widget = cameraWidgets_.back();
            layout->removeWidget(widget);
            widget->deleteLater();
            cameraWidgets_.pop_back();
        }
    }
    
    // Clear ALL stretch factors up to a safe upper bound (prevents stale stretches on old rows)
    const int MAX_GRID = 8;
    for (int i = 0; i < MAX_GRID; ++i) layout->setRowStretch(i, 0);
    for (int i = 0; i < MAX_GRID; ++i) layout->setColumnStretch(i, 0);
    
    // Set equal stretch for all active rows and columns
    for (int i = 0; i < rows; ++i) layout->setRowStretch(i, 1);
    for (int i = 0; i < cols; ++i) layout->setColumnStretch(i, 1);
    
    numCameras_ = count;

    if (selectedCameraId_ >= numCameras_) {
        selectedCameraId_ = -1;
    }

    if (diagTable_) {
        diagTable_->setRowCount(numCameras_);
        refreshDiagTable();
    }
}

void AnalysisView::setupPlaybackControls() {
    playbackPanel_ = new QWidget(this);
    playbackPanel_->setObjectName("playbackPanel"); // For CSS specificity
    playbackPanel_->setFixedHeight(55); 
    playbackPanel_->setAutoFillBackground(true); // Force paint
    // Use background-color and ensure contrast. 
    ThemeColors tc = CameraConfig::getThemeColors();
    playbackPanel_->setStyleSheet(QString(
        "QWidget#playbackPanel { background-color: %1; border-top: 1px solid %2; }")
        .arg(tc.bg, tc.border));
    
    auto layout = new QVBoxLayout(playbackPanel_);
    layout->setContentsMargins(16, 4, 16, 4);
    layout->setSpacing(2);
    
    // Playback slider (System Standard)
    // === PLAYBACK CONTROL TOOLBAR (Single Line + SVGs) ===
    auto toolbarLayout = new QHBoxLayout();
    toolbarLayout->setSpacing(8);
    toolbarLayout->setContentsMargins(0, 8, 0, 4); 
    
    // Style for SVG buttons: Transparent background, no borders, icon scales
    QString svgButtonStyle = QString(
        "QPushButton { "
        "   background: transparent; "
        "   border: 1px solid %1; "
        "   border-radius: 4px; "
        "}"
        "QPushButton:hover { "
        "   background-color: %2; "
        "}").arg(tc.border, tc.btnHover);

    // Helper macro for creating SVG buttons
    auto createSvgButton = [&](const QString& iconName, const QString& tooltip) -> QPushButton* {
        QPushButton* btn = new QPushButton(playbackPanel_);
        btn->setIcon(QIcon(":/assets/icons/" + iconName));
        btn->setIconSize(QSize(24, 24));
        btn->setFixedSize(32, 32);
        btn->setToolTip(tooltip);
        btn->setStyleSheet(svgButtonStyle);
        return btn;
    };

    // 1. Speed
    speedButton_ = new QPushButton("1.0x", playbackPanel_);
    speedButton_->setFixedSize(56, 32);
    speedButton_->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; font-weight: bold; } "
        "QPushButton:hover { background-color: %4; }"
    ).arg(tc.btnBg, tc.text, tc.border, tc.btnHover));
    speedMenu_ = new QMenu(speedButton_);
    speedMenu_->addAction("Very Slow (0.25x)")->setData(0.25);
    speedMenu_->addAction("Slow (0.5x)")->setData(0.5);
    speedMenu_->addAction("Normal (1.0x)")->setData(1.0);
    speedMenu_->addAction("Fast (2.0x)")->setData(2.0);
    speedMenu_->addAction("Very Fast (4.0x)")->setData(4.0);
    speedButton_->setMenu(speedMenu_);
    connect(speedMenu_, &QMenu::triggered, this, &AnalysisView::onSpeedChanged);
    toolbarLayout->addWidget(speedButton_);
    
    // 2. Play/Pause
    playPauseButton_ = createSvgButton("Play.svg", "Play/Pause");
    connect(playPauseButton_, &QPushButton::clicked, this, &AnalysisView::onPlayPauseClicked);
    toolbarLayout->addWidget(playPauseButton_);

    // 3. Go to Start
    beginButton_ = createSvgButton("Go to Start.svg", "Go to Start");
    connect(beginButton_, &QPushButton::clicked, this, &AnalysisView::onBeginClicked);
    toolbarLayout->addWidget(beginButton_);

    // 4. Step Back
    prevButton_ = createSvgButton("Step Back.svg", "Step Back");
    prevButton_->setAutoRepeat(true);
    connect(prevButton_, &QPushButton::pressed, this, &AnalysisView::onPreviousPressed);
    connect(prevButton_, &QPushButton::released, this, &AnalysisView::onPreviousReleased);
    toolbarLayout->addWidget(prevButton_);

    // 5. Jump to Trigger
    resetButton_ = createSvgButton("Jump to Trigger.svg", "Jump to Trigger");
    connect(resetButton_, &QPushButton::clicked, this, &AnalysisView::onResetClicked);
    toolbarLayout->addWidget(resetButton_);

    // 6. Frame Input
    toolbarLayout->addSpacing(8);
    QLabel* frameLabel = new QLabel("Frame:", playbackPanel_);
    frameLabel->setStyleSheet(QString("color: %1; font-size: 13px; margin-right: 2px;").arg(tc.text));
    toolbarLayout->addWidget(frameLabel);
    
    frameInput_ = new QLineEdit("0.0", playbackPanel_);
    frameInput_->setFixedSize(50, 24);
    frameInput_->setAlignment(Qt::AlignCenter);
    frameInput_->setStyleSheet(QString(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 0px;}"
    ).arg(tc.bg, tc.text, tc.border));
    connect(frameInput_, &QLineEdit::editingFinished, this, &AnalysisView::onFrameInputChanged);
    toolbarLayout->addWidget(frameInput_);

    // 7. Step Forward
    nextButton_ = createSvgButton("Step Forward.svg", "Step Forward");
    nextButton_->setAutoRepeat(true);
    connect(nextButton_, &QPushButton::pressed, this, &AnalysisView::onNextPressed);
    connect(nextButton_, &QPushButton::released, this, &AnalysisView::onNextReleased);
    toolbarLayout->addWidget(nextButton_);

    // 8. Go to End
    endButton_ = createSvgButton("Go to End.svg", "Go to End");
    connect(endButton_, &QPushButton::clicked, this, &AnalysisView::onEndClicked);
    toolbarLayout->addWidget(endButton_);
    
    // 9. Slider in the middle/end
    toolbarLayout->addSpacing(16);
    playbackSlider_ = new QSlider(Qt::Horizontal, playbackPanel_);
    playbackSlider_->setRange(0, 10000); // Deciseconds essentially (1000.0)
    connect(playbackSlider_, &QSlider::sliderMoved, this, &AnalysisView::onSliderMoved);
    connect(playbackSlider_, &QSlider::valueChanged, this, &AnalysisView::onSliderValueChanged);
    toolbarLayout->addWidget(playbackSlider_, 1); // Stretch factor 1
    
    // Zero Marker (We still track it, but attach it to layout properly later or manually position)
    sliderZeroMarker_ = new QLabel(playbackPanel_);
    QPixmap pm(":/assets/icons/Zero Marker.svg");
    sliderZeroMarker_->setPixmap(pm.scaled(12, 12, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    sliderZeroMarker_->setFixedSize(12, 12);
    sliderZeroMarker_->raise();
    sliderZeroMarker_->show();
    
    layout->addLayout(toolbarLayout);
}

// Slot implementations
void AnalysisView::onServerButtonClicked() {
    serverRunning_ = serverButton_->isChecked();
    serverButton_->setText(serverRunning_ ? "Stop Server" : "Start Server");
    emit serverToggled(serverRunning_);
}

// void AnalysisView::onRawModeToggled(bool enabled) { ... } // Removed

void AnalysisView::onDeleteClicked() {
    // Security check: Only allow if delete mode is enabled
    if (!deleteButton_ || !deleteButton_->isEnabled()) {
        return; 
    }

    std::cout << "[AnalysisView] Delete button clicked" << std::endl;
    // Multi-delete support
    // Use QList for Qt container compatibility
    QList<int> rowsToDelete;
    QList<QString> timestampsToDelete;
    
    QTableWidget* activeTable = !paperBreakTable_->selectedItems().isEmpty()
        ? paperBreakTable_
        : permanentPaperBreakTable_;
    int rowCount = activeTable->rowCount();
    std::cout << "[AnalysisView] Scanning " << rowCount << " rows for marked items..." << std::endl;
    
    QList<QTableWidgetItem*> selected = activeTable->selectedItems();
    QSet<int> uniqueRows;
    for (auto* item : selected) {
        uniqueRows.insert(item->row());
    }
    
    if (uniqueRows.isEmpty()) {
        QMessageBox::information(this, "Delete", "Please select items to delete.");
        return;
    }

    for (int row : uniqueRows) {
        QTableWidgetItem* item = activeTable->item(row, 0); 
        if (item) {
            QString ts = item->data(Qt::UserRole).toString();
            if (!ts.isEmpty()) {
                rowsToDelete.append(row);
                timestampsToDelete.append(ts);
                std::cout << "[AnalysisView] Row " << row << " SELECTED. TS: " << ts.toStdString() << std::endl;
            }
        }
    }
    
    if (timestampsToDelete.isEmpty()) {
        return;
    }
    
    // Confirm
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Confirm Delete", 
                                QString("Are you sure you want to delete %1 event(s)?").arg(timestampsToDelete.size()),
                                QMessageBox::Yes|QMessageBox::No);
                                
    if (reply == QMessageBox::Yes) {
        std::cout << "[AnalysisView] Confirmed. Deleting..." << std::endl;
        // Proceed with deletion
        // We must delete from database/disk first
        for (const QString& ts : timestampsToDelete) {
            bool ok = EventDatabase::instance().deleteEvent(ts);
            std::cout << "[AnalysisView] Deleted " << ts.toStdString() << ": " << (ok ? "OK" : "FAIL") << std::endl;
        }
        
        // Remove rows from table (must do in reverse order to keep indices valid)
        // Sort descending
        std::sort(rowsToDelete.begin(), rowsToDelete.end(), std::greater<int>());
        for (int row : rowsToDelete) {
            std::cout << "[AnalysisView] Removing row " << row << std::endl;
            activeTable->removeRow(row);
        }
        
        // Clear the data view immediately
        clearData();
    }
}

void AnalysisView::onTogglePermanentClicked() {
    QTableWidget* sourceTable = paperBreakTable_->selectedItems().isEmpty() ? permanentPaperBreakTable_ : paperBreakTable_;
    QList<QTableWidgetItem*> selected = sourceTable->selectedItems();
    QSet<int> uniqueRows;
    for (auto* item : selected) {
        uniqueRows.insert(item->row());
    }

    if (uniqueRows.isEmpty()) {
        QMessageBox::information(this, "Permanent Storage", "Please select a record first.");
        return;
    }

    bool anyChanged = false;
    for (int row : uniqueRows) {
        QTableWidgetItem* timeItem = sourceTable->item(row, 0);
        QTableWidgetItem* reasonItem = sourceTable->item(row, 1);
        if (!timeItem || !reasonItem) {
            continue;
        }

        const QString timestamp = timeItem->data(Qt::UserRole).toString();
        const bool currentlyPermanent = timeItem->data(Qt::UserRole + 2).toBool();
        const bool nextPermanent = !currentlyPermanent;
        if (!EventDatabase::instance().setPermanent(timestamp, nextPermanent)) {
            continue;
        }

        timeItem->setData(Qt::UserRole + 2, nextPermanent);
        reasonItem->setData(Qt::UserRole + 2, nextPermanent);
        anyChanged = true;
    }

    if (anyChanged) {
        moveSelectedRowsToTable(sourceTable, sourceTable == paperBreakTable_ ? permanentPaperBreakTable_ : paperBreakTable_, sourceTable == paperBreakTable_);
        updatePermanentButtonLabel();
    }
}

void AnalysisView::onAdminButtonClicked() {
    emit adminLoginRequested();
}

void AnalysisView::onLogSelected(int row, int col) {
    // Determine if we should load the event
    // If clicking checkbox (col 0), just toggle check state (already handled by widget)
    // We don't want to load video on every check
    // Get event timestamp from table item data
    QTableWidget* sourceTable = qobject_cast<QTableWidget*>(sender());
    if (!sourceTable) {
        sourceTable = paperBreakTable_->hasFocus() ? paperBreakTable_ : permanentPaperBreakTable_;
    }
    QTableWidgetItem* item = sourceTable->item(row, 0);
    if (!item) return;
    
    QString timestamp = item->data(Qt::UserRole).toString();
    
    if (timestamp.isEmpty()) {
        std::cerr << "[AnalysisView] No timestamp data for selected event" << std::endl;
        return;
    }
    
    try {
        // Get event info from database
        auto eventInfo = EventDatabase::instance().getEventInfo(timestamp);
        
        std::cout << "[AnalysisView] Loading event: " << timestamp.toStdString() 
                  << " (" << eventInfo.totalFrames << " frames)" << std::endl;
        
        // Load from file (on-demand)
        startReviewFromFile(eventInfo.videoPath, eventInfo.triggerIndex);
        
    } catch (const std::exception& e) {
        std::cerr << "[AnalysisView] Failed to load event: " << e.what() << std::endl;
    }
}

void AnalysisView::onCameraClicked(int cameraId) {
    std::cout << "[AnalysisView] onCameraClicked: " << cameraId << std::endl;
    selectedCameraId_ = cameraId;
    updateDynamicTab(cameraId);
    tabWidget_->setCurrentIndex(1);  // Switch to single camera tab
}

void AnalysisView::updateDynamicTab(int cameraId) {
    if (cameraId < 0 || cameraId >= numCameras_) {
        tabWidget_->setTabText(1, "Camera");
        if (selectedCameraWidget_) {
            delete selectedCameraWidget_;
            selectedCameraWidget_ = nullptr;
        }
        return;
    }

    QString label = CameraConfig::getCameraLabel(cameraId);
    tabWidget_->setTabText(1, label);
    
    // Update the single camera view
    if (selectedCameraWidget_) {
        delete selectedCameraWidget_;
    }
    selectedCameraWidget_ = new AnalysisVideoWidget(cameraId, label, singleCameraTab_);
    auto layout = qobject_cast<QVBoxLayout*>(singleCameraTab_->layout());
    if (layout) {
        layout->addWidget(selectedCameraWidget_);
    }
    // Diagnostic tab is now a standalone all-camera table; no per-camera rebuild needed.
}

void AnalysisView::onTabChanged(int index) {
    // Diagnostic tab is index 2 — start/stop timer to save resources
    if (diagRefreshTimer_ && diagAutoRefreshChk_) {
        if (index == 2 && diagAutoRefreshChk_->isChecked()) {
            refreshDiagTable();        // immediate refresh on switch
            diagRefreshTimer_->start();
        } else {
            diagRefreshTimer_->stop();
        }
    }
    // Force update of the view when switching tabs to ensure the new widget is painted
    if (isReviewMode_) {
        // Use current slider value to trigger update
        onSliderMoved(playbackSlider_->value());
    }
}

void AnalysisView::onSliderMoved(int value) {
    // Value is relative to trigger (e.g. -200 to +50)
    // Absolute frame index = value/10 + triggerFrameIndex
    double relativeFrame = value / 10.0;
    currentFrame_ = relativeFrame + triggerFrameIndex_;
    
    // Clamp currentFrame_ to valid range [0, totalFrames_]
    if (currentFrame_ < 0) currentFrame_ = 0;
    if (currentFrame_ > totalFrames_) currentFrame_ = totalFrames_;
    
    // Update input display to show relative frame
    frameInput_->setText(QString::number(relativeFrame, 'f', 1));
    
    // In Review Mode, immediate update is needed when dragging slider
    if (isReviewMode_) {
        // Get consistent metadata text
        double relFrame = currentFrame_ - triggerFrameIndex_;
        QString overlayText = getMetadataOverlayText(static_cast<int>(currentFrame_), relFrame);
        QString tooltipText = getMetadataTooltip(static_cast<int>(currentFrame_), relFrame);
        
        if (isStreamingMode_) {
            // On-demand loading from video file
            int idx = qBound(0, static_cast<int>(currentFrame_), static_cast<int>(totalFrames_ - 1));
            
            for (auto& pair : videoReaders_) {
                int camIdx = pair.first;
                cv::Mat cvFrame = pair.second->getFrame(idx);
                
                if (!cvFrame.empty() && camIdx < static_cast<int>(cameraWidgets_.size())) {
                    // Convert Mat to QImage safely without crashing on Mono8
                    cv::Mat rgb;
                    if (cvFrame.channels() == 1) {
                        cv::cvtColor(cvFrame, rgb, cv::COLOR_GRAY2RGB);
                    } else if (cvFrame.channels() == 3) {
                        cv::cvtColor(cvFrame, rgb, cv::COLOR_BGR2RGB);
                    } else {
                        rgb = cvFrame.clone(); 
                    }
                    QImage frameImage(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
                    
                    // Directly update the widget instead of using updateCameraFrame (which aborts in review mode)
                    QImage finalImage = frameImage.copy();
                    cameraWidgets_[camIdx]->setFrame(finalImage);
                    cameraWidgets_[camIdx]->setTimestamp(overlayText, tooltipText);
                    
                    if (selectedCameraWidget_ && selectedCameraWidget_->getCameraId() == camIdx) {
                        selectedCameraWidget_->setFrame(finalImage);
                        selectedCameraWidget_->setTimestamp(overlayText, tooltipText);
                    }
                }
            }
        } else if (!recordedSequence_.empty()) {
            QImage frameImage;
            // Load from in-memory sequence
            int idx = qBound(0, static_cast<int>(currentFrame_), static_cast<int>(recordedSequence_.size()) - 1);
            frameImage = recordedSequence_[idx];
            
            if (!frameImage.isNull()) {
                // Check if this is a tiled image (multi-camera recording)
                bool isTiled = (frameImage.width() > baseWidth_ || frameImage.height() > baseHeight_);
                
                if (isTiled) {
                    int cols = 4;
                    int rows = (numCameras_ + cols - 1) / cols;
                    int cellW = frameImage.width() / cols;
                    int cellH = frameImage.height() / rows;
                    
                    for (int i = 0; i < numCameras_; ++i) {
                        int r = i / cols;
                        int c = i % cols;
                        QImage slice = frameImage.copy(c * cellW, r * cellH, cellW, cellH);
                        
                        if (i < (int)cameraWidgets_.size()) {
                            cameraWidgets_[i]->setFrame(slice);
                            cameraWidgets_[i]->setTimestamp(overlayText, tooltipText);
                        }
                    }
                    
                    // Update selected camera widget with its specific slice
                    if (selectedCameraWidget_) {
                        int scId = selectedCameraWidget_->getCameraId();
                        if (scId >= 0 && scId < numCameras_) {
                            int r = scId / cols;
                            int c = scId % cols;
                            QImage slice = frameImage.copy(c * cellW, r * cellH, cellW, cellH);
                            selectedCameraWidget_->setFrame(slice);
                            selectedCameraWidget_->setTimestamp(overlayText, tooltipText);
                        }
                    }
                } else {
                    // Single-camera recording: only update camera widget 0
                    if (!cameraWidgets_.empty()) {
                        cameraWidgets_[0]->setFrame(frameImage);
                        cameraWidgets_[0]->setTimestamp(overlayText, tooltipText);
                    }
                    // Clear all other camera slots so they don't show stale or duplicate data
                    for (int wi = 1; wi < static_cast<int>(cameraWidgets_.size()); ++wi) {
                        cameraWidgets_[wi]->clear();
                    }
                    
                    if (selectedCameraWidget_) {
                        selectedCameraWidget_->setFrame(frameImage);
                        selectedCameraWidget_->setTimestamp(overlayText, tooltipText);
                    }
                }
            }
        }
    }
}

QString AnalysisView::getMetadataOverlayText(int frameIndex, double relativeFrame) {
    if (frameIndex < 0 || frameIndex >= (int)frameMetadata_.size()) {
        return QString("REC: %1").arg(relativeFrame, 0, 'f', 1);
    }

    const auto& meta = frameMetadata_[frameIndex];
    QString text = QString("REC: %1").arg(relativeFrame, 0, 'f', 1); // 1 decimal for relative
    
    // Add Hardware Timestamp and Frame Counter
    text += QString("  |  TS: %1  |  FC: %2").arg(meta.displayTime).arg(meta.frameCounter);
    
    // Add Calculated Real-World Time if valid
    if (eventBaseTime_.isValid() && triggerFrameIndex_ >= 0 && triggerFrameIndex_ < (int)frameMetadata_.size()) {
        int64_t triggerTs = frameMetadata_[triggerFrameIndex_].timestamp;
        int64_t currentTs = meta.timestamp;
        int64_t diffNs = currentTs - triggerTs;
        
        QDateTime realTime = eventBaseTime_.addMSecs(diffNs / 1000000);
        text += QString("  |  Time: %1").arg(realTime.toString("HH:mm:ss.zzz"));
    }
    
    return text;
}

QString AnalysisView::getMetadataTooltip(int frameIndex, double relativeFrame) {
    QString tip = QString("REC: %1s\n(Relative time from trigger event)").arg(relativeFrame, 0, 'f', 1);
    
    if (frameIndex >= 0 && frameIndex < (int)frameMetadata_.size()) {
        const auto& meta = frameMetadata_[frameIndex];
        tip += QString("\n\nTS: %1\n(Hardware Timestamp from Camera)").arg(meta.displayTime);
        tip += QString("\n\nFC: %2\n(Hardware Frame Counter)").arg(meta.frameCounter);
    }
    
    // Add Calculated Real-World Time if valid
    if (eventBaseTime_.isValid() && triggerFrameIndex_ >= 0 && triggerFrameIndex_ < (int)frameMetadata_.size() 
        && frameIndex >= 0 && frameIndex < (int)frameMetadata_.size()) {
        int64_t triggerTs = frameMetadata_[triggerFrameIndex_].timestamp;
        int64_t currentTs = frameMetadata_[frameIndex].timestamp;
        int64_t diffNs = currentTs - triggerTs;
        
        QDateTime realTime = eventBaseTime_.addMSecs(diffNs / 1000000);
        tip += QString("\n\nTime: %1\n(Calculated Real-World Time)").arg(realTime.toString("yyyy-MM-dd HH:mm:ss.zzz"));
    }

    // Add Resolution
    tip += QString("\n\nResolution: %1x%2").arg(baseWidth_).arg(baseHeight_);
    
    return tip;
}

void AnalysisView::onSliderValueChanged(int value) {
    if (!isPlaying_ && abs(playbackSlider_->sliderPosition() - value) > 1) {
        // If not playing and jump was significant (user clicked), force update
        onSliderMoved(value);
    }
}

void AnalysisView::onPlayPauseClicked() {
    isPlaying_ = !isPlaying_;
    
    // Toggle SVG Icon (assume Play-Pause.svg handles state visually, or we toggle if we had separate SVGs, but we only have Play-Pause.svg)
    // Actually, "Play-Pause.svg" is a static icon. If we want it to look "active", we'll just keep the icon but change the background style slightly if needed, or rely on hover states.
    // The requirement is to use "Play-Pause.svg". So we just toggle the timer state without changing text.
    
    if (isPlaying_) {
        playPauseButton_->setIcon(QIcon(":/assets/icons/Pause.svg"));
        
        int interval = static_cast<int>(33.0 / playbackSpeed_);  // 33ms = ~30fps
        playbackTimer_->start(interval);
    } else {
        playPauseButton_->setIcon(QIcon(":/assets/icons/Play.svg"));
        
        playbackTimer_->stop();
    }
}

void AnalysisView::onBeginClicked() {
    currentFrame_ = 0; // Go to absolute start
    double relativeFrame = currentFrame_ - triggerFrameIndex_;
    playbackSlider_->setValue(static_cast<int>(relativeFrame * 10));
    frameInput_->setText(QString::number(relativeFrame, 'f', 1));
    onSliderMoved(static_cast<int>(relativeFrame * 10)); // Force update
}

void AnalysisView::onPreviousPressed() {
    currentFrame_ = qMax(0.0, currentFrame_ - playbackSpeed_); // Step by playback speed
    double relativeFrame = currentFrame_ - triggerFrameIndex_;
    playbackSlider_->setValue(static_cast<int>(relativeFrame * 10));
    frameInput_->setText(QString::number(relativeFrame, 'f', 1));
    onSliderMoved(static_cast<int>(relativeFrame * 10));
}

void AnalysisView::onPreviousReleased() {}

void AnalysisView::onResetClicked() {
    // Reset to Trigger Point (0 relative)
    currentFrame_ = triggerFrameIndex_;
    playbackSlider_->setValue(0);
    frameInput_->setText("0.0");
    onSliderMoved(0);
}

void AnalysisView::onNextPressed() {
    currentFrame_ = qMin(static_cast<double>(totalFrames_), currentFrame_ + playbackSpeed_); // Step by playback speed
    double relativeFrame = currentFrame_ - triggerFrameIndex_;
    playbackSlider_->setValue(static_cast<int>(relativeFrame * 10));
    frameInput_->setText(QString::number(relativeFrame, 'f', 1));
    onSliderMoved(static_cast<int>(relativeFrame * 10));
}

void AnalysisView::onNextReleased() {}

void AnalysisView::onEndClicked() {
    currentFrame_ = totalFrames_;
    double relativeFrame = currentFrame_ - triggerFrameIndex_;
    playbackSlider_->setValue(static_cast<int>(relativeFrame * 10));
    frameInput_->setText(QString::number(relativeFrame, 'f', 1));
    onSliderMoved(static_cast<int>(relativeFrame * 10));
}

void AnalysisView::onFrameInputChanged() {
    bool ok;
    double relativeValue = frameInput_->text().toDouble(&ok);
    if (ok) {
        // Convert relative value to absolute frame
        double absoluteFrame = relativeValue + triggerFrameIndex_;
        currentFrame_ = qBound(0.0, absoluteFrame, totalFrames_);
        
        // Update slider with relative value (re-calculated from bound absolute)
        double boundRelative = currentFrame_ - triggerFrameIndex_;
        playbackSlider_->setValue(static_cast<int>(boundRelative * 10));
        
        // Force update view
        onSliderMoved(static_cast<int>(boundRelative * 10));
    }
}

void AnalysisView::onSpeedChanged(QAction* action) {
    playbackSpeed_ = action->data().toDouble();
    
    // The SVGs for speed are incomplete (e.g., missing speed_1.0x.svg).
    // Instead of using messy fallback icons, we standardise the typography.
    // The global stylesheet handles its look and feel cleanly.
    speedButton_->setIcon(QIcon()); // Clear any broken icon
    speedButton_->setText(QString("%1x").arg(playbackSpeed_, 0, 'f', 1));
    
    // Update timer interval if playing
    if (isPlaying_) {
        int interval = static_cast<int>(33.0 / playbackSpeed_);
        playbackTimer_->start(interval);
    }
}



void AnalysisView::startReview(const QString& path, int triggerIndex) {
    // 1. RAW BINARY PATH
    if (path.endsWith(".bin")) {
        loadRawSequence(path);
        
        // Restore/Set Trigger Index
        triggerFrameIndex_ = (triggerIndex >= 0) ? triggerIndex : 0;
        
        // Reset UI
        isReviewMode_ = true;
        isStreamingMode_ = false; 
        isPlaying_ = false;
        
        // Update Video Slider bounds
        totalFrames_ = recordedSequence_.size() - 1;
        
        int minRange = -triggerFrameIndex_ * 10;
        int maxRange = (totalFrames_ - triggerFrameIndex_) * 10;
        playbackSlider_->setRange(minRange, maxRange);
        
        updateSliderZeroMarker();
        
        // Go to start (Trigger Frame)
        currentFrame_ = triggerFrameIndex_; 
        onSliderMoved(0); 
        
        updatePlaybackControlsState();
        return;
    }

    // 2. TIFF DIRECTORY PATH (Legacy)
    QDir tiffDir(path);
    if (!tiffDir.exists()) {
        std::cerr << "[AnalysisView] Directory not found: " << path.toStdString() << std::endl;
        return;
    }
    
    // Get all TIFF files sorted by name
    QStringList filters;
    filters << "*.tiff" << "*.tif";
    QFileInfoList tiffFiles = tiffDir.entryInfoList(filters, QDir::Files, QDir::Name);
    
    if (tiffFiles.empty()) {
        std::cerr << "[AnalysisView] No TIFF files found in: " << path.toStdString() << std::endl;
        return;
    }
    
    std::cout << "[AnalysisView] Loading " << tiffFiles.size() << " TIFF frames asynchronously..." << std::endl;
    
    // Store trigger index for later use
    pendingTriggerIndex_ = triggerIndex;
    
    // Create progress dialog
    loadingDialog_ = new QProgressDialog("Loading TIFF frames...", "Cancel", 0, tiffFiles.size(), this);
    loadingDialog_->setWindowTitle("Loading Recording");
    loadingDialog_->setWindowModality(Qt::WindowModal);
    loadingDialog_->setMinimumDuration(0);  // Show immediately
    loadingDialog_->setValue(0);
    
    // Create watcher if not exists
    if (!tiffLoaderWatcher_) {
        tiffLoaderWatcher_ = new QFutureWatcher<QVector<QImage>>(this);
        connect(tiffLoaderWatcher_, &QFutureWatcher<QVector<QImage>>::finished,
                this, &AnalysisView::onTiffLoadingFinished);
    }
    
    // Start async loading in background thread
    QFuture<QVector<QImage>> future = QtConcurrent::run([tiffFiles, this]() -> QVector<QImage> {
        QVector<QImage> frames;
        frames.reserve(tiffFiles.size());
        
        for (int i = 0; i < tiffFiles.size(); ++i) {
            const QFileInfo& fileInfo = tiffFiles.at(i);
            cv::Mat frame = cv::imread(fileInfo.absoluteFilePath().toStdString(), cv::IMREAD_COLOR);
            
            if (!frame.empty()) {
                // Convert to QImage and copy data (detach from cv::Mat)
                QImage qimg(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
                frames.push_back(qimg.rgbSwapped().copy());
            }
            
            // Update progress (thread-safe with QMetaObject)
            QMetaObject::invokeMethod(loadingDialog_, "setValue", Qt::QueuedConnection, Q_ARG(int, i + 1));
            
            // Check for cancellation
            if (loadingDialog_ && loadingDialog_->wasCanceled()) {
                frames.clear();
                break;
            }
        }
        
        return frames;
    });
    
    tiffLoaderWatcher_->setFuture(future);
}

void AnalysisView::onTiffLoadingFinished() {
    // Get loaded frames
    QVector<QImage> frames = tiffLoaderWatcher_->result();
    
    // Close dialog
    if (loadingDialog_) {
        loadingDialog_->close();
        loadingDialog_->deleteLater();
        loadingDialog_ = nullptr;
    }
    
    // Check if loading was cancelled or failed
    if (frames.empty()) {
        std::cerr << "[AnalysisView] TIFF loading cancelled or failed" << std::endl;
        return;
    }
    
    std::cout << "[AnalysisView] Loaded " << frames.size() << " frames successfully" << std::endl;
    
    // Convert QVector to std::vector and store
    isReviewMode_ = true;
    recordedSequence_.clear();
    recordedSequence_.reserve(frames.size());
    for (const QImage& img : frames) {
        recordedSequence_.push_back(img);
    }
    
    // FIX RESOLUTION MISMATCH: Update base dimensions to match loaded content
    // This prevents single high-res frames from being treated as tiled grids
    if (!recordedSequence_.empty()) {
        const QImage& firstFrame = recordedSequence_[0];
        baseWidth_ = firstFrame.width();
        baseHeight_ = firstFrame.height();
        std::cout << "[AnalysisView] Updated reference resolution to " 
                  << baseWidth_ << "x" << baseHeight_ << std::endl;
    }
    
    totalFrames_ = recordedSequence_.size() - 1;
    
    // Set trigger index
    int triggerIndex = pendingTriggerIndex_;
    if (triggerIndex < 0 || triggerIndex > totalFrames_) {
        triggerFrameIndex_ = totalFrames_;
    } else {
        triggerFrameIndex_ = triggerIndex;
    }
    
    // Initial position is at trigger (0 relative)
    currentFrame_ = triggerFrameIndex_;
    
    // Update UI with relative range
    int minRange = -triggerFrameIndex_ * 10;
    int maxRange = (totalFrames_ - triggerFrameIndex_) * 10;
    
    playbackSlider_->setRange(minRange, maxRange);
    playbackSlider_->setValue(0);
    frameInput_->setText("0.0");
    
    // DON'T auto-play - start in paused state
    isPlaying_ = false;
    playPauseButton_->setText("▶");
    playbackTimer_->stop();
    
    // Enable controls now that we have data
    updatePlaybackControlsState();
    updateSliderZeroMarker();
    
    // Force update of the view to show the trigger frame
    onSliderMoved(0);
}


void AnalysisView::updatePlaybackControlsState() {
    bool hasData = !recordedSequence_.empty() || isStreamingMode_;
    
    // Enable/disable all playback controls
    playbackSlider_->setEnabled(hasData);
    playPauseButton_->setEnabled(hasData);
    beginButton_->setEnabled(hasData);
    prevButton_->setEnabled(hasData);
    resetButton_->setEnabled(hasData);
    nextButton_->setEnabled(hasData);
    endButton_->setEnabled(hasData);
    frameInput_->setEnabled(hasData);
    speedButton_->setEnabled(hasData);
    
    // Gray out appearance when disabled, restore theme colors when enabled
    ThemeColors tc = CameraConfig::getThemeColors();
    if (!hasData) {
        playbackPanel_->setStyleSheet(QString(
            "QWidget#playbackPanel { background-color: %1; border-top: 1px solid %2; }"
            "QWidget { color: %3; }"
        ).arg(tc.bg, tc.border, tc.border));
    } else {
        playbackPanel_->setStyleSheet(QString(
            "QWidget#playbackPanel { background-color: %1; border-top: 1px solid %2; }")
            .arg(tc.bg, tc.border));
    }
}

void AnalysisView::setLiveMode() {
    isReviewMode_ = false;
    isRecording_ = false;
    isPlaying_ = false;
    playbackTimer_->stop();
    recordedSequence_.clear();
    
    playPauseButton_->setText("Play");
    
    // Disable controls and clear camera displays
    updatePlaybackControlsState();
    
    // Clear all camera widgets
    for (auto* widget : cameraWidgets_) {
        widget->clear();
    }
    if (selectedCameraWidget_) {
        selectedCameraWidget_->clear();
    }
    
    // Hide zero marker in live mode
    sliderZeroMarker_->hide();
}

void AnalysisView::updateSliderZeroMarker() {
    if (!isReviewMode_) {
        sliderZeroMarker_->hide();
        return;
    }
    
    // Calculate the position where value=0 on the slider
    // Slider geometry: we need to account for the handle offset
    QRect sliderRect = playbackSlider_->geometry();
    int sliderMin = playbackSlider_->minimum();
    int sliderMax = playbackSlider_->maximum();
    
    if (sliderMax == sliderMin) {
        sliderZeroMarker_->hide();
        return;
    }
    
    // Calculate pixel position of value=0
    // Account for slider margins and handle width
    int handleWidth = 12;  // From stylesheet
    int usableWidth = sliderRect.width() - handleWidth;
    int zeroValue = 0;  // The trigger frame is always at value 0
    
    // Map value to pixel position
    float ratio = static_cast<float>(zeroValue - sliderMin) / (sliderMax - sliderMin);
    int xPos = sliderRect.x() + (handleWidth / 2) + static_cast<int>(ratio * usableWidth);
    int yPos = sliderRect.y() - 12;  // Position further above the slider to avoid overlap
    
    // Position and show the marker
    sliderZeroMarker_->move(xPos - 6, yPos);  // Center the 12px wide marker
    sliderZeroMarker_->show();
}

void AnalysisView::updateCameraFrame(int cameraId, const QImage& frame) {
    // In review mode, we ignore live updates
    if (isReviewMode_) return;
    
    if (cameraId >= 0 && cameraId < static_cast<int>(cameraWidgets_.size())) {
        cameraWidgets_[cameraId]->setFrame(frame);
        
        // Update timestamp
        QString ts = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        cameraWidgets_[cameraId]->setTimestamp(ts);
    }
}

void AnalysisView::onPlaybackTick() {
    // Early exit if no data in either mode
    if (recordedSequence_.empty() && !isStreamingMode_) return;
    
    // Advance frame based on speed
    // Timer runs at ~33ms (30fps). To play at 30fps (1.0x), we need 1 frame per tick.
    double step = 1.0 * playbackSpeed_;
    
    currentFrame_ += step;
    
    // Stop at end instead of looping
    if (currentFrame_ >= totalFrames_) {
        currentFrame_ = totalFrames_;
        onPlayPauseClicked(); // Stop playback
        
        // Ensure final frame is shown correctly
        onSliderMoved(playbackSlider_->value());
        return;
    }
    
    // Update UI (Slider value is relative)
    double relativeFrame = currentFrame_ - triggerFrameIndex_;
    
    playbackSlider_->setValue(static_cast<int>(relativeFrame * 10));
    
    // Force view update:
    // In Review Mode, the valueChanged signal triggers onSliderValueChanged,
    // but onSliderValueChanged deliberately ignores updates while playing to prevent
    // slider drag interference. So we must explicitly call onSliderMoved here
    // to fetch and display the new frames.
    onSliderMoved(static_cast<int>(relativeFrame * 10));
}

void AnalysisView::addPaperBreakEvent(const std::string& timestamp, int triggerIndex, int totalFrames) {
    QString rawTs = QString::fromStdString(timestamp);
    reloadEventTables();
    selectLatestEvent();
    
    // Load RAW BINARY from disk (using new per-camera format base)
    QString binPath = QString("../data/event_%1_cam1.bin").arg(rawTs);
    startReviewFromFile(binPath, triggerIndex);
    
}

void AnalysisView::addEventRow(const QString& timestamp, const QString& reason, bool permanent, bool selectRow) {
    QTableWidget* targetTable = permanent ? permanentPaperBreakTable_ : paperBreakTable_;
    const int row = targetTable->rowCount();
    targetTable->insertRow(row);

    QTableWidgetItem* timeItem = new QTableWidgetItem(formatTimestamp(timestamp));
    QTableWidgetItem* reasonItem = new QTableWidgetItem(reason);
    timeItem->setData(Qt::UserRole, timestamp);
    timeItem->setData(Qt::UserRole + 2, permanent);
    reasonItem->setData(Qt::UserRole + 2, permanent);

    targetTable->setItem(row, 0, timeItem);
    targetTable->setItem(row, 1, reasonItem);
    sortLogTable(targetTable);

    if (selectRow) {
        targetTable->selectRow(row);
        targetTable->scrollToItem(timeItem);
    }
}

void AnalysisView::reloadEventTables() {
    const auto events = EventDatabase::instance().getAllEvents();

    std::cout << "[AnalysisView] Loading " << events.size() << " historical events..." << std::endl;

    paperBreakTable_->setRowCount(0);
    permanentPaperBreakTable_->setRowCount(0);

    for (const auto& event : events) {
        QStringList triggers = {"Reel", "Calender", "Press", "Wire"};
        long long seed = 0;
        QString rawTs = event.timestamp;
        for (QChar c : rawTs) {
            if (c.isDigit()) {
                seed += c.digitValue();
            }
        }
        QString randomTrigger = triggers[seed % triggers.size()];

        addEventRow(event.timestamp, randomTrigger, event.permanent, false);
    }

    sortLogTable(paperBreakTable_);
    sortLogTable(permanentPaperBreakTable_);
}


void AnalysisView::loadRawSequence(const QString& binPath) {
    std::ifstream inFile(binPath.toStdString(), std::ios::binary);
    if (!inFile) {
        std::cerr << "[AnalysisView] Failed to open binary file: " << binPath.toStdString() << std::endl;
        QMessageBox::critical(this, "Load Error", "Failed to open event file.");
        return;
    }
    
    // Read Header
    char magic[8];
    int32_t version, width, height, pixelType, frameCount;
    
    inFile.read(magic, 8);
    inFile.read(reinterpret_cast<char*>(&version), 4);
    inFile.read(reinterpret_cast<char*>(&width), 4);
    inFile.read(reinterpret_cast<char*>(&height), 4);
    inFile.read(reinterpret_cast<char*>(&pixelType), 4);
    inFile.read(reinterpret_cast<char*>(&frameCount), 4);
    
    if (strncmp(magic, "PVISION", 7) != 0) {
        std::cerr << "[AnalysisView] Invalid file header." << std::endl;
        return;
    }
    
    std::cout << "[AnalysisView] Loading RAW: " << width << "x" << height << ", " << frameCount << " frames." << std::endl;
    
    // Parse timestamp from filename: event_yyyyMMdd_HHmmss_zzz.bin
    QFileInfo fi(binPath);
    QString baseName = fi.baseName(); // event_20260214_141139_123
    if (baseName.startsWith("event_")) {
        QString tsStr = baseName.mid(6); // 20260214_141139_123
        
        // Try new format first
        eventBaseTime_ = QDateTime::fromString(tsStr, "yyyyMMdd_HHmmss_zzz");
        
        // Legacy fallback
        if (!eventBaseTime_.isValid()) {
             eventBaseTime_ = QDateTime::fromString(tsStr, "yyyyMMdd_HHmmss");
        }
        
        if (eventBaseTime_.isValid()) {
             std::cout << "[AnalysisView] Event Base Time: " << eventBaseTime_.toString("yyyy-MM-dd HH:mm:ss.zzz").toStdString() << std::endl;
        }
    } else {
        eventBaseTime_ = QDateTime(); // Invalid
    }

    // FIX RESOLUTION MISMATCH: Update base dimensions
    baseWidth_ = width;
    baseHeight_ = height;
    std::cout << "[AnalysisView] Updated reference resolution to " << baseWidth_ << "x" << baseHeight_ << std::endl;
    
    // Pre-allocate
    recordedSequence_.clear();
    frameMetadata_.clear();
    recordedSequence_.reserve(frameCount);
    frameMetadata_.reserve(frameCount);
    
    size_t frameSize = width * height * 1; // Mono8
    std::vector<uint8_t> buffer(frameSize);
    
    for (int i = 0; i < frameCount; ++i) {
        int64_t ts, fc;
        inFile.read(reinterpret_cast<char*>(&ts), 8);
        inFile.read(reinterpret_cast<char*>(&fc), 8);
        inFile.read(reinterpret_cast<char*>(buffer.data()), frameSize);
        
        // Store Metadata
        FrameMetadata meta;
        meta.timestamp = ts;
        meta.frameCounter = fc;
        // Format timestamp (just raw for now, or convert if it's epoch)
        meta.displayTime = QString::number(ts); 
        frameMetadata_.push_back(meta);
        
        // Create QImage with explicit bytes-per-line to avoid stride/tilt artifacts
        QImage img(width, height, QImage::Format_Grayscale8);
        // Copy row by row to ensure correct stride alignment
        for (int row = 0; row < height; ++row) {
            memcpy(img.scanLine(row), buffer.data() + row * width, width);
        }
        recordedSequence_.push_back(img);
    }
    
    std::cout << "[AnalysisView] Loaded " << recordedSequence_.size() << " frames." << std::endl;
}

void AnalysisView::setDeleteEnabled(bool enabled) {
    // Enable/Show Delete Button
    if (deleteButton_) {
        deleteButton_->setVisible(enabled);
        deleteButton_->setEnabled(enabled);
    }
    
    // Switch Selection Mode & Stylesheet
    paperBreakTable_->clearSelection();
    permanentPaperBreakTable_->clearSelection();
    
    ThemeColors tc = CameraConfig::getThemeColors();
    if (enabled) {
        // DELETE MODE: RED Selection, Multi-Select (Click to toggle)
        configureLogTable(paperBreakTable_, true);
        configureLogTable(permanentPaperBreakTable_, true);
    } else {
        configureLogTable(paperBreakTable_, false);
        configureLogTable(permanentPaperBreakTable_, false);
    }
    
    std::cout << "[AnalysisView] Delete Mode: " << (enabled ? "ENABLED" : "DISABLED") << std::endl;
    updatePermanentButtonLabel();
}

void AnalysisView::updateTheme() {
    ThemeColors tc = CameraConfig::getThemeColors();
    
    // 1. Sidebar Buttons
    serverButton_->setStyleSheet(QString(
        "QPushButton { padding: 4px; font-size: 10px; background: %1; color: %2; border-radius: 3px; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:pressed { background: %4; }"
        "QPushButton:checked { background: #4CAF50; }"
        "QPushButton:checked:hover { background: #66BB6A; }"
    ).arg(tc.btnBg, tc.text, tc.btnHover, tc.border));
    
    adminButton_->setStyleSheet(QString(
        "QPushButton { padding: 4px; font-size: 10px; background: %1; color: %2; border-radius: 3px; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:pressed { background: %4; }"
    ).arg(tc.primary, tc.bg, tc.btnHover, tc.border));
    
    deleteButton_->setStyleSheet(QString(
        "QPushButton { background-color: #D32F2F; color: white; font-weight: bold; padding: 6px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #E53935; }"
        "QPushButton:pressed { background-color: #C62828; }"
        "QPushButton:disabled { background-color: %1; color: %2; }"
    ).arg(tc.btnBg, tc.border));

    permanentButton_->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: %2; font-weight: bold; padding: 6px; border-radius: 4px; border: 1px solid %3; }"
        "QPushButton:hover { background-color: %4; }"
        "QPushButton:pressed { background-color: %1; }"
        "QPushButton:disabled { background-color: %5; color: %3; }"
    ).arg(tc.btnBg, tc.text, tc.primary, tc.btnHover, tc.bg));
    
    // 2. Tab Widget
    tabWidget_->setStyleSheet(QString(
        "QTabWidget::pane { border: 1px solid %1; background: %2; }"
        "QTabBar::tab { background: %3; color: %4; padding: 8px 16px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: %2; color: %5; border-bottom: 2px solid %6; }"
        "QTabBar::tab:hover { background: %3; }"
    ).arg(tc.border, tc.bg, tc.btnBg, tc.text, tc.text, tc.primary));

    // 3. Playback Panel
    // First, ensure the current disabled/enabled state uses the new colors
    updatePlaybackControlsState();
    
    // 4. SVG Button generic styles
    QString svgButtonStyle = QString(
        "QPushButton { background: transparent; border: none; }"
        "QPushButton:hover { background-color: %1; border-radius: 4px; }"
    ).arg(tc.btnHover);
    playPauseButton_->setStyleSheet(svgButtonStyle);
    beginButton_->setStyleSheet(svgButtonStyle);
    prevButton_->setStyleSheet(svgButtonStyle);
    resetButton_->setStyleSheet(svgButtonStyle);
    nextButton_->setStyleSheet(svgButtonStyle);
    endButton_->setStyleSheet(svgButtonStyle);
    
    // 5. Speed Button & Frame Input
    speedButton_->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; font-weight: bold; } "
        "QPushButton:hover { background-color: %4; }"
    ).arg(tc.btnBg, tc.text, tc.border, tc.btnHover));
    
    frameInput_->setStyleSheet(QString(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 0px;}"
    ).arg(tc.bg, tc.text, tc.border));
    
    // 6. Labels
    if (QLabel* frameLabel = playbackPanel_->findChild<QLabel*>("frameLabel")) {
        frameLabel->setStyleSheet(QString("color: %1; font-size: 13px; margin-right: 2px;").arg(tc.text));
    }
    
    if (diagnosticTab_) {
        // Rebuild the single-camera tab only when a valid camera is selected.
        updateDynamicTab(selectedCameraId_);
    }
    
    togglePermanentTableButton_->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 6px; font-weight: 600; }"
        "QPushButton:hover { background-color: %4; }"
        "QPushButton:checked { background-color: %4; }"
    ).arg(tc.btnBg, tc.text, tc.border, tc.btnHover));

    setDeleteEnabled(deleteButton_->isVisible());
}

void AnalysisView::setPlaybackPosition(double frame) {
    currentFrame_ = frame;
    playbackSlider_->setValue(static_cast<int>(frame * 10));
    frameInput_->setText(QString::number(frame, 'f', 1));
}

void AnalysisView::onLinkCamerasToggled(bool linked) {
    // Placeholder implementation for linking cameras
    // In a real implementation this would synchronize zoom/pan across all camera widgets
    std::cout << "[AnalysisView] Link Cameras toggled: " << (linked ? "ON" : "OFF") << std::endl;
}

void AnalysisView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateSliderZeroMarker();
}

void AnalysisView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    
    // Auto-select latest record whenever we return to this view
    if ((paperBreakTable_ && paperBreakTable_->rowCount() > 0) ||
        (permanentPaperBreakTable_ && permanentPaperBreakTable_->rowCount() > 0)) {
        selectLatestEvent();
        
        // Auto-select Camera 0 and switch to Single View ("Detail View data record")
        // "if trigger record true" -> implied by having rows
        onCameraClicked(0); 
    }
}




void AnalysisView::clearData() {
    std::cout << "[AnalysisView] Clearing data to free memory..." << std::endl;
    
    isReviewMode_ = false;
    isStreamingMode_ = false;
    currentFrame_ = 0;
    triggerFrameIndex_ = 0;
    
    // Clear in-memory sequences
    recordedSequence_.clear();
    frameMetadata_.clear();
    
    // Clear and close video readers
    videoReaders_.clear();
    
    // Reset UI
    playbackSlider_->setValue(0);
    isStreamingMode_ = false;
    isReviewMode_ = false;
    
    // 3. Cancel any async loading
    if (tiffLoaderWatcher_ && tiffLoaderWatcher_->isRunning()) {
        tiffLoaderWatcher_->cancel();
        tiffLoaderWatcher_->waitForFinished();
    }
    
    // 4. Reset UI State
    currentFrame_ = 0;
    totalFrames_ = 1000;
    playbackSlider_->setValue(0);
    playbackSlider_->setRange(0, 10000);
    frameInput_->setText("0.0");
    
    // Clear camera widgets
    QImage empty;
    for (auto* widget : cameraWidgets_) {
        widget->setFrame(empty);
        widget->setTimestamp(""); // Clear timestamp
    }
    if (selectedCameraWidget_) {
        selectedCameraWidget_->setFrame(empty);
        selectedCameraWidget_->setTimestamp(""); // Clear timestamp
    }
    
    std::cout << "[AnalysisView] Data cleared." << std::endl;
}

QString AnalysisView::formatTimestamp(const QString& rawTs) {
    // Convert from yyyyMMdd_HHmmss_zzz to yyyy-MM-dd HH:mm:ss.zzz
    QDateTime dt = QDateTime::fromString(rawTs, "yyyyMMdd_HHmmss_zzz");
    // Fallback for old format
    if (!dt.isValid()) {
        dt = QDateTime::fromString(rawTs, "yyyyMMdd_HHmmss");
    }
    
    return dt.isValid() ? dt.toString("yyyy/MM/dd HH:mm:ss") : rawTs;
}

void AnalysisView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Left) {
        onPreviousPressed();
        onPreviousReleased(); // Simulate single step
        event->accept();
    } else if (event->key() == Qt::Key_Right) {
        onNextPressed();
        onNextReleased(); // Simulate single step
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void AnalysisView::setAdminMode(bool isAdmin) {
    if (enableDeleteCheck_) {
        enableDeleteCheck_->setEnabled(isAdmin);
        if (!isAdmin) {
            enableDeleteCheck_->setChecked(false);
        }
    }

    updatePermanentButtonLabel();
}

void AnalysisView::updatePermanentButtonLabel() {
    if (!permanentButton_) {
        return;
    }

    const QList<QTableWidgetItem*> selected = !paperBreakTable_->selectedItems().isEmpty()
        ? paperBreakTable_->selectedItems()
        : permanentPaperBreakTable_->selectedItems();
    bool hasSelection = !selected.isEmpty();
    bool allPermanent = hasSelection;
    for (auto* item : selected) {
        if (!item->data(Qt::UserRole + 2).toBool()) {
            allPermanent = false;
            break;
        }
    }

    permanentButton_->setEnabled(hasSelection);
    permanentButton_->setText(allPermanent ? "Remove Permanent" : "Mark Permanent");
}

QTableWidget* AnalysisView::createLogTable(QWidget* parent, bool deleteMode) {
    QTableWidget* table = new QTableWidget(0, 2, parent);
    table->setHorizontalHeaderLabels({"Trigger Time", "Reason"});
    configureLogTable(table, deleteMode);
    connectLogTable(table);
    return table;
}

void AnalysisView::configureLogTable(QTableWidget* table, bool deleteMode) {
    if (!table) {
        return;
    }

    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->setColumnWidth(0, 160);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setShowGrid(true);
    table->setSortingEnabled(false);
    table->horizontalHeader()->setSortIndicatorShown(true);
    table->horizontalHeader()->setSectionsClickable(true);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(20);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(deleteMode ? QAbstractItemView::MultiSelection : QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setStyleSheet(makeTableStyle(CameraConfig::getThemeColors(), deleteMode));
}

void AnalysisView::connectLogTable(QTableWidget* table) {
    connect(table, &QTableWidget::itemSelectionChanged, this, [this, table]() {
        if (!table->selectedItems().isEmpty()) {
            if (table == paperBreakTable_) {
                permanentPaperBreakTable_->clearSelection();
            } else if (table == permanentPaperBreakTable_) {
                paperBreakTable_->clearSelection();
            }
        }
        updatePermanentButtonLabel();
    });
    connect(table->horizontalHeader(), &QHeaderView::sectionClicked, this, [this, table](int logicalIndex) {
        if (logicalIndex == 0) {
            Qt::SortOrder order = table->horizontalHeader()->sortIndicatorOrder();
            table->sortByColumn(0, order);
        } else {
            table->horizontalHeader()->setSortIndicator(0, table->horizontalHeader()->sortIndicatorOrder());
        }
    });
    connect(table, &QTableWidget::cellClicked, this, [this, table](int row, int col) {
        if (deleteButton_ && deleteButton_->isEnabled()) {
            return;
        }
        if (table == paperBreakTable_) {
            permanentPaperBreakTable_->clearSelection();
        } else if (table == permanentPaperBreakTable_) {
            paperBreakTable_->clearSelection();
        }
        onLogSelected(row, col);
    });
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table, &QTableWidget::customContextMenuRequested, this, [this, table](const QPoint& pos) {
        if (!deleteButton_ || !deleteButton_->isEnabled()) {
            return;
        }
        ThemeColors lc = CameraConfig::getThemeColors();
        QMenu menu;
        menu.setStyleSheet(QString(
            "QMenu { background-color: %1; color: %2; border: 1px solid %3; }"
            "QMenu::item { padding: 5px 20px; }"
            "QMenu::item:selected { background-color: %4; color: %5; }")
            .arg(lc.btnBg, lc.text, lc.border, lc.btnHover, lc.primary));
        QAction* deleteAction = menu.addAction("Delete Selected");
        connect(deleteAction, &QAction::triggered, this, &AnalysisView::onDeleteClicked);
        menu.exec(table->mapToGlobal(pos));
    });
    auto deleteShortcut = new QShortcut(QKeySequence::Delete, table);
    connect(deleteShortcut, &QShortcut::activated, this, &AnalysisView::onDeleteClicked);
    auto backspaceShortcut = new QShortcut(QKeySequence(Qt::Key_Backspace), table);
    connect(backspaceShortcut, &QShortcut::activated, this, &AnalysisView::onDeleteClicked);
}

void AnalysisView::sortLogTable(QTableWidget* table) {
    if (!table) {
        return;
    }
    table->horizontalHeader()->setSortIndicator(0, Qt::DescendingOrder);
    table->sortByColumn(0, table->horizontalHeader()->sortIndicatorOrder());
}

void AnalysisView::selectLatestEvent() {
    QTableWidget* latestTable = nullptr;
    QString latestTimestamp;
    for (QTableWidget* table : {paperBreakTable_, permanentPaperBreakTable_}) {
        if (!table || table->rowCount() == 0) {
            continue;
        }
        QTableWidgetItem* item = table->item(0, 0);
        if (!item) {
            continue;
        }
        const QString ts = item->data(Qt::UserRole).toString();
        if (latestTable == nullptr || ts > latestTimestamp) {
            latestTable = table;
            latestTimestamp = ts;
        }
    }
    if (!latestTable) {
        return;
    }
    if (latestTable == paperBreakTable_) {
        permanentPaperBreakTable_->clearSelection();
    } else {
        paperBreakTable_->clearSelection();
        if (!togglePermanentTableButton_->isChecked()) {
            togglePermanentTableButton_->setChecked(true);
        }
    }
    latestTable->selectRow(0);
    latestTable->scrollToItem(latestTable->item(0, 0));
    onLogSelected(0, 1);
}

void AnalysisView::moveSelectedRowsToTable(QTableWidget* sourceTable, QTableWidget* targetTable, bool permanent) {
    QList<int> rows;
    QSet<int> uniqueRows;
    for (auto* item : sourceTable->selectedItems()) {
        uniqueRows.insert(item->row());
    }
    for (int row : uniqueRows) {
        rows.append(row);
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows) {
        QTableWidgetItem* timeItem = sourceTable->item(row, 0);
        QTableWidgetItem* reasonItem = sourceTable->item(row, 1);
        if (!timeItem || !reasonItem) {
            continue;
        }
        addEventRow(timeItem->data(Qt::UserRole).toString(), reasonItem->text(), permanent, false);
        sourceTable->removeRow(row);
    }
    sortLogTable(sourceTable);
    sortLogTable(targetTable);
}

// ---------------------------------------------------------------------------
// setCameraManager — called from MainWindow after construction
// ---------------------------------------------------------------------------
void AnalysisView::setCameraManager(CameraManager* manager) {
    cameraManager_ = manager;

    if (diagTable_) {
        refreshDiagTable();
    }

    if (diagRefreshTimer_ && diagAutoRefreshChk_ && diagAutoRefreshChk_->isChecked()
        && tabWidget_ && tabWidget_->currentIndex() == 2) {
        diagRefreshTimer_->start();
    }
}

// ---------------------------------------------------------------------------
// setupDiagnosticTab — build the all-camera live data table once at startup
// ---------------------------------------------------------------------------
void AnalysisView::setupDiagnosticTab() {
    ThemeColors tc = CameraConfig::getThemeColors();

    auto* rootLayout = new QVBoxLayout(diagnosticTab_);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(8);

    // --- Control bar (title + buttons) ---
    auto* controlBar = new QHBoxLayout();
    controlBar->setSpacing(8);

    auto* titleLabel = new QLabel("Camera Diagnostics", diagnosticTab_);
    titleLabel->setStyleSheet(QString(
        "color: %1; font-size: 16px; font-weight: bold;").arg(tc.primary));
    controlBar->addWidget(titleLabel);
    controlBar->addStretch();

    diagAutoRefreshChk_ = new QCheckBox("Auto (3 s)", diagnosticTab_);
    diagAutoRefreshChk_->setChecked(true);
    diagAutoRefreshChk_->setStyleSheet(QString(
        "QCheckBox { color: %1; font-size: 12px; }"
        "QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid %2; border-radius: 3px; background: %3; }"
        "QCheckBox::indicator:checked { background: %4; border-color: %4; }").arg(
            tc.text, tc.border, tc.btnBg, tc.primary));
    controlBar->addWidget(diagAutoRefreshChk_);

    diagRefreshBtn_ = new QPushButton("Refresh", diagnosticTab_);
    diagRefreshBtn_->setFixedHeight(28);
    diagRefreshBtn_->setStyleSheet(QString(
        "QPushButton { background: %1; color: %2; border: none; border-radius: 6px;"
        "  padding: 0 14px; font-size: 12px; font-weight: 600; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:pressed { background: %4; }").arg(
            tc.primary, tc.bg, tc.btnHover, tc.border));
    controlBar->addWidget(diagRefreshBtn_);
    rootLayout->addLayout(controlBar);

    // --- Table ---
    const QStringList headers = {
        "ID", "Name", "Temp (C)", "FPS", "Shutter [us]",
        "Gain", "Gamma", "WDR High", "WDR Low",
        "Buf Frames", "Buf [MB]"
    };

    diagTable_ = new QTableWidget(0, headers.size(), diagnosticTab_);
    diagTable_->setHorizontalHeaderLabels(headers);
    diagTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    diagTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    diagTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    diagTable_->verticalHeader()->setVisible(false);
    diagTable_->setAlternatingRowColors(true);
    diagTable_->setSortingEnabled(false);
    diagTable_->setShowGrid(true);
    diagTable_->setWordWrap(false);

    // Column widths
    diagTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    diagTable_->setColumnWidth(0, 40);   // ID
    diagTable_->setColumnWidth(1, 130);  // Name
    diagTable_->setColumnWidth(2, 90);   // Temp
    diagTable_->setColumnWidth(3, 70);   // FPS
    diagTable_->setColumnWidth(4, 95);   // Shutter
    diagTable_->setColumnWidth(5, 65);   // Gain
    diagTable_->setColumnWidth(6, 65);   // Gamma
    diagTable_->setColumnWidth(7, 80);   // WDR High
    diagTable_->setColumnWidth(8, 80);   // WDR Low
    diagTable_->setColumnWidth(9, 85);   // Buf Frames
    diagTable_->setColumnWidth(10, 80);  // Buf MB
    diagTable_->horizontalHeader()->setStretchLastSection(true);

    // Stylesheet (re-use project table style)
    diagTable_->setStyleSheet(makeTableStyle(tc, false));

    rootLayout->addWidget(diagTable_, 1);

    // --- Timer ---
    diagRefreshTimer_ = new QTimer(this);
    diagRefreshTimer_->setInterval(3000);
    connect(diagRefreshTimer_, &QTimer::timeout, this, &AnalysisView::refreshDiagTable);
    connect(diagRefreshBtn_, &QPushButton::clicked, this, &AnalysisView::refreshDiagTable);
    connect(diagAutoRefreshChk_, &QCheckBox::toggled, this, &AnalysisView::onDiagAutoRefreshToggled);

    // Start auto-refresh by default (timer activates when tab is shown)
    // First populate with static data immediately
    refreshDiagTable();
}

// ---------------------------------------------------------------------------
// onDiagAutoRefreshToggled — start/stop the 3-second refresh timer
// ---------------------------------------------------------------------------
void AnalysisView::onDiagAutoRefreshToggled(bool enabled) {
    if (!diagRefreshTimer_) return;
    if (enabled)
        diagRefreshTimer_->start();
    else
        diagRefreshTimer_->stop();
}

// ---------------------------------------------------------------------------
// refreshDiagTable — poll all cameras and update table rows
// ---------------------------------------------------------------------------
void AnalysisView::refreshDiagTable() {
    if (!diagTable_) return;

    ThemeColors tc = CameraConfig::getThemeColors();
    int camCount = CameraConfig::getCameraCount();

    // Resize rows if camera count changed
    if (diagTable_->rowCount() != camCount)
        diagTable_->setRowCount(camCount);

    // Helper: create a centered, non-editable item
    auto makeItem = [](const QString& text) {
        auto* item = new QTableWidgetItem(text);
        item->setTextAlignment(Qt::AlignCenter);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        return item;
    };
    // Helper: ASCII placeholder item
    auto makeNA = [&tc]() {
        auto* item = new QTableWidgetItem("N/A");
        item->setTextAlignment(Qt::AlignCenter);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        item->setForeground(QColor(tc.border));
        return item;
    };

    auto applyRowColors = [&](int row, const QColor& background, const QColor& foreground) {
        for (int col = 0; col < diagTable_->columnCount(); ++col) {
            QTableWidgetItem* item = diagTable_->item(row, col);
            if (!item) continue;
            item->setBackground(background);
            item->setForeground(foreground);
        }
    };

    std::vector<CameraInfo> cameras;
    cameras.reserve(camCount);
    for (int idx = 0; idx < camCount; ++idx) {
        cameras.push_back(CameraConfig::getCameraInfo(idx));
    }
    std::sort(cameras.begin(), cameras.end(), [](const CameraInfo& lhs, const CameraInfo& rhs) {
        return lhs.id < rhs.id;
    });

    for (int row = 0; row < camCount; ++row) {
        const CameraInfo& info = cameras[row];
        const int configIndex = info.id - 1;

        // Disabled cameras: show ID + Name, everything else as N/A.
        if (info.source == 2) {
            diagTable_->setItem(row, 0,  makeItem(QString::number(info.id)));
            diagTable_->setItem(row, 1,  makeItem(info.name));
            for (int col = 2; col <= 10; ++col)
                diagTable_->setItem(row, col, makeNA());
            applyRowColors(row, QColor(55, 55, 55, 140), QColor(tc.border));
            continue;
        }

        // --- Live data from CameraManager (if available) ---
        double temperature = std::numeric_limits<double>::quiet_NaN();
        double fps   = 0.0;
        CameraManager::CameraParams p;
        bool isConnected = false;

        if (cameraManager_) {
            isConnected  = cameraManager_->isCameraConnected(configIndex);
            if (isConnected) {
                temperature = cameraManager_->getTemperature(configIndex);
                fps         = cameraManager_->getCameraFps(configIndex);
                p           = cameraManager_->getCameraParams(configIndex);
            }
        } else {
            // Fallback: static config values
            fps = info.fps;
        }

        // Col 0: ID
        diagTable_->setItem(row, 0, makeItem(QString::number(info.id)));

        // Col 1: Name
        diagTable_->setItem(row, 1, makeItem(info.name));

        // Col 2: Temperature
        {
            QString tempStr = std::isnan(temperature)
                ? QString("N/A")
                : QString::number(temperature, 'f', 1);
            auto* item = makeItem(tempStr);
            diagTable_->setItem(row, 2, item);
        }

        // Col 3: FPS
        diagTable_->setItem(row, 3, makeItem(
            fps > 0.0 ? QString::number(fps, 'f', 1) : QString("N/A")));

        // Col 4: Shutter [µs]
        diagTable_->setItem(row, 4, cameraManager_
            ? makeItem(QString::number(p.exposureUs, 'f', 0))
            : makeNA());

        // Col 5: Gain
        diagTable_->setItem(row, 5, cameraManager_
            ? makeItem(QString::number(p.gain, 'f', 2))
            : makeNA());

        // Col 6: Gamma
        diagTable_->setItem(row, 6, cameraManager_
            ? makeItem(QString::number(p.gamma, 'f', 2))
            : makeNA());

        // Col 7: WDR High
        diagTable_->setItem(row, 7, (cameraManager_ && !std::isnan(p.wdrHigh))
            ? makeItem(QString::number(p.wdrHigh, 'f', 2))
            : makeNA());

        // Col 8: WDR Low
        diagTable_->setItem(row, 8, (cameraManager_ && !std::isnan(p.wdrLow))
            ? makeItem(QString::number(p.wdrLow, 'f', 2))
            : makeNA());

        // Col 9: Buffer Frames (live Pylon output queue depth)
        diagTable_->setItem(row, 9, cameraManager_
            ? makeItem(QString::number(p.outputQueueDepth))
            : makeNA());

        // Col 10: Buffer [MB] — outputQueueDepth × W × H × bpp / 1 048 576
        if (cameraManager_ && p.width > 0 && p.height > 0) {
            double mb = static_cast<double>(p.outputQueueDepth)
                      * p.width * p.height * p.bpp
                      / (1024.0 * 1024.0);
            diagTable_->setItem(row, 10, makeItem(QString::number(mb, 'f', 2)));
        } else {
            diagTable_->setItem(row, 10, makeNA());
        }

        if (cameraManager_) {
            if (isConnected) {
                applyRowColors(row, QColor(20, 70, 40, 90), QColor(tc.text));
            } else {
                applyRowColors(row, QColor(90, 35, 35, 90), QColor("#F2C2C2"));
            }
        }

        // Keep temperature severity as the strongest visual signal.
        if (!std::isnan(temperature)) {
            QTableWidgetItem* tempItem = diagTable_->item(row, 2);
            if (tempItem) {
                CameraManager::TemperatureStatus st = CameraManager::classifyTemperature(temperature);
                if (st == CameraManager::TS_Error)
                    tempItem->setBackground(QColor(0xFF, 0x40, 0x40, 160));
                else if (st == CameraManager::TS_Critical)
                    tempItem->setBackground(QColor(0xFF, 0xAA, 0x00, 160));
            }
        }
    }
}
