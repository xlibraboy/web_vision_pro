#include "AnalysisView.h"
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

AnalysisView::AnalysisView(int numCameras, QWidget *parent) 
    : QWidget(parent), numCameras_(numCameras), selectedCameraId_(-1),
      serverRunning_(false), isRecording_(false), isPlaying_(false), isReviewMode_(false),
      currentFrame_(0), totalFrames_(1000), playbackSpeed_(1.0), triggerFrameIndex_(0),
      isStreamingMode_(false), baseWidth_(782), baseHeight_(582) {
    
    // Initialize video reader
    videoReader_ = std::make_unique<VideoStreamReader>();
    
    // Setup playback timer
    playbackTimer_ = new QTimer(this);
    connect(playbackTimer_, &QTimer::timeout, this, &AnalysisView::onPlaybackTick);
    
    setupUI();
    
    // Initialize EventDatabase and load historical events
    EventDatabase::instance().initialize("../data");
    auto events = EventDatabase::instance().getAllEvents();
    
    std::cout << "[AnalysisView] Loading " << events.size() << " historical events..." << std::endl;
    
    for (const auto& event : events) {
        // Insert new row
        int row = paperBreakTable_->rowCount();
        paperBreakTable_->insertRow(row);
        
        // Column 0: Timestamp (was Col 1)
        QString displayTime = formatTimestamp(event.timestamp);
        QTableWidgetItem* timeItem = new QTableWidgetItem(displayTime);
        paperBreakTable_->setItem(row, 0, timeItem);
        
        // Column 1: Trigger By (Deterministic) (was Col 2)
        QStringList triggers = {"Reel", "Calender", "Press", "Wire"};
        long long seed = 0;
        QString rawTs = event.timestamp.isEmpty() ? QString::number(row) : event.timestamp;
        for (QChar c : rawTs) { 
            if (c.isDigit()) seed += c.digitValue(); 
        }
        QString randomTrigger = triggers[seed % triggers.size()];
        
        QTableWidgetItem* sourceItem = new QTableWidgetItem(randomTrigger);
        paperBreakTable_->setItem(row, 1, sourceItem);
        
        // Store event timestamp in item data for later loading
        timeItem->setData(Qt::UserRole, event.timestamp);
    }
    
    // Auto-select latest historical event
    if (paperBreakTable_->rowCount() > 0) {
        int lastRow = paperBreakTable_->rowCount() - 1;
        paperBreakTable_->selectRow(lastRow);
        onLogSelected(lastRow, 1); // Load the event
    }
    
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
    
    // Check if this is a TIFF directory (ends with _tiff) or Raw Binary (.bin)
    if (videoPath.endsWith("_tiff") || videoPath.endsWith(".bin")) {
        std::cout << "[AnalysisView] Delegating to specialized loader: " << videoPath.toStdString() << std::endl;
        startReview(videoPath, triggerIndex);
        return;
    }
    
    // Open video file with VideoStreamReader (for .bin, .mp4, etc.)
    if (!videoReader_->open(videoPath)) {
        std::cerr << "[AnalysisView] Failed to open video file!" << std::endl;
        return;
    }
    
    // Switch to streaming mode
    isReviewMode_ = true;
    isStreamingMode_ = true;
    recordedSequence_.clear();  // Clear in-memory sequence as we're loading from disk
    
    // Get video properties
    totalFrames_ = videoReader_->getTotalFrames() - 1;
    
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
    
    // Preload chunk around trigger point
    videoReader_->preloadChunk(triggerFrameIndex_, 25);
    
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
    
    // 1. Server Toggle (FIRST - left side)
    serverButton_ = new QPushButton("Server: OFF", controlsGroup);
    serverButton_->setCheckable(true);
    serverButton_->setToolTip("Toggle Server Connection");
    serverButton_->setStyleSheet(
        "QPushButton { padding: 4px; font-size: 10px; background: #607D8B; color: white; border-radius: 3px; }"
        "QPushButton:hover { background: #78909C; }"
        "QPushButton:pressed { background: #455A64; }"
        "QPushButton:checked { background: #4CAF50; }"
        "QPushButton:checked:hover { background: #66BB6A; }"
    );
    connect(serverButton_, &QPushButton::clicked, this, &AnalysisView::onServerButtonClicked);
    
    // 2. Admin Login (SECOND - right side)
    adminButton_ = new QPushButton("Login", controlsGroup);
    adminButton_->setToolTip("Admin Login");
    adminButton_->setStyleSheet(
        "QPushButton { padding: 4px; font-size: 10px; background: #2196F3; color: white; border-radius: 3px; }"
        "QPushButton:hover { background: #42A5F5; }"
        "QPushButton:pressed { background: #1976D2; }"
    );
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
    
    paperBreakTable_ = new QTableWidget(0, 2, logGroup); // 2 Columns: Timestamp, Source
    paperBreakTable_->setHorizontalHeaderLabels({"Trigger Time", "Trigger By"});

    // Fix 1: Don't bold header on selection
    paperBreakTable_->horizontalHeader()->setHighlightSections(false);
    // Fix 2: Allow manual resizing, set default width
    paperBreakTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    paperBreakTable_->setColumnWidth(0, 150);
    paperBreakTable_->horizontalHeader()->setStretchLastSection(true); // Stretch last column to fill
    // Fix: Disable horizontal scrolling
    paperBreakTable_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    paperBreakTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    paperBreakTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    paperBreakTable_->setEditTriggers(QAbstractItemView::NoEditTriggers); 
    paperBreakTable_->setAlternatingRowColors(true);
    
    paperBreakTable_->setStyleSheet(
        "QTableWidget { background-color: #2a2a2a; alternate-background-color: #323232; color: #e0e0e0; gridline-color: #383838; font-size: 11px; border: none; outline: none; }"
        "QHeaderView::section { background-color: #383838; color: #ddd; padding: 2px 1px; border: none; border-bottom: 2px solid #555; }" 
        "QHeaderView::section:checked { background-color: #383838; color: #ddd; }"
        "QHeaderView::section:pressed { background-color: #383838; color: #ddd; }"
        "QHeaderView::section:hover { background-color: #383838; color: #ddd; }"
        "QHeaderView::section:disabled { background-color: #383838; color: #ddd; }"
        "QTableWidget::item { padding: 1px; border: none; color: #e0e0e0; }"  
        "QTableWidget::item:selected { background-color: #1565C0; color: white; }"
        "QTableWidget::item:selected:!active { background-color: #0D47A1; color: white; }"
    );
    
    // Connect log selection and Delete toggle
    connect(paperBreakTable_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (deleteButton_ && deleteButton_->isEnabled()) {
             // Admin Delete Mode: Native Selection handles the "Red" toggle.
             // We do nothing here, let the user click to select/deselect.
        } else {
            // Normal Mode: Load Event
            onLogSelected(row, col);
        }
    });
    
    // Enable context menu for deletion
    paperBreakTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(paperBreakTable_, &QTableWidget::customContextMenuRequested, [this](const QPoint& pos) {
        if (!deleteButton_ || !deleteButton_->isEnabled()) return; // Only allow context menu if delete is enabled
        
        QMenu menu;
        menu.setStyleSheet("QMenu { background-color: #333; color: white; border: 1px solid #555; }"
                           "QMenu::item { padding: 5px 20px; }"
                           "QMenu::item:selected { background-color: #1976D2; }");
        QAction* deleteAction = menu.addAction("Delete Selected");
        connect(deleteAction, &QAction::triggered, this, &AnalysisView::onDeleteClicked);
        menu.exec(paperBreakTable_->mapToGlobal(pos));
    });

    // Add Keyboard Shortcut (Del and Backspace)
    auto deleteShortcut = new QShortcut(QKeySequence::Delete, paperBreakTable_);
    connect(deleteShortcut, &QShortcut::activated, this, &AnalysisView::onDeleteClicked);
    
    auto backspaceShortcut = new QShortcut(QKeySequence(Qt::Key_Backspace), paperBreakTable_);
    connect(backspaceShortcut, &QShortcut::activated, this, &AnalysisView::onDeleteClicked);
    
    logLayout->addWidget(paperBreakTable_);
    
    // Delete Button (Initially Disabled/Hidden)
    deleteButton_ = new QPushButton("Delete Selected", logGroup);
    deleteButton_->setStyleSheet(
        "QPushButton { background-color: #D32F2F; color: white; font-weight: bold; padding: 6px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #E53935; }"
        "QPushButton:pressed { background-color: #C62828; }"
        "QPushButton:disabled { background-color: #555; color: #888; }"
    );
    deleteButton_->setEnabled(false); // Only admin
    deleteButton_->setVisible(false); // Hide until admin mode
    connect(deleteButton_, &QPushButton::clicked, this, &AnalysisView::onDeleteClicked);
    
    logLayout->addWidget(deleteButton_);
    
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
    tabWidget_->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #444; background: #1a1a1a; }"
        "QTabBar::tab { background: #333; color: #aaa; padding: 8px 16px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #1a1a1a; color: white; border-bottom: 2px solid #2196F3; }"
        "QTabBar::tab:hover { background: #444; }"
    );
    
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
    
    // Tab 3: Diagnostic (Camera Details)
    diagnosticTab_ = new QWidget();
    auto diagLayout = new QVBoxLayout(diagnosticTab_);
    diagLayout->setContentsMargins(20, 20, 20, 20);
    diagLayout->setAlignment(Qt::AlignTop);
    
    // Default content
    auto diagLabel = new QLabel("Select a camera to view details", diagnosticTab_);
    diagLabel->setAlignment(Qt::AlignCenter);
    diagLabel->setStyleSheet("color: #888; font-size: 16px;");
    diagLabel->setObjectName("diagLabel"); // Access by name
    diagLayout->addWidget(diagLabel);
    
    tabWidget_->addTab(diagnosticTab_, "Diagnostic");

    // Tab 4: Configuration
    // Tab 4: Configuration
    configTab_ = new QWidget();
    auto configLayout = new QVBoxLayout(configTab_);
    
    // --- Camera Settings Group ---
    auto camGroup = new QGroupBox("Camera Settings", configTab_);
    auto camForm = new QFormLayout(camGroup);
    
    // Resolution
    resolutionComboBox_ = new QComboBox(camGroup);
    resolutionComboBox_->addItem("Full (782x582)");
    resolutionComboBox_->addItem("Binning 2x2 (391x291)");
    // Note: Changing this requires restart of acquisition
    resolutionComboBox_->setToolTip("Warning: Changing resolution requires restarting the camera system.");
    
    // FPS
    fpsSpinBox_ = new QSpinBox(camGroup);
    fpsSpinBox_->setRange(1, 100);
    fpsSpinBox_->setValue(10); // Default for Paper Machine
    fpsSpinBox_->setSuffix(" fps");
    
    camForm->addRow("Resolution:", resolutionComboBox_);
    camForm->addRow("Frame Rate:", fpsSpinBox_);
    
    configLayout->addWidget(camGroup);
    
    // --- Buffer Settings Group ---
    auto bufferGroup = new QGroupBox("Buffer Settings", configTab_);
    auto formLayout = new QFormLayout(bufferGroup);
    
    preTriggerSpinBox_ = new QSpinBox(bufferGroup);
    preTriggerSpinBox_->setRange(1, 60);
    preTriggerSpinBox_->setValue(10); // Default (10s @ 10fps = 100 frames)
    preTriggerSpinBox_->setSuffix(" s");
    preTriggerSpinBox_->setToolTip("Duration of recording BEFORE the trigger.");
    
    postTriggerSpinBox_ = new QSpinBox(bufferGroup);
    postTriggerSpinBox_->setRange(1, 60);
    postTriggerSpinBox_->setValue(5); // Default (5s @ 10fps = 50 frames)
    postTriggerSpinBox_->setSuffix(" s");
    postTriggerSpinBox_->setToolTip("Duration of recording AFTER the trigger.");
    
    formLayout->addRow("Pre-Trigger:", preTriggerSpinBox_);
    formLayout->addRow("Post-Trigger:", postTriggerSpinBox_);
    
    applyConfigButton_ = new QPushButton("Apply Configuration", configTab_);
    applyConfigButton_->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold; padding: 5px;");
    connect(applyConfigButton_, &QPushButton::clicked, this, &AnalysisView::onApplyConfigClicked);
    
    configLayout->addWidget(bufferGroup);
    configLayout->addWidget(applyConfigButton_);
    configLayout->addStretch();
    
    tabWidget_->addTab(configTab_, "Configuration");
    
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

