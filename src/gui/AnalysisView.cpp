#include "AnalysisView.h"
#include <QKeyEvent>
#include <QMouseEvent>
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
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QShortcut>
#include <QMenu>
#include <QKeySequence>
#include <QFrame>
#include <QPainter>
#include <QStyledItemDelegate>
#include <algorithm>

class LogSelectionDelegate : public QStyledItemDelegate {
public:
    explicit LogSelectionDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        QStyledItemDelegate::paint(painter, opt, index);

        if (!(option.state & QStyle::State_Selected) || index.column() != 0) {
            return;
        }

        ThemeColors tc = CameraConfig::getThemeColors();
        painter->save();
        painter->fillRect(QRect(option.rect.left(), option.rect.top() + 1, 3, option.rect.height() - 2), QColor(tc.primary));
        painter->restore();
    }
};

static QString makeSidebarPanelStyle(const ThemeColors& tc) {
    const QString divider = QColor(tc.border).lighter(112).name();
    const QString subtleText = QColor(tc.text).lighter(110).name();
    const QString mutedText = QColor(tc.text).lighter(135).name();
    return QString(
        "QWidget#analysisLeftSidebar { background: transparent; }"
        "QGroupBox#analysisSidebarCard {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 10px;"
        "  margin-top: 12px;"
        "  padding-top: 12px;"
        "  font-size: 13px;"
        "  font-weight: 700;"
        "  color: %3;"
        "}"
        "QGroupBox#analysisSidebarCard::title {"
        "  subcontrol-origin: margin;"
        "  subcontrol-position: top left;"
        "  left: 12px;"
        "  padding: 0 4px;"
        "  color: %3;"
        "}"
        "QLabel#analysisSidebarSectionLabel {"
        "  color: %4;"
        "  font-size: 12px;"
        "  font-weight: 700;"
        "  padding: 0 2px;"
        "}"
        "QLabel#analysisSidebarSectionLabel[accent='true'] { color: %5; }"
        "QLabel#analysisSidebarSectionLabel[utility='true'] { color: %6; font-size: 11px; font-weight: 600; padding: 0; }"
        "QFrame#analysisSidebarDivider {"
        "  background-color: %7;"
        "  min-height: 1px;"
        "  max-height: 1px;"
        "  border: none;"
        "}"
    ).arg(tc.btnBg, tc.border, tc.text, subtleText, tc.primary, mutedText, divider);
}

static QString makeSidebarStateButtonStyle(const ThemeColors& tc, bool active) {
    const QString bg = active ? QStringLiteral("#0F6B52") : tc.bg;
    const QString border = active ? QStringLiteral("#18A57B") : tc.border;
    const QString hover = active ? QStringLiteral("#138362") : tc.btnHover;
    const QString text = active ? QStringLiteral("#F5FFFC") : tc.text;
    return QString(
        "QPushButton {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 6px;"
        "  padding: 7px 10px;"
        "  font-size: 12px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background-color: %4; border-color: %5; }"
        "QPushButton:pressed { background-color: %5; color: %6; }"
    ).arg(bg, text, border, hover, tc.primary, tc.bg);
}

static QString makeSidebarPrimaryButtonStyle(const ThemeColors& tc) {
    return QString(
        "QPushButton {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %1;"
        "  border-radius: 6px;"
        "  padding: 7px 10px;"
        "  font-size: 12px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background-color: %3; border-color: %3; }"
        "QPushButton:pressed { background-color: %4; color: %5; }"
    ).arg(tc.primary, tc.bg, tc.btnHover, tc.border, tc.text);
}

static QString makeSidebarOutlineButtonStyle(const ThemeColors& tc, bool checkedHighlight = false) {
    const QString checkedBg = checkedHighlight ? QColor(tc.primary).darker(180).name() : tc.btnHover;
    return QString(
        "QPushButton {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 6px;"
        "  padding: 6px 10px;"
        "  font-size: 10px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background-color: %4; border-color: %5; }"
        "QPushButton:pressed { background-color: %4; }"
        "QPushButton:checked { background-color: %6; border-color: %5; color: %5; }"
        "QPushButton:disabled { background-color: %7; color: %3; border-color: %3; }"
    ).arg(tc.btnBg, tc.text, tc.border, tc.btnHover, tc.primary, checkedBg, tc.bg);
}

static QString makeSidebarUtilityButtonStyle(const ThemeColors& tc) {
    const QString quietText = QColor(tc.text).lighter(120).name();
    return QString(
        "QPushButton {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 6px;"
        "  padding: 6px 10px;"
        "  font-size: 10px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover { background-color: %4; color: %5; border-color: %5; }"
        "QPushButton:pressed { background-color: %4; }"
        "QPushButton:checked { background-color: %1; color: %5; border-color: %5; }"
    ).arg(tc.btnBg, quietText, tc.border, tc.bg, tc.primary);
}

static QString makeSidebarActionButtonStyle(const ThemeColors& tc) {
    const QString disabledText = QColor(tc.text).lighter(135).name();
    const QString disabledBorder = QColor(tc.primary).darker(150).name();
    return QString(
        "QPushButton {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 6px;"
        "  padding: 6px 10px;"
        "  font-size: 10px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background-color: %4; border-color: %5; }"
        "QPushButton:pressed { background-color: %4; }"
        "QPushButton:disabled { background-color: %1; color: %6; border-color: %7; }"
    ).arg(tc.btnBg, tc.text, tc.primary, tc.btnHover, tc.primary, disabledText, disabledBorder);
}

static QString makeSidebarDangerButtonStyle(const ThemeColors& tc) {
    return QString(
        "QPushButton {"
        "  background-color: #982B2B;"
        "  color: #FFF5F5;"
        "  border: 1px solid #B93A3A;"
        "  border-radius: 6px;"
        "  padding: 6px 10px;"
        "  font-size: 10px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background-color: #B93A3A; border-color: #D94A4A; }"
        "QPushButton:pressed { background-color: #7C2020; }"
        "QPushButton:disabled { background-color: %1; color: %2; border-color: %2; }"
    ).arg(tc.btnBg, tc.border);
}

static QString makePlaybackIconButtonStyle(const ThemeColors& tc, bool active = false) {
    const QString bg = active ? QColor(tc.primary).darker(125).name() : tc.btnBg;
    const QString border = active ? tc.primary : tc.border;
    const QString text = active ? tc.bg : tc.text;
    return QString(
        "QPushButton {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 6px;"
        "  padding: 0;"
        "}"
        "QPushButton:hover { background-color: %4; border-color: %5; }"
        "QPushButton:pressed { background-color: %5; color: %6; }"
        "QPushButton:disabled { background-color: %7; color: %3; border-color: %3; }"
    ).arg(bg, text, border, tc.btnHover, tc.primary, tc.bg, tc.bg);
}

static QString makePlaybackSpeedButtonStyle(const ThemeColors& tc) {
    const QString quietText = QColor(tc.text).lighter(115).name();
    return QString(
        "QPushButton {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 6px;"
        "  padding: 0 10px;"
        "  font-size: 12px;"
        "  font-weight: 700;"
        "  text-align: left;"
        "}"
        "QPushButton:hover { background-color: %4; border-color: %5; color: %6; }"
        "QPushButton:pressed { background-color: %5; color: %6; }"
        "QPushButton:menu-indicator { subcontrol-origin: padding; subcontrol-position: center right; right: 8px; }"
        "QPushButton:disabled { background-color: %7; color: %3; border-color: %3; }"
    ).arg(tc.bg, quietText, tc.border, tc.btnBg, tc.primary, tc.text, tc.bg);
}

static QString makePlaybackSliderStyle(const ThemeColors& tc) {
    return QString(
        "QSlider::groove:horizontal { height: 4px; background: %1; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: %2; border-radius: 2px; }"
        "QSlider::add-page:horizontal { background: %3; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; height: 12px; margin: -4px 0; background: %4; border: 1px solid %2; border-radius: 6px; }"
        "QSlider::handle:horizontal:hover { background: %5; border-color: %5; }"
        "QSlider::handle:horizontal:pressed { background: %5; }"
        "QSlider::groove:horizontal:disabled { background: %6; }"
        "QSlider::sub-page:horizontal:disabled { background: %6; }"
        "QSlider::add-page:horizontal:disabled { background: %1; }"
        "QSlider::handle:horizontal:disabled { background: %1; border-color: %6; }"
    ).arg(tc.border, tc.primary, tc.btnHover, tc.handle, QColor(tc.handle).lighter(115).name(), tc.bg);
}

// Helper: build paperBreakTable stylesheet from theme colors
static QString makeTableStyle(const ThemeColors& tc, bool deleteMode) {
    QColor selectedBg(tc.btnHover);
    QColor divider(tc.border);
    divider = divider.lightness() < 128 ? divider.lighter(130) : divider.darker(115);
    selectedBg = selectedBg.lightness() < 128 ? selectedBg.lighter(125) : selectedBg.darker(115);
    QString selNormal  = QString("QTableWidget::item:selected { background-color: %1; color: white; }")
                         .arg(selectedBg.name());
    QString selDelete  = "QTableWidget::item:selected { background-color: #D32F2F; color: white; }";
    return QString(
        "QTableWidget { background-color: %1; alternate-background-color: %2; color: #E0E0E0; "
        "gridline-color: transparent; font-size: 11px; font-family: 'Noto Sans', 'DejaVu Sans', sans-serif; "
        "border: 1px solid %3; border-radius: 8px; outline: none; padding: 2px; }"
        "QHeaderView::section { background-color: %1; color: #E0E0E0; padding: 6px 8px; border: none; border-bottom: 1px solid %3; text-align: left; font-size: 10px; font-family: 'Noto Sans', 'DejaVu Sans', sans-serif; font-weight: 700; }"
        "QHeaderView::section:checked, QHeaderView::section:pressed, "
        "QHeaderView::section:hover, QHeaderView::section:disabled "
        "{ background-color: %1; color: #E0E0E0; }"
        "QTableCornerButton::section { background-color: %1; border: none; border-bottom: 1px solid %3; }"
        "QTableWidget::item { padding: 4px 8px; border: none; color: #E0E0E0; font-size: 11px; font-family: 'Noto Sans', 'DejaVu Sans', sans-serif; }"
    ).arg(tc.bg, tc.btnBg, divider.name())
     + (deleteMode ? selDelete : selNormal);
}

