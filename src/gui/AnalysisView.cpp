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
        "gridline-color: #2a2a2a; font-size: 12px; font-family: sans-serif; border: none; outline: none; }"
        "QHeaderView::section { background-color: %1; color: #E0E0E0; padding: 4px; border: none; border-bottom: 1px solid #2a2a2a; text-align: left; font-size: 12px; font-family: sans-serif; }"
        "QHeaderView::section:checked, QHeaderView::section:pressed, "
        "QHeaderView::section:hover, QHeaderView::section:disabled "
        "{ background-color: %1; color: #E0E0E0; }"
        "QTableWidget::item { padding: 4px; border: none; color: #E0E0E0; font-size: 12px; font-family: sans-serif; }"
    ).arg(tc.bg, tc.btnBg)
     + (deleteMode ? selDelete : selNormal);
}

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
    
    paperBreakTable_->setSortingEnabled(false);
    for (const auto& event : events) {
        // Insert new row
        int row = paperBreakTable_->rowCount();
        paperBreakTable_->insertRow(row);
        
        // Column 0: Timestamp
        QString displayTime = formatTimestamp(event.timestamp);
        QTableWidgetItem* timeItem = new QTableWidgetItem(displayTime);
        paperBreakTable_->setItem(row, 0, timeItem);
        
        // Column 1: Trigger By (Deterministic)
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
    // Ensure sorting logic matches new behavior
    paperBreakTable_->horizontalHeader()->setSortIndicator(0, Qt::DescendingOrder);
    paperBreakTable_->sortByColumn(0, Qt::DescendingOrder);
    
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
    
    paperBreakTable_ = new QTableWidget(0, 2, logGroup); // 2 Columns
    paperBreakTable_->setHorizontalHeaderLabels({"Trigger Time", "Reason"});

    // Fix 1: Don't bold header on selection
    paperBreakTable_->horizontalHeader()->setHighlightSections(false);
    // Left align headers
    paperBreakTable_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // Fix 2: Allow manual resizing, set default width
    paperBreakTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    paperBreakTable_->setColumnWidth(0, 160);
    paperBreakTable_->horizontalHeader()->setStretchLastSection(true); // Stretch last column to fill
    // We want a scrollbar, so do not set ScrollBarAlwaysOff for vertical, but leave horizontal alone if possible, or just allow Both
    // paperBreakTable_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // Commented out to allow standard scrolling if needed
    paperBreakTable_->setShowGrid(true); // Show gridlines for row/col borders
    
    // Custom Sorting Logic: Only allow sorting on Trigger Time (Column 0)
    paperBreakTable_->setSortingEnabled(false); 
    paperBreakTable_->horizontalHeader()->setSortIndicatorShown(true);
    paperBreakTable_->horizontalHeader()->setSectionsClickable(true);
    connect(paperBreakTable_->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int logicalIndex) {
        if (logicalIndex == 0) { // Only sort if 'Trigger Time' is clicked
            Qt::SortOrder order = paperBreakTable_->horizontalHeader()->sortIndicatorOrder();
            paperBreakTable_->sortByColumn(0, order);
        } else {
            // Keep the indicator on column 0 to avoid confusion
            paperBreakTable_->horizontalHeader()->setSortIndicator(0, paperBreakTable_->horizontalHeader()->sortIndicatorOrder());
        }
    });

    paperBreakTable_->verticalHeader()->setVisible(false);
    paperBreakTable_->verticalHeader()->setDefaultSectionSize(30);
    
    paperBreakTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    paperBreakTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    paperBreakTable_->setEditTriggers(QAbstractItemView::NoEditTriggers); 
    paperBreakTable_->setAlternatingRowColors(true);
    
    paperBreakTable_->setStyleSheet(makeTableStyle(tc, false));
    
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
        
        ThemeColors lc = CameraConfig::getThemeColors();
        QMenu menu;
        menu.setStyleSheet(QString(
                           "QMenu { background-color: %1; color: %2; border: 1px solid %3; }"
                           "QMenu::item { padding: 5px 20px; }"
                           "QMenu::item:selected { background-color: %4; color: %5; }")
                           .arg(lc.btnBg, lc.text, lc.border, lc.btnHover, lc.primary));
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
    deleteButton_->setStyleSheet(QString(
        "QPushButton { background-color: #D32F2F; color: white; font-weight: bold; padding: 6px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #E53935; }"
        "QPushButton:pressed { background-color: #C62828; }"
        "QPushButton:disabled { background-color: %1; color: %2; }"
    ).arg(tc.btnBg, tc.border));
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
    
    // Tab 3: Diagnostic (Camera Details)
    diagnosticTab_ = new QWidget();
    auto diagLayout = new QVBoxLayout(diagnosticTab_);
    diagLayout->setContentsMargins(20, 20, 20, 20);
    diagLayout->setAlignment(Qt::AlignTop);
    
    // Default content
    auto diagLabel = new QLabel("Select a camera to view details", diagnosticTab_);
    diagLabel->setAlignment(Qt::AlignCenter);
    diagLabel->setStyleSheet(QString("color: %1; font-size: 16px;").arg(tc.border));
    diagLabel->setObjectName("diagLabel"); // Access by name
    diagLayout->addWidget(diagLabel);
    
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
        ThemeColors tc = CameraConfig::getThemeColors();
        
        auto createDetailRow = [&tc](const QString& label, const QString& value) {
            auto row = new QWidget();
            auto rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 5, 0, 5);
            
            auto lbl = new QLabel(label, row);
            lbl->setStyleSheet(QString("color: %1; font-weight: bold; min-width: 120px;").arg(tc.border));
            
            auto val = new QLabel(value, row);
            val->setStyleSheet(QString("color: %1;").arg(tc.text));
            val->setWordWrap(true);
            
            rowLayout->addWidget(lbl);
            rowLayout->addWidget(val);
            rowLayout->addStretch();
            return row;
        };
        
        // Title
        auto title = new QLabel("Camera Details", diagnosticTab_);
        title->setStyleSheet(QString("color: %1; font-size: 18px; font-weight: bold; margin-bottom: 10px;").arg(tc.primary));
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
    
    paperBreakTable_->setSortingEnabled(false);
    int row = paperBreakTable_->rowCount();
    paperBreakTable_->insertRow(row);
    
    // Column 0: Timestamp
    QTableWidgetItem* timeItem = new QTableWidgetItem(displayTs);
    paperBreakTable_->setItem(row, 0, timeItem);
    
    // Column 1: Reason
    QStringList triggers = {"Reel", "Calender", "Press", "Wire"};
    // Use simple hash of timestamp to pick trigger source
    long long seed = 0;
    for (QChar c : rawTs) { 
        if (c.isDigit()) seed += c.digitValue(); 
    }
    QString randomTrigger = triggers[seed % triggers.size()];
    
    QTableWidgetItem* sourceItem = new QTableWidgetItem(randomTrigger);
    paperBreakTable_->setItem(row, 1, sourceItem);
    
    // Store event timestamp in item data for later loading
    timeItem->setData(Qt::UserRole, rawTs);
    
    // Note: sorting enabled is manipulated manually, just sort here.
    paperBreakTable_->sortByColumn(0, paperBreakTable_->horizontalHeader()->sortIndicatorOrder());
    
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
        deleteButton_->setVisible(enabled);
        deleteButton_->setEnabled(enabled);
    }
    
    // Switch Selection Mode & Stylesheet
    paperBreakTable_->clearSelection();
    
    ThemeColors tc = CameraConfig::getThemeColors();
    if (enabled) {
        // DELETE MODE: RED Selection, Multi-Select (Click to toggle)
        paperBreakTable_->setSelectionMode(QAbstractItemView::MultiSelection);
        paperBreakTable_->setStyleSheet(makeTableStyle(tc, true));
    } else {
        // NORMAL MODE: Primary color Selection, Single-Select
        paperBreakTable_->setSelectionMode(QAbstractItemView::SingleSelection);
        paperBreakTable_->setStyleSheet(makeTableStyle(tc, false));
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
        // Re-trigger dynamic tab update to reconstruct diagnostic view with new colors
        updateDynamicTab(selectedCameraId_);
    }
    
    // 7. Table and Context Menu
    setDeleteEnabled(deleteButton_->isVisible()); // Reapplies the makeTableStyle logic
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
}