void AnalysisView::setupPlaybackControls() {
    playbackPanel_ = new QWidget(this);
    playbackPanel_->setObjectName("playbackPanel"); // For CSS specificity
    playbackPanel_->setFixedHeight(55); 
    playbackPanel_->setAutoFillBackground(true); // Force paint
    // Use background-color and ensure contrast. 
    playbackPanel_->setStyleSheet("QWidget#playbackPanel { background-color: #121212; border-top: 1px solid #333; }");
    
    auto layout = new QVBoxLayout(playbackPanel_);
    layout->setContentsMargins(16, 4, 16, 4);
    layout->setSpacing(2);
    
    // Playback slider (System Standard)
    playbackSlider_ = new QSlider(Qt::Horizontal, playbackPanel_);
    playbackSlider_->setRange(0, 10000);
    playbackSlider_->setStyleSheet(
        "QSlider::groove:horizontal { height: 4px; background: #333; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; height: 12px; margin: -4px 0; background: #fff; border-radius: 6px; }"
        "QSlider::handle:horizontal:hover { transform: scale(1.2); background: #eee; }"
        "QSlider::sub-page:horizontal { background: #0078D4; border-radius: 2px; }"
        "QSlider::add-page:horizontal { background: #333; border-radius: 2px; }"
    );
    layout->addWidget(playbackSlider_);
    
    // Zero Marker
    sliderZeroMarker_ = new QLabel(playbackPanel_);
    sliderZeroMarker_->setFixedSize(3, 15);
    sliderZeroMarker_->setStyleSheet("background: #FF5722;");
    sliderZeroMarker_->raise();
    sliderZeroMarker_->show();
    
    // Toolbar
    auto toolbarLayout = new QHBoxLayout();
    toolbarLayout->setSpacing(8);
    toolbarLayout->setContentsMargins(0, 4, 0, 0); 
    
    // === PLAYBACK CONTROL TOOLBAR (High Visibility System Standard) ===
    
    // Style: Glassy Dark background for visibility against any parent, White Text
    QString buttonStyle = 
        "QPushButton { "
        "   background-color: #252525; " /* Slight dark background to ensure visibility */
        "   color: #ffffff; "
        "   border: 1px solid #333; "
        "   border-radius: 4px; "
        "   font-family: 'Segoe UI Symbol', 'DejaVu Sans', sans-serif; " /* Font fallback */
        "   font-size: 16px; " 
        "   padding: 4px; "
        "}"
        "QPushButton:hover { "
        "   background-color: #333333; "
        "   border-color: #555; "
        "}"
        "QPushButton:pressed { "
        "   background-color: #000000; "
        "   color: #aaa; "
        "}";

    toolbarLayout->addStretch();
    
    // Nav Controls
    beginButton_ = new QPushButton("|<", playbackPanel_); // Safer ASCII fallback if fonts fail, or simpler unicode
    beginButton_->setFixedSize(28, 24);
    beginButton_->setToolTip("Go to Start");
    beginButton_->setStyleSheet(buttonStyle);
    connect(beginButton_, &QPushButton::clicked, this, &AnalysisView::onBeginClicked);
    toolbarLayout->addWidget(beginButton_);
    
    prevButton_ = new QPushButton("<", playbackPanel_);
    prevButton_->setFixedSize(28, 24);
    prevButton_->setToolTip("Step Back");
    prevButton_->setStyleSheet(buttonStyle);
    prevButton_->setAutoRepeat(true);
    connect(prevButton_, &QPushButton::pressed, this, &AnalysisView::onPreviousPressed);
    connect(prevButton_, &QPushButton::released, this, &AnalysisView::onPreviousReleased);
    toolbarLayout->addWidget(prevButton_);
    
    // Play/Pause (Accent Blue)
    playPauseButton_ = new QPushButton(">", playbackPanel_); 
    playPauseButton_->setFixedSize(28, 24); 
    playPauseButton_->setToolTip("Play/Pause");
    // Ensure this button definitely looks different
    playPauseButton_->setStyleSheet(
        "QPushButton { "
        "   background-color: #0078D4; "
        "   color: white; "
        "   border: none; "
        "   border-radius: 21px; "
        "   font-size: 20px; "
        "   font-weight: bold; "
        "   padding-left: 2px; "
        "}"
        "QPushButton:hover { background-color: #1084E0; }"
        "QPushButton:pressed { background-color: #006CC0; }"
    );
    connect(playPauseButton_, &QPushButton::clicked, this, &AnalysisView::onPlayPauseClicked);
    toolbarLayout->addWidget(playPauseButton_);
    
    nextButton_ = new QPushButton(">", playbackPanel_);
    nextButton_->setFixedSize(28, 24);
    nextButton_->setToolTip("Step Forward");
    nextButton_->setStyleSheet(buttonStyle);
    nextButton_->setAutoRepeat(true);
    connect(nextButton_, &QPushButton::pressed, this, &AnalysisView::onNextPressed);
    connect(nextButton_, &QPushButton::released, this, &AnalysisView::onNextReleased);
    toolbarLayout->addWidget(nextButton_);
    
    endButton_ = new QPushButton(">|", playbackPanel_);
    endButton_->setFixedSize(28, 24);
    endButton_->setToolTip("Go to End");
    endButton_->setStyleSheet(buttonStyle);
    connect(endButton_, &QPushButton::clicked, this, &AnalysisView::onEndClicked);
    toolbarLayout->addWidget(endButton_);
    
    toolbarLayout->addSpacing(24);
    
    // Trigger
    resetButton_ = new QPushButton("O", playbackPanel_); // Simple character fallback
    resetButton_->setFixedSize(28, 24);
    resetButton_->setToolTip("Jump to Trigger");
    resetButton_->setStyleSheet(buttonStyle); 
    connect(resetButton_, &QPushButton::clicked, this, &AnalysisView::onResetClicked);
    toolbarLayout->addWidget(resetButton_);
    
    toolbarLayout->addStretch();
    
    // Frame Input
    QLabel* frameLabel = new QLabel("Frame:", playbackPanel_);
    frameLabel->setStyleSheet("color: #bbb; font-size: 15px; margin-right: 4px;");
    toolbarLayout->addWidget(frameLabel);
    
    frameInput_ = new QLineEdit("0.0", playbackPanel_);
    frameInput_->setFixedSize(60, 24);
    frameInput_->setAlignment(Qt::AlignCenter);
    frameInput_->setStyleSheet("QLineEdit { background: #222; color: #fff; border: 1px solid #444; border-radius: 4px; }");
    connect(frameInput_, &QLineEdit::editingFinished, this, &AnalysisView::onFrameInputChanged);
    toolbarLayout->addWidget(frameInput_);
    
    toolbarLayout->addSpacing(12);
    
    // Speed
    speedButton_ = new QPushButton("1.0x", playbackPanel_);
    speedButton_->setFixedSize(60, 24);
    speedButton_->setStyleSheet(buttonStyle); // Use visible button style
    speedMenu_ = new QMenu(speedButton_);
    speedMenu_->addAction("Very Slow (0.25x)")->setData(0.25);
    speedMenu_->addAction("Slow (0.5x)")->setData(0.5);
    speedMenu_->addAction("Normal (1.0x)")->setData(1.0);
    speedMenu_->addAction("Fast (2.0x)")->setData(2.0);
    speedMenu_->addAction("Very Fast (4.0x)")->setData(4.0);
    speedButton_->setMenu(speedMenu_);
    connect(speedMenu_, &QMenu::triggered, this, &AnalysisView::onSpeedChanged);
    toolbarLayout->addWidget(speedButton_);
    
    toolbarLayout->addSpacing(12);
    
    // Export
    saveAviButton_ = new QPushButton("Export", playbackPanel_); 
    saveAviButton_->setFixedSize(60, 24);
    saveAviButton_->setStyleSheet(
        "QPushButton { background: transparent; color: #0078D4; border: 1px solid #0078D4; border-radius: 4px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background-color: rgba(0, 120, 212, 0.1); }"
        "QPushButton:pressed { background-color: rgba(0, 120, 212, 0.2); }"
    );
    connect(saveAviButton_, &QPushButton::clicked, this, &AnalysisView::onExportMp4Clicked);
    toolbarLayout->addWidget(saveAviButton_);
    
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
    
    int rowCount = paperBreakTable_->rowCount();
    std::cout << "[AnalysisView] Scanning " << rowCount << " rows for marked items..." << std::endl;
    
    // Use selectedItems() to find rows
    QList<QTableWidgetItem*> selected = paperBreakTable_->selectedItems();
    QSet<int> uniqueRows;
    for (auto* item : selected) {
        uniqueRows.insert(item->row());
    }
    
    if (uniqueRows.isEmpty()) {
        QMessageBox::information(this, "Delete", "Please select items to delete.");
        return;
    }

    for (int row : uniqueRows) {
        QTableWidgetItem* item = paperBreakTable_->item(row, 0); 
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
            paperBreakTable_->removeRow(row);
        }
        
        // Clear the data view immediately
        clearData();
    }
}