static QString analysisSurfaceColor(const QString& surfaceStyle) {
    return surfaceStyle == "light" ? QStringLiteral("#F2F2F2") : QStringLiteral("#000000");
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
    EventDatabase::instance().initialize(CameraConfig::getEventStoragePath());
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
    mainSplitter_->setSizes({304, 800});    // Initial sizes
    
    mainLayout->addWidget(mainSplitter_, 1);
    
    // Initialize controls to disabled state (no data yet)
    updatePlaybackControlsState();
    applyAnalysisViewStyle();
}

void AnalysisView::startReviewFromFile(const QString& videoPath, int triggerIndex) {
    std::cout << "[AnalysisView] Starting review from file: " << videoPath.toStdString() << std::endl;
    
    // Check if this is a TIFF directory (ends with _tiff)
    if (videoPath.endsWith("_tiff")) {
        std::cout << "[AnalysisView] Delegating to specialized loader: " << videoPath.toStdString() << std::endl;
        startReview(videoPath, triggerIndex);
        return;
    }
    
    // Open all event camera files that actually exist on disk, not only the
    // currently configured camera count. This preserves old multi-camera events
    // even if the current system config now has fewer active cameras.
    const int maxEventCameras = 16;
    videoReaders_.clear();
    
    bool anyOpened = false;
    int highestOpenedCameraIndex = -1;
    for (int i = 0; i < maxEventCameras; ++i) {
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
                highestOpenedCameraIndex = std::max(highestOpenedCameraIndex, i);
            }
        }
    }

    if (!anyOpened) {
        std::cerr << "[AnalysisView] Failed to open any video files for event!" << std::endl;
        return;
    }

    const int eventCameraCount = std::max(1, highestOpenedCameraIndex + 1);
    if (eventCameraCount != static_cast<int>(cameraWidgets_.size())) {
        setCameraCount(eventCameraCount);
    }

    currentEventCameraLabels_.clear();
    QFileInfo currentEventFileInfo(videoPath);
    QString currentEventBaseName = currentEventFileInfo.baseName();
    if (currentEventBaseName.startsWith("event_")) {
        QString tsStr = currentEventBaseName.mid(6);
        const int camSuffix = tsStr.lastIndexOf("_cam");
        if (camSuffix >= 0) {
            tsStr = tsStr.left(camSuffix);
        }
        try {
            currentEventCameraLabels_ = EventDatabase::instance().getEventInfo(tsStr).cameraLabels;
        } catch (...) {
            currentEventCameraLabels_.clear();
        }
    }

    for (int i = 0; i < static_cast<int>(cameraWidgets_.size()); ++i) {
        if (!cameraWidgets_[i]) {
            continue;
        }
        if (videoReaders_.find(i) != videoReaders_.end()) {
            cameraWidgets_[i]->setTitle(currentEventCameraLabel(i));
        } else {
            cameraWidgets_[i]->setTitle(QString("CAM-%1").arg(i + 1, 2, 10, QChar('0')));
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

    loadEventAnnotations(videoPath);

    // Parse event base time from filename: event_yyyyMMdd_HHmmss_zzz_camN.bin
    eventBaseTime_ = QDateTime();
    QFileInfo eventFileInfo(videoPath);
    QString eventBaseName = eventFileInfo.baseName();
    if (eventBaseName.startsWith("event_")) {
        QString tsStr = eventBaseName.mid(6);
        const int camSuffix = tsStr.lastIndexOf("_cam");
        if (camSuffix >= 0) {
            tsStr = tsStr.left(camSuffix);
        }
        eventBaseTime_ = QDateTime::fromString(tsStr, "yyyyMMdd_HHmmss_zzz");
        if (!eventBaseTime_.isValid()) {
            eventBaseTime_ = QDateTime::fromString(tsStr, "yyyyMMdd_HHmmss");
        }
    }

    // Load per-frame timestamp/frame counter metadata from the first available RAW reader.
    // New RAW layout stores pixels first and FrameMetadata second for each frame.
    frameMetadata_.clear();
    if (!videoReaders_.empty()) {
        auto& primaryReader = videoReaders_.begin()->second;
        const int frameCount = primaryReader->getTotalFrames();
        frameMetadata_.reserve(frameCount);
        for (int i = 0; i < frameCount; ++i) {
            ::FrameMetadata rawMeta = {};
            if (!primaryReader->getFrameMetadata(i, rawMeta)) {
                break;
            }
            FrameMetadata meta;
            meta.timestamp = static_cast<int64_t>(rawMeta.timestamp);
            meta.frameCounter = static_cast<int64_t>(rawMeta.frameId);
            meta.displayTime = QString("%1.%2 s")
                .arg(static_cast<qulonglong>(rawMeta.timestamp / 1000000000ULL))
                .arg(static_cast<qulonglong>(rawMeta.timestamp % 1000000000ULL), 9, 10, QChar('0'));
            frameMetadata_.push_back(meta);
        }
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
    setPlaybackPlaying(false);
    
    updatePlaybackControlsState();
    updateSliderZeroMarker();  // Position the zero marker
    updateAnnotationSliderMarkers();
    updatePlaybackInfoLabel();
    
    std::cout << "[AnalysisView] Review loaded from file: " << totalFrames_ + 1 
              << " frames, trigger at " << triggerFrameIndex_ << std::endl;
}

void AnalysisView::setupLeftSidebar() {
    leftSidebar_ = new QWidget(this);
    leftSidebar_->setObjectName("analysisLeftSidebar");
    leftSidebar_->setFixedWidth(304);
    
    auto layout = new QVBoxLayout(leftSidebar_);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(12);

    // Server Controls Group
    auto controlsGroup = new QGroupBox("Control Panel", leftSidebar_);
    controlsGroup->setObjectName("analysisSidebarCard");
    auto controlsLayout = new QHBoxLayout(controlsGroup);
    controlsLayout->setSpacing(8);
    controlsLayout->setContentsMargins(10, 12, 10, 10);
    
    ThemeColors tc = CameraConfig::getThemeColors();
    leftSidebar_->setStyleSheet(makeSidebarPanelStyle(tc));

    // 1. Server Toggle (FIRST - left side)
    serverButton_ = new QPushButton("Server Offline", controlsGroup);
    serverButton_->setCheckable(true);
    serverButton_->setToolTip("Toggle Server Connection");
    serverButton_->setMinimumHeight(34);
    serverButton_->setStyleSheet(makeSidebarStateButtonStyle(tc, false));
    connect(serverButton_, &QPushButton::clicked, this, &AnalysisView::onServerButtonClicked);
    
    // 2. Admin Login (SECOND - right side)
    adminButton_ = new QPushButton("Login", controlsGroup);
    adminButton_->setToolTip("Admin Login");
    adminButton_->setMinimumHeight(34);
    adminButton_->setStyleSheet(makeSidebarPrimaryButtonStyle(tc));
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
    controlsLayout->setStretch(0, 1);
    controlsLayout->setStretch(1, 1);

    layout->addWidget(controlsGroup);
    
    // Paper Break Log Group
    auto logGroup = new QGroupBox("Paper Break Log", leftSidebar_);
    logGroup->setObjectName("analysisSidebarCard");
    auto logLayout = new QVBoxLayout(logGroup);
    logLayout->setContentsMargins(10, 12, 10, 10);
    logLayout->setSpacing(8);
    
    paperBreakTable_ = createLogTable(logGroup, false);
    permanentPaperBreakTable_ = createLogTable(logGroup, false);

    recentRecordsLabel_ = new QLabel("Recent Records (0)", logGroup);
    recentRecordsLabel_->setObjectName("analysisSidebarSectionLabel");
    logLayout->addWidget(recentRecordsLabel_);

    logLayout->addWidget(paperBreakTable_);

    togglePermanentTableButton_ = new QPushButton("Show Permanent Storage", logGroup);
    togglePermanentTableButton_->setCheckable(true);
    togglePermanentTableButton_->setMinimumHeight(34);
    togglePermanentTableButton_->setIcon(QIcon(":/assets/icons/arrow_down.svg"));
    togglePermanentTableButton_->setIconSize(QSize(12, 12));
    togglePermanentTableButton_->setStyleSheet(makeSidebarUtilityButtonStyle(tc));
    connect(togglePermanentTableButton_, &QPushButton::toggled, this, [this](bool checked) {
        permanentSectionWidget_->setVisible(checked);
        togglePermanentTableButton_->setText(checked ? "Hide Permanent Storage" : "Show Permanent Storage");
        togglePermanentTableButton_->setIcon(QIcon(checked ? ":/assets/icons/arrow_up.svg" : ":/assets/icons/arrow_down.svg"));
        permanentPaperBreakTable_->setSizePolicy(QSizePolicy::Expanding, checked ? QSizePolicy::Expanding : QSizePolicy::Preferred);
        leftSidebar_->updateGeometry();
    });
    logLayout->addWidget(togglePermanentTableButton_);

    permanentSectionWidget_ = new QWidget(logGroup);
    permanentSectionWidget_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto permanentLayout = new QVBoxLayout(permanentSectionWidget_);
    permanentLayout->setContentsMargins(0, 0, 0, 0);
    permanentLayout->setSpacing(8);
    permanentRecordsLabel_ = new QLabel("Permanent Storage (0)", permanentSectionWidget_);
    permanentRecordsLabel_->setObjectName("analysisSidebarSectionLabel");
    permanentRecordsLabel_->setProperty("accent", true);
    permanentLayout->addWidget(permanentRecordsLabel_);
    permanentLayout->addWidget(permanentPaperBreakTable_, 1);
    permanentSectionWidget_->setVisible(false);
    logLayout->addWidget(permanentSectionWidget_);

    auto deleteDivider = new QFrame(logGroup);
    deleteDivider->setObjectName("analysisSidebarDivider");
    deleteDivider->setFrameShape(QFrame::HLine);
    deleteDivider->setFrameShadow(QFrame::Plain);
    logLayout->addWidget(deleteDivider);

    auto deleteModeRow = new QHBoxLayout();
    deleteModeRow->setContentsMargins(0, 8, 0, 0);
    deleteModeRow->setSpacing(8);

    auto deleteLabel = new QLabel("Enable Event Deletion", logGroup);
    deleteLabel->setObjectName("analysisSidebarSectionLabel");
    deleteLabel->setProperty("utility", true);
    deleteModeRow->addWidget(deleteLabel);
    deleteModeRow->addStretch();

    enableDeleteCheck_ = new ToggleSwitch(logGroup);
    enableDeleteCheck_->setEnabled(false);
    connect(enableDeleteCheck_, &ToggleSwitch::toggled, this, &AnalysisView::setDeleteEnabled);
    deleteModeRow->addWidget(enableDeleteCheck_, 0, Qt::AlignVCenter);
    logLayout->addLayout(deleteModeRow);
    
    // Delete Button (Initially Disabled/Hidden)
    auto buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 2, 0, 0);
    buttonRow->setSpacing(6);

    permanentButton_ = new QPushButton("Mark Permanent", logGroup);
    permanentButton_->setMinimumHeight(34);
    permanentButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    permanentButton_->setStyleSheet(makeSidebarActionButtonStyle(tc));
    permanentButton_->setEnabled(false);
    connect(permanentButton_, &QPushButton::clicked, this, &AnalysisView::onTogglePermanentClicked);
    buttonRow->addWidget(permanentButton_);

    deleteButton_ = new QPushButton("Delete Selected", logGroup);
    deleteButton_->setMinimumHeight(34);
    deleteButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    deleteButton_->setStyleSheet(makeSidebarDangerButtonStyle(tc));
    deleteButton_->setEnabled(false); // Only admin
    deleteButton_->setVisible(false); // Hide until admin mode
    connect(deleteButton_, &QPushButton::clicked, this, &AnalysisView::onDeleteClicked);

    buttonRow->addWidget(deleteButton_);
    buttonRow->setStretch(0, 1);
    buttonRow->setStretch(1, 1);
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

    // Metadata selector lives in the tab header empty space. It is visible for
    // All Camera and Camera detail tabs, and hidden on Diagnostic.
    metadataHeaderWidget_ = new QWidget(tabWidget_);
    auto metadataLayout = new QHBoxLayout(metadataHeaderWidget_);
    metadataLayout->setContentsMargins(2, 0, 2, 0);
    metadataLayout->setSpacing(4);
    auto metadataLabel = new QLabel("Metadata:", metadataHeaderWidget_);
    metadataDisplayCombo_ = new QComboBox(metadataHeaderWidget_);
    metadataDisplayCombo_->setToolTip("Select metadata shown on camera review overlays.");
    metadataDisplayCombo_->addItem("None", "none");
    metadataDisplayCombo_->addItem("Standard", "standard");
    metadataDisplayCombo_->addItem("Timestamp + Frame Counter", "full");
    metadataDisplayCombo_->addItem("Timestamp Only", "timestamp");
    metadataDisplayCombo_->addItem("Frame Counter Only", "framecounter");
    metadataDisplayCombo_->addItem("Real Time Only", "realtime");
    metadataDisplayCombo_->addItem("Relative Frame Only", "relative");
    metadataDisplayCombo_->setCurrentIndex(0);
    metadataDisplayCombo_->setFixedWidth(220);
    headerToolsSeparator_ = new QFrame(metadataHeaderWidget_);
    headerToolsSeparator_->setFrameShape(QFrame::VLine);
    headerToolsSeparator_->setFrameShadow(QFrame::Plain);
    headerToolsSeparator_->setFixedHeight(22);
    headerToolsSeparator_->setStyleSheet(QString("background-color: %1; border: none;").arg(tc.border));
    tabWidget_->setCornerWidget(metadataHeaderWidget_, Qt::TopRightCorner);
    connect(metadataDisplayCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (isReviewMode_ && playbackSlider_) {
            onSliderMoved(playbackSlider_->value());
        }
    });
    headerToolsSeparator_->setVisible(false);
    
    // Tab 1: All Camera
    allCameraTab_ = new QWidget();
    setupCameraGrid(allCameraTab_);
    tabWidget_->addTab(allCameraTab_, "All Camera");
    
    // Tab 2: Single Camera (dynamic name)
    singleCameraTab_ = new QWidget();
    auto singleLayout = new QVBoxLayout(singleCameraTab_);
    singleLayout->setContentsMargins(4, 4, 4, 4);
    singleLayout->setSpacing(4);

    detailToolsWidget_ = new QWidget(metadataHeaderWidget_);
    auto toolsLayout = new QHBoxLayout(detailToolsWidget_);
    toolsLayout->setContentsMargins(0, 2, 0, 2);
    toolsLayout->setSpacing(6);

    markerToolCheck_ = new QCheckBox("Marker", detailToolsWidget_);
    markerToolCheck_->setToolTip("Enable marker tool, then draw on the detail image.");

    markerShapeCombo_ = new QComboBox(detailToolsWidget_);
    markerShapeCombo_->setToolTip("Marker drawing type");
    markerShapeCombo_->setIconSize(QSize(16, 16));
    markerShapeCombo_->addItem(QIcon(":/assets/icons/marker_pen.svg"), "Pen", "pen");
    markerShapeCombo_->addItem(QIcon(":/assets/icons/marker_square.svg"), "Square", "rectangle");
    markerShapeCombo_->addItem(QIcon(":/assets/icons/marker_round.svg"), "Round", "circle");
    markerShapeCombo_->addItem(QIcon(":/assets/icons/marker_arrow.svg"), "Arrow", "arrow");
    markerShapeCombo_->setFixedWidth(118);

    auto zoomLabel = new QLabel("Zoom:", detailToolsWidget_);
    zoomSlider_ = new QSlider(Qt::Horizontal, detailToolsWidget_);
    zoomSlider_->setRange(100, 600);
    zoomSlider_->setValue(100);
    zoomSlider_->setFixedWidth(130);
    zoomValueLabel_ = new QLabel("1.0x", detailToolsWidget_);
    zoomValueLabel_->setMinimumWidth(38);

    auto brightnessLabel = new QLabel("Brightness:", detailToolsWidget_);
    brightnessSlider_ = new QSlider(Qt::Horizontal, detailToolsWidget_);
    brightnessSlider_->setRange(-100, 100);
    brightnessSlider_->setValue(0);
    brightnessSlider_->setFixedWidth(130);
    brightnessValueLabel_ = new QLabel("0", detailToolsWidget_);
    brightnessValueLabel_->setMinimumWidth(30);

    auto resetToolsButton = new QPushButton("Reset", detailToolsWidget_);
    resetToolsButton->setToolTip("Reset marker, zoom, and brightness for the detail image.");

    toolsLayout->addWidget(markerToolCheck_);
    toolsLayout->addWidget(markerShapeCombo_);
    toolsLayout->addSpacing(6);
    toolsLayout->addWidget(zoomLabel);
    toolsLayout->addWidget(zoomSlider_);
    toolsLayout->addWidget(zoomValueLabel_);
    toolsLayout->addSpacing(6);
    toolsLayout->addWidget(brightnessLabel);
    toolsLayout->addWidget(brightnessSlider_);
    toolsLayout->addWidget(brightnessValueLabel_);
    toolsLayout->addWidget(resetToolsButton);
    toolsLayout->addStretch(1);
    metadataLayout->addWidget(detailToolsWidget_);
    metadataLayout->addSpacing(4);
    metadataLayout->addWidget(headerToolsSeparator_);
    metadataLayout->addSpacing(4);
    metadataLayout->addWidget(metadataLabel);
    metadataLayout->addWidget(metadataDisplayCombo_);
    detailToolsWidget_->setVisible(false);
    
    selectedCameraWidget_ = new AnalysisVideoWidget(-1, "Select a camera", singleCameraTab_);
    singleLayout->addWidget(selectedCameraWidget_, 1);

    connect(selectedCameraWidget_, &AnalysisVideoWidget::annotationChangedNormalized, this,
            [this](int cameraId, const QString& shape, const QVector<QPointF>& points) {
        const int frameIndex = currentReviewFrameIndex();
        if (cameraId < 0 || frameIndex < 0 || currentAnnotationPath_.isEmpty()) return;
        QJsonArray pts;
        for (const QPointF& p : points) {
            QJsonObject obj;
            obj["nx"] = p.x();
            obj["ny"] = p.y();
            pts.append(obj);
        }
        QJsonObject ann;
        ann["shape"] = shape;
        ann["space"] = "image";
        ann["points"] = pts;
        eventAnnotations_[annotationKey(cameraId, frameIndex)] = ann;
        saveEventAnnotations();
        seekToRelativeFrame(frameIndex - triggerFrameIndex_);
        applyAnnotationToSelectedFrame();
        updateAnnotationSliderMarkers();
    });

    connect(markerToolCheck_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (selectedCameraWidget_) selectedCameraWidget_->setMarkerToolEnabled(enabled);
    });
    connect(markerShapeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (selectedCameraWidget_ && markerShapeCombo_) {
            selectedCameraWidget_->setMarkerShape(markerShapeCombo_->currentData().toString());
        }
    });
    connect(zoomSlider_, &QSlider::valueChanged, this, [this](int value) {
        const double zoom = value / 100.0;
        if (zoomValueLabel_) zoomValueLabel_->setText(QString("%1x").arg(zoom, 0, 'f', 1));
        if (selectedCameraWidget_) selectedCameraWidget_->setZoomFactor(zoom);
    });
    connect(brightnessSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (brightnessValueLabel_) brightnessValueLabel_->setText(QString::number(value));
        if (selectedCameraWidget_) selectedCameraWidget_->setBrightnessOffset(value);
    });
    connect(resetToolsButton, &QPushButton::clicked, this, [this]() {
        if (markerToolCheck_) markerToolCheck_->setChecked(false);
        if (markerShapeCombo_) markerShapeCombo_->setCurrentIndex(0);
        if (zoomSlider_) zoomSlider_->setValue(100);
        if (brightnessSlider_) brightnessSlider_->setValue(0);
        if (selectedCameraWidget_) selectedCameraWidget_->resetImageTools();
    });

    tabWidget_->addTab(singleCameraTab_, "Camera");
    
    // Tab 3: Diagnostic — all-camera live data table (built once)
    diagnosticTab_ = new QWidget();
    setupDiagnosticTab();
    tabWidget_->addTab(diagnosticTab_, "Diagnostic");

    // Tab 4: Configuration - REMOVED (Moved to System Settings Dialog)
    
    layout->addWidget(tabWidget_, 1);
    
    // Add playback panel here (camera area)
    layout->addWidget(playbackPanel_);

    connect(tabWidget_, &QTabWidget::currentChanged, this, &AnalysisView::onTabChanged);
}