void AnalysisView::onExportMp4Clicked() {
    // Determine current source
    QString sourceFile;
    
    if (isStreamingMode_) {
        // Find current event from table or internal state
        // For simplicity, we just use the last loaded file if in streaming mode
        // Ideally we track the current file path in class
        // Let's deduce it from the paperBreakTable selection
        QList<QTableWidgetItem*> selected = paperBreakTable_->selectedItems();
        if (!selected.isEmpty()) {
            QString timestamp = selected[0]->data(Qt::UserRole).toString();
            auto eventInfo = EventDatabase::instance().getEventInfo(timestamp);
            sourceFile = eventInfo.videoPath;
        }
    } else {
        std::cerr << "[AnalysisView] Export only available for saved files (Streaming Mode)." << std::endl;
        return;
    }
    
    if (!sourceFile.isEmpty()) {
        exportToMp4(sourceFile);
    }
}

void AnalysisView::exportToMp4(const QString& sourcePath) {
    if (sourcePath.isEmpty()) return;
    
    QString destPath = sourcePath;
    // Replace extension with .mp4
    int lastDot = destPath.lastIndexOf('.');
    if (lastDot != -1) {
        destPath = destPath.left(lastDot);
    }
    destPath += "_export.mp4";
    
    std::cout << "[AnalysisView] Exporting " << sourcePath.toStdString() << " to " << destPath.toStdString() << "..." << std::endl;
    
    // Disable UI during export
    saveAviButton_->setEnabled(false);
    saveAviButton_->setText("Exporting...");
    QApplication::processEvents();
    
    // Use a local reader to transcode using Pylon
    VideoStreamReader reader;
    if (reader.open(sourcePath)) {
        if (reader.exportToMp4(destPath)) {
             std::cout << "[AnalysisView] Export complete." << std::endl;
        } else {
             std::cerr << "[AnalysisView] Export failed." << std::endl;
        }
        reader.close();
    }
    
    saveAviButton_->setText("Export MP4");
    saveAviButton_->setEnabled(true);
}