void AnalysisView::applyAnalysisViewStyle() {
    if (!tabWidget_ || !playbackPanel_ || !frameInput_) {
        return;
    }

    const ThemeColors tc = CameraConfig::getThemeColors();
    const AnalysisViewStyle style = CameraConfig::getAnalysisViewStyle();
    const QString playbackSurface = analysisSurfaceColor(style.playbackSurfaceStyle);

    tabWidget_->setStyleSheet(QString(
        "QTabWidget::pane { border: 1px solid %1; background: %2; }"
        "QTabBar::tab { background: %3; color: %4; padding: 8px 16px; margin-right: 2px; font-family: '%7'; font-size: %8px; }"
        "QTabBar::tab:selected { background: %2; color: %5; border-bottom: 2px solid %6; }"
        "QTabBar::tab:hover { background: %3; }"
    ).arg(tc.border, tc.bg, tc.btnBg, tc.text, tc.text, tc.primary,
          style.tabFontFamily, QString::number(style.tabFontSize)));

    playbackPanel_->setStyleSheet(QString(
        "QWidget#playbackPanel { background-color: %1; border-top: 1px solid %2; }"
    ).arg(playbackSurface, tc.border));

    frameInput_->setStyleSheet(QString(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 5px; padding: 0 4px; font-family: '%4'; font-size: %5px; font-weight: 700;}"
        "QLineEdit:focus { border-color: %6; }"
    ).arg(tc.bg, tc.text, tc.border, style.timestampFontFamily, QString::number(std::max(12, style.timestampFontSize)), tc.primary));

    playbackSlider_->setStyleSheet(makePlaybackSliderStyle(tc));
    speedButton_->setStyleSheet(makePlaybackSpeedButtonStyle(tc));
    playPauseButton_->setStyleSheet(makePlaybackIconButtonStyle(tc, isPlaying_));
    beginButton_->setStyleSheet(makePlaybackIconButtonStyle(tc));
    prevButton_->setStyleSheet(makePlaybackIconButtonStyle(tc));
    resetButton_->setStyleSheet(makePlaybackIconButtonStyle(tc));
    nextButton_->setStyleSheet(makePlaybackIconButtonStyle(tc));
    endButton_->setStyleSheet(makePlaybackIconButtonStyle(tc));
    speedMenu_->setStyleSheet(QString(
        "QMenu { background-color: %1; color: %2; border: 1px solid %3; }"
        "QMenu::item { padding: 5px 20px; }"
        "QMenu::item:selected { background-color: %4; color: %5; }"
    ).arg(tc.btnBg, tc.text, tc.border, tc.btnHover, tc.primary));

    if (QLabel* frameLabel = playbackPanel_->findChild<QLabel*>("frameLabel")) {
        frameLabel->setStyleSheet(QString(
            "color: %1; font-family: '%2'; font-size: %3px; margin-right: 4px; font-weight: 600;"
        ).arg(tc.text, style.tabFontFamily, QString::number(std::max(11, style.tabFontSize))));
    }
    if (playbackInfoLabel_) {
        playbackInfoLabel_->setStyleSheet(QString(
            "color: %1; font-family: '%2'; font-size: %3px; font-weight: 600; margin-left: 6px;"
        ).arg(tc.text, style.tabFontFamily, QString::number(std::max(11, style.tabFontSize))));
    }

    for (AnalysisVideoWidget* widget : cameraWidgets_) {
        if (widget) {
            widget->update();
        }
    }

    if (selectedCameraWidget_) {
        selectedCameraWidget_->update();
    }
}