void AnalysisView::onAdminButtonClicked() {
    emit adminLoginRequested();
}

void AnalysisView::onLogSelected(int row, int col) {
    // Determine if we should load the event
    // If clicking checkbox (col 0), just toggle check state (already handled by widget)
    // We don't want to load video on every check
    // Get event timestamp from table item data
    // Column 0 is now Timestamp
    QTableWidgetItem* item = paperBreakTable_->item(row, 0);
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
    
    // Update Details Tab (Diagnostic)
    if (diagnosticTab_) {
        // Clear existing layout
        qDeleteAll(diagnosticTab_->findChildren<QWidget*>("", Qt::FindDirectChildrenOnly));
        delete diagnosticTab_->layout();
        
        auto diagLayout = new QVBoxLayout(diagnosticTab_);
        diagLayout->setContentsMargins(20, 20, 20, 20);
        diagLayout->setSpacing(10);
        diagLayout->setAlignment(Qt::AlignTop);
        
        // Get Camera Info
        CameraInfo info = CameraConfig::getCameraInfo(cameraId);
        
        auto createDetailRow = [](const QString& label, const QString& value) {
            auto row = new QWidget();
            auto rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 5, 0, 5);
            
            auto lbl = new QLabel(label, row);
            lbl->setStyleSheet("color: #888; font-weight: bold; min-width: 120px;");
            
            auto val = new QLabel(value, row);
            val->setStyleSheet("color: #eee;");
            val->setWordWrap(true);
            
            rowLayout->addWidget(lbl);
            rowLayout->addWidget(val);
            rowLayout->addStretch();
            return row;
        };
        
        // Title
        auto title = new QLabel("Camera Details", diagnosticTab_);
        title->setStyleSheet("color: #2196F3; font-size: 18px; font-weight: bold; margin-bottom: 10px;");
        diagLayout->addWidget(title);
        
        // Details
        diagLayout->addWidget(createDetailRow("ID:", QString::number(info.id)));
        diagLayout->addWidget(createDetailRow("Name:", info.description)); // Using description as name
        diagLayout->addWidget(createDetailRow("Model:", info.model));
        diagLayout->addWidget(createDetailRow("IP Address:", info.ipAddress));
        diagLayout->addWidget(createDetailRow("Location:", info.location));
        diagLayout->addWidget(createDetailRow("Side:", info.side));
        diagLayout->addWidget(createDetailRow("Resolution:", info.imageSize));
        diagLayout->addWidget(createDetailRow("Frame Rate:", QString::number(info.fps) + " fps"));
        diagLayout->addWidget(createDetailRow("Temperature:", QString::number(info.temperature) + " °C"));
        
        diagLayout->addStretch();
    }
}