void AnalysisView::setupCameraGrid(QWidget* container) {
    auto rootLayout = new QVBoxLayout(container);
    rootLayout->setContentsMargins(4, 4, 4, 4);
    rootLayout->setSpacing(4);

    auto gridContainer = new QWidget(container);
    cameraGridLayout_ = new QGridLayout(gridContainer);
    cameraGridLayout_->setSpacing(4);
    cameraGridLayout_->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(gridContainer, 1);
    
    // Calculate grid dimensions
    int cols = 4;
    int rows = (numCameras_ + cols - 1) / cols;
    
    for (int i = 0; i < numCameras_; ++i) {
        QString label = CameraConfig::getCameraLabel(i);
            auto widget = new AnalysisVideoWidget(i, label, gridContainer);
        widget->setAnnotationEditable(false);
        connect(widget, &AnalysisVideoWidget::doubleClicked, this, &AnalysisView::onCameraClicked);
        
        int row = i / cols;
        int col = i % cols;
        cameraGridLayout_->addWidget(widget, row, col);
        cameraWidgets_.push_back(widget);
    }
    
    // Set equal stretch for all rows and columns
    for (int i = 0; i < rows; ++i) cameraGridLayout_->setRowStretch(i, 1);
    for (int i = 0; i < cols; ++i) cameraGridLayout_->setColumnStretch(i, 1);
}

void AnalysisView::setCameraCount(int count) {
    if (count == numCameras_) return;
    
    auto layout = cameraGridLayout_;
    if (!layout) return;
    
    int cols = 4;
    int rows = (std::max(count, 1) + cols - 1) / cols;
    
    if (count > numCameras_) {
        for (int i = numCameras_; i < count; ++i) {
            QString label = CameraConfig::getCameraLabel(i);
            auto widget = new AnalysisVideoWidget(i, label, layout->parentWidget());
            widget->setAnnotationEditable(false);
            connect(widget, &AnalysisVideoWidget::doubleClicked, this, &AnalysisView::onCameraClicked);
            
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
    playbackPanel_->setFixedHeight(60); 
    playbackPanel_->setAutoFillBackground(true); // Force paint
    // Use background-color and ensure contrast. 
    ThemeColors tc = CameraConfig::getThemeColors();
    playbackPanel_->setStyleSheet(QString(
        "QWidget#playbackPanel { background-color: %1; border-top: 1px solid %2; }")
        .arg(tc.bg, tc.border));
    
    auto layout = new QVBoxLayout(playbackPanel_);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(0);
    
    // Playback slider (System Standard)
    // === PLAYBACK CONTROL TOOLBAR (Single Line + SVGs) ===
    auto toolbarLayout = new QHBoxLayout();
    toolbarLayout->setSpacing(4);
    toolbarLayout->setContentsMargins(0, 0, 0, 0); 

    auto createDivider = [&]() -> QFrame* {
        QFrame* divider = new QFrame(playbackPanel_);
        divider->setFrameShape(QFrame::VLine);
        divider->setFrameShadow(QFrame::Plain);
        divider->setFixedSize(1, 20);
        divider->setStyleSheet(QString("background-color: %1; border: none;").arg(tc.border));
        return divider;
    };

    // Helper macro for creating SVG buttons
    auto createSvgButton = [&](const QString& iconName, const QString& tooltip) -> QPushButton* {
        QPushButton* btn = new QPushButton(playbackPanel_);
        btn->setIcon(QIcon(":/assets/icons/" + iconName));
        btn->setIconSize(QSize(18, 18));
        btn->setFixedSize(30, 30);
        btn->setToolTip(tooltip);
        btn->setStyleSheet(makePlaybackIconButtonStyle(tc));
        return btn;
    };

    // 1. Speed
    speedButton_ = new QPushButton("1.0x", playbackPanel_);
    speedButton_->setFixedSize(64, 30);
    speedButton_->setStyleSheet(makePlaybackSpeedButtonStyle(tc));
    speedMenu_ = new QMenu(speedButton_);
    speedMenu_->addAction("Ultra Slow (0.05x)")->setData(0.05);
    speedMenu_->addAction("Very Slow (0.10x)")->setData(0.10);
    speedMenu_->addAction("Slow (0.15x)")->setData(0.15);
    speedMenu_->addAction("Slow+ (0.20x)")->setData(0.20);
    speedMenu_->addAction("Legacy Very Slow (0.25x)")->setData(0.25);
    speedMenu_->addAction("Half Speed (0.5x)")->setData(0.5);
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
    toolbarLayout->addSpacing(4);
    toolbarLayout->addWidget(createDivider(), 0, Qt::AlignVCenter);
    toolbarLayout->addSpacing(4);

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
    toolbarLayout->addSpacing(4);
    toolbarLayout->addWidget(createDivider(), 0, Qt::AlignVCenter);

    // 6. Hidden frame input retained for internal seek synchronization only.
    frameInput_ = new QLineEdit("0.0", playbackPanel_);
    frameInput_->hide();
    connect(frameInput_, &QLineEdit::editingFinished, this, &AnalysisView::onFrameInputChanged);

    playbackInfoLabel_ = new QLabel("Frame -- | Time --", playbackPanel_);
    playbackInfoLabel_->setMinimumWidth(150);
    playbackInfoLabel_->setToolTip("Actual integer recorded frame and time from trigger.");
    playbackInfoLabel_->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: 600; margin-left: 6px;").arg(tc.text));
    toolbarLayout->addSpacing(4);
    toolbarLayout->addWidget(playbackInfoLabel_);
    toolbarLayout->addSpacing(4);
    toolbarLayout->addWidget(createDivider(), 0, Qt::AlignVCenter);

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
    toolbarLayout->addSpacing(8);
    playbackSlider_ = new QSlider(Qt::Horizontal, playbackPanel_);
    playbackSlider_->setRange(0, 10000); // Deciseconds essentially (1000.0)
    connect(playbackSlider_, &QSlider::sliderMoved, this, &AnalysisView::onSliderMoved);
    connect(playbackSlider_, &QSlider::valueChanged, this, &AnalysisView::onSliderValueChanged);
    playbackSlider_->setStyleSheet(makePlaybackSliderStyle(tc));
    toolbarLayout->addWidget(playbackSlider_, 1); // Stretch factor 1
    
    // Zero Marker (We still track it, but attach it to layout properly later or manually position)
    sliderZeroMarker_ = new QLabel(playbackPanel_);
    QPixmap pm(":/assets/icons/Zero Marker.svg");
    sliderZeroMarker_->setPixmap(pm.scaled(10, 10, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    sliderZeroMarker_->setFixedSize(10, 10);
    sliderZeroMarker_->raise();
    sliderZeroMarker_->show();
    
    layout->addLayout(toolbarLayout);
}

// Slot implementations
void AnalysisView::onServerButtonClicked() {
    serverRunning_ = serverButton_->isChecked();
    serverButton_->setText(serverRunning_ ? "Server Online" : "Server Offline");
    serverButton_->setStyleSheet(makeSidebarStateButtonStyle(CameraConfig::getThemeColors(), serverRunning_));
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

        // Stop review/playback and close file handles before deleting files from disk.
        clearData();

        const bool tableBlocked = activeTable->blockSignals(true);

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

        activeTable->clearSelection();
        activeTable->blockSignals(tableBlocked);

        updateRecordCountLabel();
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
        if (row >= 0 && row < paperBreakTable_->rowCount() && paperBreakTable_->item(row, 0)
            && paperBreakTable_->item(row, 0)->isSelected()) {
            sourceTable = paperBreakTable_;
        } else if (row >= 0 && row < permanentPaperBreakTable_->rowCount() && permanentPaperBreakTable_->item(row, 0)
                   && permanentPaperBreakTable_->item(row, 0)->isSelected()) {
            sourceTable = permanentPaperBreakTable_;
        } else if (paperBreakTable_->currentRow() == row && row >= 0 && row < paperBreakTable_->rowCount()) {
            sourceTable = paperBreakTable_;
        } else if (permanentPaperBreakTable_->currentRow() == row && row >= 0 && row < permanentPaperBreakTable_->rowCount()) {
            sourceTable = permanentPaperBreakTable_;
        } else {
            sourceTable = paperBreakTable_->rowCount() > 0 ? paperBreakTable_ : permanentPaperBreakTable_;
        }
    }
    QTableWidgetItem* item = sourceTable->item(row, 0);
    if (!item) return;

    if (!suppressNewEventIndicatorClear_ && item->data(Qt::UserRole + 3).toBool()) {
        item->setIcon(QIcon());
        item->setData(Qt::UserRole + 3, false);
        QFont timeFont = item->font();
        timeFont.setBold(false);
        item->setFont(timeFont);
        if (QTableWidgetItem* reasonItem = sourceTable->item(row, 1)) {
            reasonItem->setData(Qt::UserRole + 3, false);
            QFont reasonFont = reasonItem->font();
            reasonFont.setBold(false);
            reasonItem->setFont(reasonFont);
        }
        latestAddedEventTimestamp_.clear();
    }
    
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

void AnalysisView::onSelectedCameraDoubleClicked(int cameraId) {
    std::cout << "[AnalysisView] onSelectedCameraDoubleClicked: " << cameraId << std::endl;
    Q_UNUSED(cameraId);
    tabWidget_->setCurrentIndex(0);
}

void AnalysisView::updateDynamicTab(int cameraId) {
    auto* layout = qobject_cast<QVBoxLayout*>(singleCameraTab_->layout());
    auto removeSelectedCameraWidget = [&]() {
        if (!selectedCameraWidget_) {
            return;
        }
        if (layout) {
            layout->removeWidget(selectedCameraWidget_);
        }
        selectedCameraWidget_->deleteLater();
        selectedCameraWidget_ = nullptr;
    };

    if (cameraId < 0 || cameraId >= numCameras_) {
        tabWidget_->setTabText(1, "Camera");
        removeSelectedCameraWidget();
        return;
    }

    QString label = isReviewMode_ ? currentEventCameraLabel(cameraId)
                                  : CameraConfig::getCameraLabel(cameraId);
    tabWidget_->setTabText(1, label);
    
    // Update the single camera view
    removeSelectedCameraWidget();
    selectedCameraWidget_ = new AnalysisVideoWidget(cameraId, label, singleCameraTab_);
    if (markerShapeCombo_) {
        selectedCameraWidget_->setMarkerShape(markerShapeCombo_->currentData().toString());
    }
    selectedCameraWidget_->setMarkerToolEnabled(markerToolCheck_ && markerToolCheck_->isChecked());
    selectedCameraWidget_->setZoomFactor(zoomSlider_ ? zoomSlider_->value() / 100.0 : 1.0);
    selectedCameraWidget_->setBrightnessOffset(brightnessSlider_ ? brightnessSlider_->value() : 0);
    connect(selectedCameraWidget_, &AnalysisVideoWidget::annotationChangedNormalized, this,
            [this](int cameraId, const QString& shape, const QVector<QPointF>& points) {
        const int frameIndex = currentReviewFrameIndex();
        if (cameraId < 0 || frameIndex < 0 || currentAnnotationPath_.isEmpty()) return;
        QJsonArray pts;
        for (const QPointF& p : points) {
            QJsonObject obj;
            obj["nx"] = p.x();
            obj["ny"] = p.y();
            pts.append(obj);
        }
        QJsonObject ann;
        ann["shape"] = shape;
        ann["space"] = "image";
        ann["points"] = pts;
        eventAnnotations_[annotationKey(cameraId, frameIndex)] = ann;
        saveEventAnnotations();
        seekToRelativeFrame(frameIndex - triggerFrameIndex_);
        applyAnnotationToSelectedFrame();
        updateAnnotationSliderMarkers();
    });
    connect(selectedCameraWidget_, &AnalysisVideoWidget::doubleClicked,
            this, &AnalysisView::onSelectedCameraDoubleClicked);
    if (layout) {
        layout->addWidget(selectedCameraWidget_, 1);
    }
    applyAnnotationToSelectedFrame();
    updateAnnotationSliderMarkers();
    // Diagnostic tab is now a standalone all-camera table; no per-camera rebuild needed.
}

void AnalysisView::onTabChanged(int index) {
    updateAnnotationSliderMarkers();

    if (metadataHeaderWidget_) {
        metadataHeaderWidget_->setVisible(index == 0 || index == 1);
    }
    if (detailToolsWidget_) {
        detailToolsWidget_->setVisible(index == 1);
    }
    if (headerToolsSeparator_) {
        headerToolsSeparator_->setVisible(index == 1);
    }

    // Keep diagnostics live; force an immediate refresh when switching to the tab.
    if (diagRefreshTimer_ && diagAutoRefreshChk_) {
        if (diagAutoRefreshChk_->isChecked() && !diagRefreshTimer_->isActive()) {
            diagRefreshTimer_->start();
        }
        if (index == 2) {
            refreshDiagTable();
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
    
    // Update input display to show relative frame for manual seek.
    frameInput_->setText(QString::number(relativeFrame, 'f', 1));
    updatePlaybackInfoLabel();
    
    // In Review Mode, immediate update is needed when dragging slider
    if (isReviewMode_) {
        // Get consistent metadata text
        double relFrame = currentFrame_ - triggerFrameIndex_;
        QString overlayText = getMetadataOverlayText(static_cast<int>(currentFrame_), relFrame);
        QString tooltipText = getMetadataTooltip(static_cast<int>(currentFrame_), relFrame);
        
        if (isStreamingMode_) {
            // On-demand loading from video file
            int idx = currentReviewFrameIndex();
            
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
                    applyAnnotationToWidget(cameraWidgets_[camIdx], camIdx, idx);
                    
                    if (selectedCameraWidget_ && selectedCameraWidget_->getCameraId() == camIdx) {
                        selectedCameraWidget_->setFrame(finalImage);
                        selectedCameraWidget_->setTimestamp(overlayText, tooltipText);
                        applyAnnotationToSelectedFrame();
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
                            applyAnnotationToWidget(cameraWidgets_[i], i, idx);
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
                            applyAnnotationToSelectedFrame();
                        }
                    }
                } else {
                    // Single-camera recording: only update camera widget 0
                    if (!cameraWidgets_.empty()) {
                        cameraWidgets_[0]->setFrame(frameImage);
                        cameraWidgets_[0]->setTimestamp(overlayText, tooltipText);
                        applyAnnotationToWidget(cameraWidgets_[0], 0, idx);
                    }
                    // Clear all other camera slots so they don't show stale or duplicate data
                    for (int wi = 1; wi < static_cast<int>(cameraWidgets_.size()); ++wi) {
                        cameraWidgets_[wi]->clear();
                    }
                    
                    if (selectedCameraWidget_) {
                        selectedCameraWidget_->setFrame(frameImage);
                        selectedCameraWidget_->setTimestamp(overlayText, tooltipText);
                        applyAnnotationToSelectedFrame();
                    }
                }
            }
        }
    }
}

int AnalysisView::currentReviewFrameIndex() const {
    const int maxFrame = std::max(0, static_cast<int>(std::floor(totalFrames_)));
    return qBound(0, static_cast<int>(std::floor(currentFrame_ + 0.0001)), maxFrame);
}

QString AnalysisView::annotationKey(int cameraId, int frameIndex) const {
    return QString("cam%1_frame%2").arg(cameraId + 1).arg(frameIndex);
}

void AnalysisView::loadEventAnnotations(const QString& videoPath) {
    eventAnnotations_ = QJsonObject();
    currentAnnotationPath_.clear();

    QFileInfo fi(videoPath);
    QString base = fi.baseName();
    const int camSuffix = base.lastIndexOf("_cam");
    if (camSuffix >= 0) {
        base = base.left(camSuffix);
    }
    currentAnnotationPath_ = fi.dir().filePath(base + "_annotations.json");

    QFile file(currentAnnotationPath_);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isObject()) {
        eventAnnotations_ = doc.object();
    }
}

void AnalysisView::saveEventAnnotations() {
    if (currentAnnotationPath_.isEmpty()) {
        return;
    }
    QFile file(currentAnnotationPath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cerr << "[AnalysisView] Failed to save annotations: " << currentAnnotationPath_.toStdString() << std::endl;
        return;
    }
    file.write(QJsonDocument(eventAnnotations_).toJson(QJsonDocument::Indented));
}

void AnalysisView::applyAnnotationToWidget(AnalysisVideoWidget* widget, int cameraId, int frameIndex) {
    if (!widget) {
        return;
    }

    const QString key = annotationKey(cameraId, frameIndex);
    if (!eventAnnotations_.contains(key) || !eventAnnotations_[key].isObject()) {
        widget->clearAnnotation();
        return;
    }

    QJsonObject ann = eventAnnotations_[key].toObject();
    QJsonArray annotationsArray = ann["annotations"].toArray();
    if (!annotationsArray.isEmpty()) {
        // Reverting multi-marker behavior: use the last marker if an existing sidecar
        // was created with the temporary multi-marker format.
        ann = annotationsArray.last().toObject();
    }

    const QJsonArray pts = ann["points"].toArray();
    if (ann["space"].toString() == "image") {
        QVector<QPointF> normalizedPoints;
        for (const QJsonValue& val : pts) {
            const QJsonObject p = val.toObject();
            normalizedPoints.append(QPointF(p["nx"].toDouble(), p["ny"].toDouble()));
        }
        widget->setAnnotationNormalized(ann["shape"].toString("pen"), normalizedPoints);
        return;
    }

    QVector<QPoint> legacyPoints;
    const int dstW = std::max(1, widget->width());
    const int dstH = std::max(1, widget->height());
    for (const QJsonValue& val : pts) {
        const QJsonObject p = val.toObject();
        if (p.contains("nx") && p.contains("ny")) {
            legacyPoints.append(QPoint(static_cast<int>(p["nx"].toDouble() * dstW),
                                       static_cast<int>(p["ny"].toDouble() * dstH)));
        } else {
            legacyPoints.append(QPoint(p["x"].toInt(), p["y"].toInt()));
        }
    }
    widget->setAnnotation(ann["shape"].toString("pen"), legacyPoints);
}

void AnalysisView::applyAnnotationToSelectedFrame() {
    if (!selectedCameraWidget_) {
        return;
    }
    const int cameraId = selectedCameraWidget_->getCameraId();
    const int frameIndex = currentReviewFrameIndex();
    applyAnnotationToWidget(selectedCameraWidget_, cameraId, frameIndex);
    if (markerShapeCombo_ && !eventAnnotations_.contains(annotationKey(cameraId, frameIndex))) {
        selectedCameraWidget_->setMarkerShape(markerShapeCombo_->currentData().toString());
    }
}

QString AnalysisView::getMetadataOverlayText(int frameIndex, double relativeFrame) {
    const QString relativeText = QString("REC: %1").arg(relativeFrame, 0, 'f', 1);
    const QString mode = metadataDisplayCombo_ ? metadataDisplayCombo_->currentData().toString() : QString("standard");

    if (mode == "none") {
        return QString();
    }

    if (mode == "relative" || frameIndex < 0 || frameIndex >= (int)frameMetadata_.size()) {
        return relativeText;
    }

    const auto& meta = frameMetadata_[frameIndex];

    QString realTimeText;
    if (eventBaseTime_.isValid() && triggerFrameIndex_ >= 0 && triggerFrameIndex_ < (int)frameMetadata_.size()) {
        int64_t triggerTs = frameMetadata_[triggerFrameIndex_].timestamp;
        int64_t currentTs = meta.timestamp;
        int64_t diffNs = currentTs - triggerTs;
        QDateTime realTime = eventBaseTime_.addMSecs(diffNs / 1000000);
        realTimeText = realTime.toString("HH:mm:ss.zzz");
    }

    if (mode == "timestamp") {
        return QString("%1  |  TS: %2").arg(relativeText, meta.displayTime);
    }
    if (mode == "framecounter") {
        return QString("%1  |  FC: %2").arg(relativeText).arg(meta.frameCounter);
    }
    if (mode == "realtime") {
        return realTimeText.isEmpty()
            ? QString("%1  |  Time: N/A").arg(relativeText)
            : QString("%1  |  Time: %2").arg(relativeText, realTimeText);
    }

    QString text = QString("%1  |  TS: %2  |  FC: %3").arg(relativeText, meta.displayTime).arg(meta.frameCounter);
    if (!realTimeText.isEmpty()) {
        text += QString("  |  Time: %1").arg(realTimeText);
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
    if (!isPlaying_ && !playbackSlider_->isSliderDown()) {
        onSliderMoved(value);
    }
}

void AnalysisView::setPlaybackPlaying(bool playing) {
    isPlaying_ = playing;

    if (playPauseButton_) {
        playPauseButton_->setIcon(QIcon(playing ? ":/assets/icons/Pause.svg" : ":/assets/icons/Play.svg"));
        playPauseButton_->setStyleSheet(makePlaybackIconButtonStyle(CameraConfig::getThemeColors(), playing));
    }

    if (playbackTimer_) {
        if (playing) {
            const int interval = std::max(1, static_cast<int>(33.0 / playbackSpeed_));
            playbackTimer_->start(interval);
        } else {
            playbackTimer_->stop();
        }
    }

    updatePlaybackControlsState();
}

void AnalysisView::seekToRelativeFrame(double relativeFrame) {
    if (!playbackSlider_) {
        return;
    }

    currentFrame_ = qBound(0.0, relativeFrame + triggerFrameIndex_, totalFrames_);
    const double boundedRelative = currentFrame_ - triggerFrameIndex_;
    const int sliderValue = static_cast<int>(std::round(boundedRelative * 10.0));

    const bool sliderBlocked = playbackSlider_->blockSignals(true);
    playbackSlider_->setValue(sliderValue);
    playbackSlider_->blockSignals(sliderBlocked);

    frameInput_->setText(QString::number(boundedRelative, 'f', 1));
    onSliderMoved(sliderValue);
    updatePlaybackControlsState();
}

void AnalysisView::onPlayPauseClicked() {
    if (!playbackSlider_->isEnabled()) {
        return;
    }

    if (!isPlaying_ && currentFrame_ >= totalFrames_) {
        seekToRelativeFrame(-triggerFrameIndex_);
    }

    setPlaybackPlaying(!isPlaying_);
}

void AnalysisView::onBeginClicked() {
    seekToRelativeFrame(-triggerFrameIndex_);
}

void AnalysisView::onPreviousPressed() {
    const double step = std::max(0.1, playbackSpeed_);
    seekToRelativeFrame((currentFrame_ - triggerFrameIndex_) - step);
}

void AnalysisView::onPreviousReleased() {}

void AnalysisView::onResetClicked() {
    seekToRelativeFrame(0.0);
}

void AnalysisView::onNextPressed() {
    const double step = std::max(0.1, playbackSpeed_);
    seekToRelativeFrame((currentFrame_ - triggerFrameIndex_) + step);
}

void AnalysisView::onNextReleased() {}

void AnalysisView::onEndClicked() {
    seekToRelativeFrame(totalFrames_ - triggerFrameIndex_);
}

void AnalysisView::onFrameInputChanged() {
    bool ok;
    double relativeValue = frameInput_->text().toDouble(&ok);
    if (ok) {
        seekToRelativeFrame(relativeValue);
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
        setPlaybackPlaying(true);
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
        setPlaybackPlaying(false);
        
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
    setPlaybackPlaying(false);
    
    // Enable controls now that we have data
    updatePlaybackControlsState();
    updateSliderZeroMarker();
    
    // Force update of the view to show the trigger frame
    onSliderMoved(0);
}


void AnalysisView::updatePlaybackControlsState() {
    bool hasData = !recordedSequence_.empty() || isStreamingMode_;
    const bool canPlay = hasData && totalFrames_ > 0;
    const bool atStart = !hasData || currentFrame_ <= 0.0;
    const bool atEnd = !hasData || currentFrame_ >= totalFrames_;
    
    // Enable/disable all playback controls
    playbackSlider_->setEnabled(hasData);
    playPauseButton_->setEnabled(canPlay);
    beginButton_->setEnabled(hasData && !atStart);
    prevButton_->setEnabled(hasData && !atStart);
    resetButton_->setEnabled(hasData);
    nextButton_->setEnabled(hasData && !atEnd);
    endButton_->setEnabled(hasData && !atEnd);
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
    setPlaybackPlaying(false);
    recordedSequence_.clear();

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

void AnalysisView::updatePlaybackInfoLabel() {
    if (!playbackInfoLabel_) {
        return;
    }

    if (!isReviewMode_) {
        playbackInfoLabel_->setText("Frame -- | Time --");
        return;
    }

    const int frameIndex = currentReviewFrameIndex();
    const double relFrames = frameIndex - triggerFrameIndex_;
    double fps = 0.0;
    if (!videoReaders_.empty() && videoReaders_.begin()->second) {
        fps = videoReaders_.begin()->second->getFps();
    }
    if (fps <= 0.0) {
        fps = 1.0;
    }
    const double seconds = relFrames / fps;
    playbackInfoLabel_->setText(QString("Frame %1 | Time %2%3 s")
        .arg(frameIndex)
        .arg(seconds >= 0.0 ? "+" : "")
        .arg(seconds, 0, 'f', 3));
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
    int handleWidth = 12;  // From playback slider stylesheet
    int usableWidth = sliderRect.width() - handleWidth;
    int zeroValue = 0;  // The trigger frame is always at value 0
    
    // Map value to pixel position
    float ratio = static_cast<float>(zeroValue - sliderMin) / (sliderMax - sliderMin);
    int xPos = sliderRect.x() + (handleWidth / 2) + static_cast<int>(ratio * usableWidth);
    int yPos = sliderRect.y() - 16;  // Keep the zero marker above annotation dots and slider handle
    
    // Position and show the marker
    sliderZeroMarker_->move(xPos - 5, yPos);  // Center the 10px wide marker
    sliderZeroMarker_->show();
}

void AnalysisView::updateAnnotationSliderMarkers() {
    for (QLabel* marker : annotationSliderMarkers_) {
        if (marker) {
            marker->deleteLater();
        }
    }
    annotationSliderMarkers_.clear();

    if (!isReviewMode_ || !playbackSlider_ || eventAnnotations_.isEmpty()) {
        return;
    }

    const int sliderMin = playbackSlider_->minimum();
    const int sliderMax = playbackSlider_->maximum();
    if (sliderMax == sliderMin) {
        return;
    }

    const QRect sliderRect = playbackSlider_->geometry();
    const int handleWidth = 12;
    const int usableWidth = std::max(1, sliderRect.width() - handleWidth);
    // Draw annotation dots below the slider groove so they don't crowd the orange zero marker.
    const int yPos = sliderRect.y() + sliderRect.height() + 2;

    const bool detailTabActive = tabWidget_ && tabWidget_->currentIndex() == 1 && selectedCameraId_ >= 0;
    const QString selectedCameraPrefix = detailTabActive
        ? QString("cam%1_").arg(selectedCameraId_ + 1)
        : QString();

    QSet<int> framesWithMarkers;
    QHash<int, QStringList> frameCameraLabels;
    for (auto it = eventAnnotations_.begin(); it != eventAnnotations_.end(); ++it) {
        const QString key = it.key();
        if (detailTabActive && !key.startsWith(selectedCameraPrefix)) {
            continue;
        }
        const int framePos = key.indexOf("_frame");
        if (framePos < 0) {
            continue;
        }
        bool ok = false;
        const int frameIndex = key.mid(framePos + 6).toInt(&ok);
        if (ok) {
            framesWithMarkers.insert(frameIndex);
            const QString cameraLabel = key.left(framePos);
            frameCameraLabels[frameIndex].append(cameraLabel);
        }
    }

    for (int frameIndex : framesWithMarkers) {
        const int sliderValue = static_cast<int>((frameIndex - triggerFrameIndex_) * 10);
        if (sliderValue < sliderMin || sliderValue > sliderMax) {
            continue;
        }
        const double ratio = static_cast<double>(sliderValue - sliderMin) / (sliderMax - sliderMin);
        const int xPos = sliderRect.x() + (handleWidth / 2) + static_cast<int>(ratio * usableWidth);

        QLabel* marker = new QLabel(playbackPanel_);
        marker->setFixedSize(7, 7);
        const QString cameraText = frameCameraLabels.value(frameIndex).join(", ").replace("cam", "Camera ");
        marker->setToolTip(detailTabActive
            ? QString("Marker on Camera %1, frame %2").arg(selectedCameraId_ + 1).arg(frameIndex)
            : QString("Marker on frame %1 (%2)").arg(frameIndex).arg(cameraText));
        marker->setStyleSheet("background-color: #FF3B30; border: 1px solid white; border-radius: 3px;");
        marker->move(xPos - 3, yPos);
        marker->setCursor(Qt::PointingHandCursor);
        marker->installEventFilter(this);
        marker->setProperty("annotationFrame", frameIndex);
        marker->raise();
        marker->show();
        annotationSliderMarkers_.append(marker);
    }
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
        seekToRelativeFrame(currentFrame_ - triggerFrameIndex_);
        setPlaybackPlaying(false);
        return;
    }
    
    // Update UI (Slider value is relative)
    double relativeFrame = currentFrame_ - triggerFrameIndex_;
    
    const bool sliderBlocked = playbackSlider_->blockSignals(true);
    playbackSlider_->setValue(static_cast<int>(std::round(relativeFrame * 10.0)));
    playbackSlider_->blockSignals(sliderBlocked);
    
    // Force view update:
    // In Review Mode, the valueChanged signal triggers onSliderValueChanged,
    // but onSliderValueChanged deliberately ignores updates while playing to prevent
    // slider drag interference. So we must explicitly call onSliderMoved here
    // to fetch and display the new frames.
    onSliderMoved(static_cast<int>(std::round(relativeFrame * 10.0)));
}

void AnalysisView::addPaperBreakEvent(const std::string& timestamp, int triggerIndex, int totalFrames) {
    QString rawTs = QString::fromStdString(timestamp);
    latestAddedEventTimestamp_ = rawTs;
    reloadEventTables();
    selectLatestEvent();
    
    // Load RAW BINARY from disk (using new per-camera format base)
    QString binPath = QDir(CameraConfig::getEventStoragePath()).filePath(QString("event_%1_cam1.bin").arg(rawTs));
    startReviewFromFile(binPath, triggerIndex);
    
}

void AnalysisView::addEventRow(const QString& timestamp, const QString& reason, bool permanent, bool selectRow) {
    QTableWidget* targetTable = permanent ? permanentPaperBreakTable_ : paperBreakTable_;
    const int row = targetTable->rowCount();
    targetTable->insertRow(row);

    const bool isNewRecentEvent = !permanent && !latestAddedEventTimestamp_.isEmpty() && timestamp == latestAddedEventTimestamp_;
    QTableWidgetItem* timeItem = new QTableWidgetItem(formatTimestamp(timestamp));
    QTableWidgetItem* reasonItem = new QTableWidgetItem(reason);
    timeItem->setData(Qt::UserRole, timestamp);
    timeItem->setData(Qt::UserRole + 2, permanent);
    timeItem->setData(Qt::UserRole + 3, isNewRecentEvent);
    reasonItem->setData(Qt::UserRole + 2, permanent);
    reasonItem->setData(Qt::UserRole + 3, isNewRecentEvent);

    if (isNewRecentEvent) {
        ThemeColors tc = CameraConfig::getThemeColors();
        QPixmap dotPixmap(6, 6);
        dotPixmap.fill(Qt::transparent);
        QPainter painter(&dotPixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(tc.primary));
        painter.drawEllipse(0, 0, 5, 5);
        painter.end();
        timeItem->setIcon(QIcon(dotPixmap));

        QFont newEventFont = timeItem->font();
        newEventFont.setBold(true);
        timeItem->setFont(newEventFont);
        reasonItem->setFont(newEventFont);
    }

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
    updateRecordCountLabel();
}

void AnalysisView::updateRecordCountLabel() {
    if (recentRecordsLabel_) {
        recentRecordsLabel_->setText(QString("Recent Records (%1)").arg(paperBreakTable_->rowCount()));
    }

    if (permanentRecordsLabel_) {
        permanentRecordsLabel_->setText(QString("Permanent Storage (%1)").arg(permanentPaperBreakTable_->rowCount()));
    }
}

void AnalysisView::reloadEventStorage() {
    EventDatabase::instance().initialize(CameraConfig::getEventStoragePath());
    reloadEventTables();
    selectLatestEvent();
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
    leftSidebar_->setStyleSheet(makeSidebarPanelStyle(tc));
    
    // 1. Sidebar Buttons
    serverButton_->setStyleSheet(makeSidebarStateButtonStyle(tc, serverRunning_));
    
    adminButton_->setStyleSheet(makeSidebarPrimaryButtonStyle(tc));
    
    deleteButton_->setStyleSheet(makeSidebarDangerButtonStyle(tc));

    permanentButton_->setStyleSheet(makeSidebarActionButtonStyle(tc));
    
    // 2. Playback and tab surface/typography
    // First, ensure the current disabled/enabled state uses the new colors
    updatePlaybackControlsState();
    
    applyAnalysisViewStyle();
    
    if (diagnosticTab_) {
        // Rebuild the single-camera tab only when a valid camera is selected.
        updateDynamicTab(selectedCameraId_);
    }
    
    togglePermanentTableButton_->setStyleSheet(makeSidebarUtilityButtonStyle(tc));

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
    updateAnnotationSliderMarkers();
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

    isPlaying_ = false;
    if (playbackTimer_) {
        playbackTimer_->stop();
    }
    isReviewMode_ = false;
    isStreamingMode_ = false;
    currentFrame_ = 0;
    triggerFrameIndex_ = 0;

    // Cancel async loading before releasing frame/video storage.
    if (tiffLoaderWatcher_ && tiffLoaderWatcher_->isRunning()) {
        tiffLoaderWatcher_->cancel();
        tiffLoaderWatcher_->waitForFinished();
    }

    // Reset UI with slider signals blocked to avoid re-entering review rendering while
    // readers/metadata are being released.
    if (playbackSlider_) {
        const bool blocked = playbackSlider_->blockSignals(true);
        playbackSlider_->setRange(0, 10000);
        playbackSlider_->setValue(0);
        playbackSlider_->blockSignals(blocked);
    }
    if (frameInput_) {
        frameInput_->setText("0.0");
    }
    updatePlaybackInfoLabel();
    if (sliderZeroMarker_) {
        sliderZeroMarker_->hide();
    }

    totalFrames_ = 1000;

    // Clear camera widgets before closing readers so the UI no longer references review frames.
    QImage empty;
    for (int i = 0; i < static_cast<int>(cameraWidgets_.size()); ++i) {
        auto* widget = cameraWidgets_[i];
        if (widget) {
            widget->setTitle(CameraConfig::getCameraLabel(i));
            widget->setFrame(empty);
            widget->setTimestamp("");
        }
    }
    if (selectedCameraWidget_) {
        const int selectedCameraId = selectedCameraWidget_->getCameraId();
        selectedCameraWidget_->setTitle(selectedCameraId >= 0 ? CameraConfig::getCameraLabel(selectedCameraId)
                                                              : QString("Select a camera"));
        selectedCameraWidget_->setFrame(empty);
        selectedCameraWidget_->setTimestamp("");
    }

    recordedSequence_.clear();
    frameMetadata_.clear();
    videoReaders_.clear();
    currentAnnotationPath_.clear();
    currentEventCameraLabels_.clear();
    const int configuredCameraCount = CameraConfig::getCameraCount();
    if (configuredCameraCount > 0 && configuredCameraCount != static_cast<int>(cameraWidgets_.size())) {
        setCameraCount(configuredCameraCount);
    }
    eventAnnotations_ = QJsonObject();
    updateAnnotationSliderMarkers();
    updatePlaybackControlsState();

    std::cout << "[AnalysisView] Data cleared." << std::endl;
}

QString AnalysisView::currentEventCameraLabel(int cameraId) const {
    if (cameraId >= 0 && cameraId < currentEventCameraLabels_.size()) {
        const QString savedLabel = currentEventCameraLabels_.at(cameraId).trimmed();
        if (!savedLabel.isEmpty()) {
            return savedLabel;
        }
    }
    if (cameraId >= 0 && cameraId < CameraConfig::getCameraCount()) {
        return CameraConfig::getCameraLabel(cameraId);
    }
    return QString("CAM-%1").arg(cameraId + 1, 2, 10, QChar('0'));
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

bool AnalysisView::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        QWidget* widget = qobject_cast<QWidget*>(watched);
        if (widget && widget->property("annotationFrame").isValid()) {
            const int frameIndex = widget->property("annotationFrame").toInt();
            seekToRelativeFrame(frameIndex - triggerFrameIndex_);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
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
    table->setItemDelegate(new LogSelectionDelegate(table));
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
    table->setColumnWidth(0, 154);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setShowGrid(false);
    table->setFrameShape(QFrame::NoFrame);
    table->setSortingEnabled(false);
    table->horizontalHeader()->setSortIndicatorShown(true);
    table->horizontalHeader()->setSectionsClickable(true);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(24);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(deleteMode ? QAbstractItemView::MultiSelection : QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setFocusPolicy(Qt::NoFocus);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
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
    latestTable->setCurrentCell(0, 0);
    latestTable->scrollToItem(latestTable->item(0, 0));
    suppressNewEventIndicatorClear_ = true;
    onLogSelected(0, 1);
    suppressNewEventIndicatorClear_ = false;
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
    updateRecordCountLabel();
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
    titleLabel->setToolTip("Shows live camera health. RAM Frames are EventController ring-buffer frames currently held in host RAM. Drops/Stream Health are based on incomplete grabs. Basler guidance: tune Packet Size, Inter-Packet Delay, bandwidth, AOI, and exposure time if drops occur or resulting FPS falls below acquisition FPS.");
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
        "RAM Frames", "RAM [MB]", "Drops", "Stream Health"
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
    diagTable_->setColumnWidth(9, 95);   // RAM Frames
    diagTable_->setColumnWidth(10, 85);  // RAM MB
    diagTable_->setColumnWidth(11, 90);  // Drops
    diagTable_->setColumnWidth(12, 140); // Stream Health
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

    // Start auto-refresh by default.
    if (diagAutoRefreshChk_->isChecked()) {
        diagRefreshTimer_->start();
    }
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
            for (int col = 2; col <= 12; ++col)
                diagTable_->setItem(row, col, makeNA());
            applyRowColors(row, QColor(55, 55, 55, 140), QColor(tc.border));
            continue;
        }

        // --- Live data from CameraManager (if available) ---
        double temperature = std::numeric_limits<double>::quiet_NaN();
        double fps   = 0.0;
        CameraManager::CameraParams p;
        bool isConnected = false;
        uint64_t dropCount = 0;
        uint64_t consecutiveDrops = 0;

        if (cameraManager_ && configIndex >= 0) {
            isConnected  = cameraManager_->isCameraConnected(configIndex) || cameraManager_->isCameraOpen(configIndex);
            if (isConnected) {
                temperature = cameraManager_->getTemperature(configIndex);
                fps         = cameraManager_->getCameraFps(configIndex);
                p           = cameraManager_->getCameraParams(configIndex);
                dropCount   = cameraManager_->getIncompleteGrabCount(configIndex);
                consecutiveDrops = cameraManager_->getConsecutiveIncompleteGrabCount(configIndex);
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
        diagTable_->setItem(row, 4, (cameraManager_ && isConnected)
            ? makeItem(QString::number(p.exposureUs, 'f', 0))
            : makeNA());

        // Col 5: Gain
        diagTable_->setItem(row, 5, (cameraManager_ && isConnected)
            ? makeItem(QString::number(p.gain, 'f', 2))
            : makeNA());

        // Col 6: Gamma
        diagTable_->setItem(row, 6, (cameraManager_ && isConnected)
            ? makeItem(QString::number(p.gamma, 'f', 2))
            : makeNA());

        // Col 7: WDR High
        diagTable_->setItem(row, 7, (cameraManager_ && isConnected && !std::isnan(p.wdrHigh))
            ? makeItem(QString::number(p.wdrHigh, 'f', 2))
            : makeNA());

        // Col 8: WDR Low
        diagTable_->setItem(row, 8, (cameraManager_ && isConnected && !std::isnan(p.wdrLow))
            ? makeItem(QString::number(p.wdrLow, 'f', 2))
            : makeNA());

        // Col 9: RAM Frames (EventController ring buffer fill/capacity)
        const size_t ramFrames = EventController::instance().getBufferedFrameCount(info.id);
        const size_t ramCapacity = EventController::instance().getBufferCapacity(info.id);
        if (ramCapacity > 0) {
            auto* ramItem = makeItem(QString("%1 / %2").arg(ramFrames).arg(ramCapacity));
            ramItem->setToolTip("Frames currently held in host RAM ring buffer (pre/post-trigger).");
            diagTable_->setItem(row, 9, ramItem);
        } else {
            diagTable_->setItem(row, 9, makeItem("0 / 0"));
        }

        // Col 10: RAM [MB] — ramFrames × W × H × bpp / 1 048 576
        const int width = (p.width > 0) ? p.width : info.width;
        const int height = (p.height > 0) ? p.height : info.height;
        int bpp = (p.bpp > 0) ? p.bpp : 1;
        if (bpp <= 1) {
            const QString fmt = info.pixelFormat.toUpper();
            if (fmt.contains("12") || fmt.contains("16")) {
                bpp = 2;
            } else if (fmt.contains("RGB") || fmt.contains("BGR")) {
                bpp = 3;
            }
        }

        if (width > 0 && height > 0) {
            double mb = static_cast<double>(ramFrames) * width * height * bpp / (1024.0 * 1024.0);
            diagTable_->setItem(row, 10, makeItem(QString::number(mb, 'f', 2)));
        } else {
            diagTable_->setItem(row, 10, makeNA());
        }

        const QString dropText = (cameraManager_ && isConnected)
            ? QString("%1 / %2").arg(dropCount).arg(consecutiveDrops)
            : QString("N/A");
        diagTable_->setItem(row, 11, (cameraManager_ && isConnected)
            ? makeItem(dropText)
            : makeNA());

        const QString healthText = (cameraManager_ && isConnected)
            ? (consecutiveDrops > 0 ? QString("WARNING") : (dropCount > 0 ? QString("Recovered") : QString("OK")))
            : QString("Offline");
        diagTable_->setItem(row, 12, (cameraManager_ && isConnected)
            ? makeItem(healthText)
            : makeItem(healthText));

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

        QTableWidgetItem* healthItem = diagTable_->item(row, 12);
        if (healthItem) {
            healthItem->setToolTip("OK: no incomplete grabs. Recovered: drops occurred earlier. WARNING: ongoing incomplete grabs (usually bandwidth/packet timing/exposure load issue).");
            if (healthText == "WARNING") {
                healthItem->setBackground(QColor(0xFF, 0xAA, 0x00, 180));
                healthItem->setForeground(QColor("#1A1A1A"));
            } else if (healthText == "Recovered") {
                healthItem->setBackground(QColor(0xFF, 0xD3, 0x6B, 140));
                healthItem->setForeground(QColor("#2B2B2B"));
            } else if (healthText == "OK") {
                healthItem->setBackground(QColor(0x57, 0xD3, 0x7C, 140));
                healthItem->setForeground(QColor("#103218"));
            } else {
                healthItem->setBackground(QColor(0x90, 0x35, 0x35, 120));
                healthItem->setForeground(QColor("#F2C2C2"));
            }
        }
    }
}