void AnalysisView::onTabChanged(int index) {
    Q_UNUSED(index);
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
        QImage frameImage;
        
        if (isStreamingMode_) {
            // On-demand loading from video file
            int idx = qBound(0, static_cast<int>(currentFrame_), static_cast<int>(totalFrames_ - 1));
            cv::Mat cvFrame = videoReader_->getFrame(idx);
            
            if (!cvFrame.empty()) {
                // Convert Mat to QImage
                cv::Mat rgb;
                cv::cvtColor(cvFrame, rgb, cv::COLOR_BGR2RGB);
                frameImage = QImage(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888).copy();
            }
        } else if (!recordedSequence_.empty()) {
            // Load from in-memory sequence
            int idx = qBound(0, static_cast<int>(currentFrame_), static_cast<int>(recordedSequence_.size()) - 1);
            frameImage = recordedSequence_[idx];
        }
        
        if (!frameImage.isNull()) {
            // Check if this is a tiled image (multi-camera recording)
            bool isTiled = (frameImage.width() > baseWidth_ || frameImage.height() > baseHeight_);
            
            // Get consistent metadata text
            QString overlayText = getMetadataOverlayText(static_cast<int>(currentFrame_), relativeFrame);
            QString tooltipText = getMetadataTooltip(static_cast<int>(currentFrame_), relativeFrame);
            
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
                // Legacy or single-camera recording (apply to all for fallback)
                for (auto* widget : cameraWidgets_) {
                    widget->setFrame(frameImage);
                    widget->setTimestamp(overlayText, tooltipText);
                }
                
                if (selectedCameraWidget_) {
                    selectedCameraWidget_->setFrame(frameImage);
                    selectedCameraWidget_->setTimestamp(overlayText, tooltipText);
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
    playPauseButton_->setText(isPlaying_ ? "||" : ">"); // Use Safe ASCII/Simpler Symbols
    
    // Toggle Style
    playPauseButton_->setStyleSheet(isPlaying_
        ? "QPushButton { background-color: #0078D4; color: white; border: none; border-radius: 21px; font-size: 16px; font-weight: bold; } QPushButton:hover { background-color: #1084E0; } QPushButton:pressed { background-color: #006CC0; }"
        : "QPushButton { background-color: #0078D4; color: white; border: none; border-radius: 21px; font-size: 16px; font-weight: bold; padding-left: 2px; } QPushButton:hover { background-color: #1084E0; } QPushButton:pressed { background-color: #006CC0; }"
    );
    
    // Start/stop playback timer
    if (isPlaying_) {
        // Calculate interval based on speed (assume 30 fps base rate)
        int interval = static_cast<int>(33.0 / playbackSpeed_);  // 33ms = ~30fps
        playbackTimer_->start(interval);
    } else {
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
    currentFrame_ = qMax(0.0, currentFrame_ - 1.0); // Step by 1 frame
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
    currentFrame_ = qMin(static_cast<double>(totalFrames_), currentFrame_ + 1.0); // Step by 1 frame
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
    speedButton_->setText(QString("%1x").arg(playbackSpeed_));
    
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
    frameInput_->setEnabled(hasData);
    speedButton_->setEnabled(hasData);
    saveAviButton_->setEnabled(hasData && isStreamingMode_); // Only enable export for saved files
    
    // Gray out appearance when disabled
    if (!hasData) {
        playbackPanel_->setStyleSheet("QWidget { color: gray; }");
    } else {
        playbackPanel_->setStyleSheet("");
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
    int yPos = sliderRect.y() - 7;  // Position slightly above the slider
    
    // Position and show the marker
    sliderZeroMarker_->move(xPos - 1, yPos);  // Center the 3px wide marker
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
    // Early exit if no data
    if (recordedSequence_.empty()) return;
    
    // Advance frame based on speed
    // Timer runs at ~33ms (30fps). To play at 30fps (1.0x), we need 1 frame per tick.
    double step = 1.0 * playbackSpeed_;
    
    currentFrame_ += step;
    
    // Stop at end instead of looping
    if (currentFrame_ >= totalFrames_) {
        currentFrame_ = totalFrames_;
        onPlayPauseClicked(); // Stop playback
    }
    
    // Update UI (Slider value is relative)
    double relativeFrame = currentFrame_ - triggerFrameIndex_;
    
    playbackSlider_->setValue(static_cast<int>(relativeFrame * 10));
    frameInput_->setText(QString::number(relativeFrame, 'f', 1));
    
    // In Review Mode, update the camera widgets with the recorded frame
    if (isReviewMode_ && !recordedSequence_.empty()) {
        int idx = qBound(0, static_cast<int>(currentFrame_), static_cast<int>(recordedSequence_.size()) - 1);
        const QImage& frame = recordedSequence_[idx];
        
        // Metadata Display (Consistent with Slider Moved)
        QString overlayText = getMetadataOverlayText(idx, relativeFrame);
        QString tooltipText = getMetadataTooltip(idx, relativeFrame);
        
        for (auto* widget : cameraWidgets_) {
            widget->setFrame(frame);
            widget->setTimestamp(overlayText, tooltipText);
        }
        
        if (selectedCameraWidget_) {
            selectedCameraWidget_->setFrame(frame);
            selectedCameraWidget_->setTimestamp(overlayText, tooltipText);
        }
    }
}

void AnalysisView::addPaperBreakEvent(const std::string& timestamp, int triggerIndex, int totalFrames) {
    QString rawTs = QString::fromStdString(timestamp);
    QString displayTs = formatTimestamp(rawTs);
    
    int row = paperBreakTable_->rowCount();
    paperBreakTable_->insertRow(row);
    
    // Column 0: Timestamp (was Col 1)
    QTableWidgetItem* timeItem = new QTableWidgetItem(displayTs);
    paperBreakTable_->setItem(row, 0, timeItem);
    
    // Column 1: Trigger By (Deterministic) (was Col 2)
    QStringList triggers = {"Reel", "Calender", "Press", "Wire"};
    // Use simple hash of timestamp to pick trigger source
    // timestamp string format: yyyyMMdd_HHmmss_zzz
    // We can just sum the digits or take the last few digits
    long long seed = 0;
    for (QChar c : rawTs) { 
        if (c.isDigit()) seed += c.digitValue(); 
    }
    QString randomTrigger = triggers[seed % triggers.size()];
    
    QTableWidgetItem* sourceItem = new QTableWidgetItem(randomTrigger);
    paperBreakTable_->setItem(row, 1, sourceItem);
    
    // Store event timestamp in item data for later loading
    timeItem->setData(Qt::UserRole, rawTs);
    
    // Load RAW BINARY from disk
    QString binPath = QString("../data/event_%1.bin").arg(rawTs);
    startReview(binPath, triggerIndex);
    
    // Auto-select the new row
    paperBreakTable_->selectRow(row);
    paperBreakTable_->scrollToItem(paperBreakTable_->item(row, 0)); // Ensure visible
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
        
        // Create QImage (Deep Copy)
        QImage img(width, height, QImage::Format_Grayscale8);
        memcpy(img.bits(), buffer.data(), frameSize);
        recordedSequence_.push_back(img);
    }
    
    std::cout << "[AnalysisView] Loaded " << recordedSequence_.size() << " frames." << std::endl;
}

void AnalysisView::setDeleteEnabled(bool enabled) {
    // Enable/Show Delete Button
    if (deleteButton_) {
        deleteButton_->setEnabled(enabled);
        deleteButton_->setVisible(enabled);
    }
    
    // Switch Selection Mode & Stylesheet
    paperBreakTable_->clearSelection();
    
    if (enabled) {
        // DELETE MODE: RED Selection, Multi-Select (Click to toggle)
        paperBreakTable_->setSelectionMode(QAbstractItemView::MultiSelection);
        paperBreakTable_->setStyleSheet(
            "QTableWidget { background-color: #2a2a2a; alternate-background-color: #323232; color: #e0e0e0; gridline-color: #383838; font-size: 11px; border: none; outline: none; }"
            "QHeaderView::section { background: #383838; color: #ddd; padding: 2px 4px; border: none; border-bottom: 2px solid #555; }" 
            "QTableWidget::item { padding: 1px; border: none; color: #e0e0e0; }"  
            "QTableWidget::item:selected { background-color: #D32F2F; color: white; }" // RED
            "QTableWidget::item:selected:!active { background-color: #C62828; color: white; }"
        );
    } else {
        // NORMAL MODE: BLUE Selection, Single-Select
        paperBreakTable_->setSelectionMode(QAbstractItemView::SingleSelection);
        paperBreakTable_->setStyleSheet(
            "QTableWidget { background-color: #2a2a2a; alternate-background-color: #323232; color: #e0e0e0; gridline-color: #383838; font-size: 11px; border: none; outline: none; }"
            "QHeaderView::section { background: #383838; color: #ddd; padding: 2px 4px; border: none; border-bottom: 2px solid #555; }" 
            "QTableWidget::item { padding: 1px; border: none; color: #e0e0e0; }"  
            "QTableWidget::item:selected { background-color: #1565C0; color: white; }" // BLUE
            "QTableWidget::item:selected:!active { background-color: #0D47A1; color: white; }"
        );
    }
    
    // Clean up any manual background modifications from previous logic (safety)
    for (int i = 0; i < paperBreakTable_->rowCount(); ++i) {
        for (int c = 0; c < paperBreakTable_->columnCount(); ++c) {
            paperBreakTable_->item(i, c)->setBackground(Qt::transparent);
            paperBreakTable_->item(i, c)->setData(Qt::UserRole + 1, false); 
        }
    }
    
    std::cout << "[AnalysisView] Delete Mode: " << (enabled ? "ENABLED" : "DISABLED") << std::endl;
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
    if (paperBreakTable_ && paperBreakTable_->rowCount() > 0) {
        int lastRow = paperBreakTable_->rowCount() - 1;
        paperBreakTable_->selectRow(lastRow);
        paperBreakTable_->scrollToItem(paperBreakTable_->item(lastRow, 0)); // Ensure visible
        onLogSelected(lastRow, 1); // Load the latest recording
        
        // Auto-select Camera 0 and switch to Single View ("Detail View data record")
        // "if trigger record true" -> implied by having rows
        onCameraClicked(0); 
    }
}

void AnalysisView::onApplyConfigClicked() {
    int preSeconds = preTriggerSpinBox_->value();
    int postSeconds = postTriggerSpinBox_->value();
    
    // Warn user about buffer reset
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Apply Configuration", 
                                  "Changing buffer settings will clear the current recording buffer.\nAny unsaved data will be lost.\n\nContinue?",
                                  QMessageBox::Yes|QMessageBox::No);
                                  
    if (reply == QMessageBox::Yes) {
        // Gather settings
        int fps = fpsSpinBox_->value(); // New FPS control
        int binning = (resolutionComboBox_->currentIndex() == 0) ? 1 : 2; // 0=Full, 1=2x2
        
        std::cout << "[AnalysisView] Applying Config: Pre=" << preSeconds << "s, Post=" << postSeconds 
                  << "s, FPS=" << fps << ", Binning=" << binning << std::endl;
                  
        // Emit signal to MainWindow to handle coordination
        emit configApplied(preSeconds, postSeconds, fps, binning);
        
        QMessageBox::information(this, "Success", "Configuration applied. \nBuffer reset and Camera settings updated.");
    }
}


void AnalysisView::clearData() {
    std::cout << "[AnalysisView] Clearing data to free memory..." << std::endl;
    
    // 1. Clear In-Memory Sequence
    recordedSequence_.clear();
    // Force vector to release memory
    std::vector<QImage>().swap(recordedSequence_);
    
    // 2. Close Video Reader (File handles + Buffer)
    if (videoReader_) {
        videoReader_->close();
    }
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
