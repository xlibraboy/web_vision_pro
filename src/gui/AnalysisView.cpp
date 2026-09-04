#include "AnalysisView.h"
#include <cmath>
#include <cstdlib>
#include <QKeyEvent>
#include <QMouseEvent>
#include "widgets/AnalysisVideoWidget.h"
#include "../config/CameraConfig.h"
#include "../core/EventController.h"
#include "../core/EventDatabase.h"
#include "../core/SpeedProfile.h"
#include "../core/VideoStreamReader.h"
#include "../processing/EventSignalScanner.h"
#include "widgets/EventDashboard.h"
#include <QApplication>
#include <QCursor>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QScrollBar>
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
#include <QPointer>
#include <QProgressBar>
#include <QKeySequence>
#include <QFrame>
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QPolygonF>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <algorithm>
#include <limits>

class LogSelectionDelegate : public QStyledItemDelegate {
public:
    explicit LogSelectionDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);

        // The newest-event row carries a pulsing/static tint. The stylesheet
        // rule "QTableWidget::item:selected { background-color: ... }" would
        // mask that tint whenever the row is selected (the table auto-selects
        // the newest event), so paint this cell manually — tint background +
        // selection accent bar + text — instead of letting the base paint run.
        if (index.data(Qt::UserRole + 3).toBool()) {
            ThemeColors tc = CameraConfig::getThemeColors();
            painter->save();
            painter->setClipRect(opt.rect);
            painter->setFont(opt.font);
            if (opt.backgroundBrush.style() != Qt::NoBrush) {
                painter->fillRect(opt.rect, opt.backgroundBrush);
            } else {
                painter->fillRect(opt.rect, QColor(tc.bg));
            }
            if (opt.state & QStyle::State_Selected) {
                if (index.column() == 0) {
                    painter->fillRect(QRect(opt.rect.left(), opt.rect.top() + 1, 3, opt.rect.height() - 2), QColor(tc.primary));
                }
                painter->setPen(QColor(Qt::white)); // matches the :selected text color
            } else {
                painter->setPen(QColor(tc.text));
            }
            // Mirror the stylesheet's QTableWidget::item padding (4px) and the
            // header's left/vertical-center alignment. Elide long text with an
            // ellipsis exactly like the base item rendering does.
            const QRect textRect = opt.rect.adjusted(4, 4, -4, -4);
            const QString elided = opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, textRect.width());
            painter->drawText(textRect, opt.displayAlignment, elided);
            painter->restore();
            return;
        }

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

// Render a vertically-oriented label pixmap (text rotated 90° so it reads
// top-to-bottom) used for the compact TOOLS tab on the frame's right edge.
// Small pixel size + tight letter spacing keep the handle slim.
static QPixmap makeVerticalTabPixmap(const QString& text, const QColor& color,
                                     int w, int h) {
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = p.font();
    f.setPixelSize(9);
    f.setBold(true);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    p.setFont(f);
    p.setPen(color);
    p.translate(w / 2.0, h / 2.0);
    p.rotate(90);
    const QRect textRect(-h / 2, -w / 2, h, w);
    p.drawText(textRect, Qt::AlignCenter, text);
    return pm;
}

// Right tools panel animation constant: drawer slide distance on show/hide.
// (No drop-shadow margin is needed — the panel uses no graphics effect while
// idle, so nesting artifacts on X11 are avoided entirely.)
static constexpr int kToolsSlidePx = 16;

// Render a small trend sparkline from a series of samples. The highest
// sample maps to ~90% of the height; the line is colored by severity.
static QPixmap makeSparklinePixmap(const std::vector<double>& samples,
                                   const QColor& lineColor,
                                   int width = 56, int height = 18)
{
    QPixmap pm(width, height);
    pm.fill(Qt::transparent);
    if (samples.size() < 2)
        return pm;

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    double maxV = 0.5; // floor so sub-1 drops/s micro-trends stay visible
    for (double v : samples)
        if (v > maxV) maxV = v;

    QPolygonF poly;
    poly.reserve(static_cast<int>(samples.size()));
    const double plotH = height - 4.0; // leave headroom above the peak
    for (size_t i = 0; i < samples.size(); ++i) {
        const double x = 1.0 + (width - 2.0) * static_cast<double>(i) / static_cast<double>(samples.size() - 1);
        const double y = 2.0 + plotH * (1.0 - samples[i] / maxV);
        poly << QPointF(x, y);
    }

    // Soft area fill under the curve
    QPainterPath area(poly.first());
    for (int i = 1; i < poly.size(); ++i)
        area.lineTo(poly.at(i));
    area.lineTo(poly.last().x(), height - 1);
    area.lineTo(poly.first().x(), height - 1);
    area.closeSubpath();
    QColor fill = lineColor;
    fill.setAlpha(60);
    p.fillPath(area, fill);

    // Line + endpoint dot
    p.setPen(QPen(lineColor, 1.4));
    p.drawPolyline(poly);
    p.setBrush(lineColor);
    p.setPen(Qt::NoPen);
    p.drawEllipse(poly.last(), 2.0, 2.0);
    return pm;
}

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
        "  padding: 3px 8px;"
        "  font-size: 11px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background-color: %4; border-color: %5; }"
        "QPushButton:pressed { background-color: %5; color: %6; }"
    ).arg(bg, text, border, hover, tc.primary, tc.bg);
}

// Amber "busy" treatment used while camera startup is in progress.
static QString makeSidebarConnectingButtonStyle(const ThemeColors& tc) {
    return QString(
        "QPushButton {"
        "  background-color: #6B4F12;"
        "  color: #FFD98A;"
        "  border: 1px solid #B8860B;"
        "  border-radius: 6px;"
        "  padding: 3px 8px;"
        "  font-size: 11px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background-color: #7A5C1A; border-color: #D4A017; }"
        "QPushButton:pressed { background-color: #5A4210; }"
        "QPushButton:disabled { background-color: %1; color: %2; border-color: %2; }"
    ).arg(tc.btnBg, tc.border);
}

static QString makeSidebarPrimaryButtonStyle(const ThemeColors& tc) {
    return QString(
        "QPushButton {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %1;"
        "  border-radius: 6px;"
        "  padding: 3px 8px;"
        "  font-size: 11px;"
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
        "  padding: 3px 8px;"
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
        "  padding: 2px 8px;"
        "  font-size: 10px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover { background-color: %4; color: %5; border-color: %5; }"
        "QPushButton:pressed { background-color: %4; }"
        "QPushButton:checked { background-color: %1; color: %5; border-color: %5; }"
    ).arg(tc.btnBg, quietText, tc.border, tc.bg, tc.primary);
}

// Combined "Mark Permanent | Delete Selected" segmented control: one rounded
// container with a 1px vertical divider; each segment keeps its own hover and
// pressed color so the two actions read as a single compact row.
static QString makeSidebarSplitActionStyle(const ThemeColors& tc) {
    const QString divider = QColor(tc.border).lighter(112).name();
    const QString muted = QColor(tc.text).lighter(135).name();
    const QString accentText = QColor(tc.primary).lighter(120).name();
    const QString pressedBg = QColor(tc.primary).darker(150).name();
    return QString(
        "QWidget#splitActionRow {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 6px;"
        "}"
        "QFrame#splitActionDivider {"
        "  background-color: %3;"
        "  border: none;"
        "  min-width: 1px;"
        "  max-width: 1px;"
        "}"
        "QPushButton#permanentActionButton {"
        "  background: transparent;"
        "  border: none;"
        "  border-top-left-radius: 5px;"
        "  border-bottom-left-radius: 5px;"
        "  color: %4;"
        "  font-size: 10px;"
        "  font-weight: 700;"
        "  padding: 2px 8px;"
        "}"
        "QPushButton#permanentActionButton:hover { background-color: %5; color: %6; }"
        "QPushButton#permanentActionButton:pressed { background-color: %7; color: %6; }"
        "QPushButton#permanentActionButton:disabled { color: %8; background: transparent; }"
        "QPushButton#deleteActionButton {"
        "  background: transparent;"
        "  border: none;"
        "  border-top-right-radius: 5px;"
        "  border-bottom-right-radius: 5px;"
        "  color: #FFB4B4;"
        "  font-size: 10px;"
        "  font-weight: 700;"
        "  padding: 2px 8px;"
        "}"
        "QPushButton#deleteActionButton:hover { background-color: #982B2B; color: #FFF5F5; }"
        "QPushButton#deleteActionButton:pressed { background-color: #7C2020; color: #FFF5F5; }"
        "QPushButton#deleteActionButton:disabled { color: %8; background: transparent; }"
    ).arg(tc.btnBg, tc.border, divider, accentText, tc.btnHover, tc.text, pressedBg, muted);
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
        "  padding: 0 8px;"
        "  font-size: 11px;"
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
        "QSlider::groove:horizontal { height: 2px; background: %1; border-radius: 1px; }"
        "QSlider::sub-page:horizontal { background: %2; border-radius: 1px; }"
        "QSlider::add-page:horizontal { background: %3; border-radius: 1px; }"
        "QSlider::handle:horizontal { width: 10px; height: 10px; margin: -4px 0; background: %4; border: 1px solid %2; border-radius: 5px; }"
        "QSlider::handle:horizontal:hover { background: %5; border-color: %5; }"
        "QSlider::handle:horizontal:pressed { background: %5; }"
        "QSlider::groove:horizontal:disabled { background: %6; }"
        "QSlider::sub-page:horizontal:disabled { background: %6; }"
        "QSlider::add-page:horizontal:disabled { background: %1; }"
        "QSlider::handle:horizontal:disabled { background: %1; border-color: %6; }"
    ).arg(tc.border, tc.primary, tc.btnHover, tc.handle, QColor(tc.handle).lighter(115).name(), tc.bg);
}

static constexpr int kReviewSliderUnitsPerSecond = 1000;

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
        "QHeaderView::section { background-color: %1; color: #E0E0E0; padding: 6px 4px; border: none; border-bottom: 1px solid %3; text-align: left; font-size: 10px; font-family: 'Noto Sans', 'DejaVu Sans', sans-serif; font-weight: 700; }"
        "QHeaderView::section:checked, QHeaderView::section:pressed, "
        "QHeaderView::section:hover, QHeaderView::section:disabled "
        "{ background-color: %1; color: #E0E0E0; }"
        "QTableCornerButton::section { background-color: %1; border: none; border-bottom: 1px solid %3; }"
        "QTableWidget::item { padding: 4px 4px; border: none; color: #E0E0E0; font-size: 11px; font-family: 'Noto Sans', 'DejaVu Sans', sans-serif; }"
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
    
    // Accept keyboard focus so media-player keys (Left/Right step, Space
    // play/pause) work even when the user clicks an empty area of the view.
    setFocusPolicy(Qt::StrongFocus);
    
    // Readers initialized per camera dynamically
    
    // Setup playback timer
    playbackTimer_ = new QTimer(this);
    connect(playbackTimer_, &QTimer::timeout, this, &AnalysisView::onPlaybackTick);
    
    // Server-button Connecting… animation (animated ellipsis).
    serverConnectingTimer_ = new QTimer(this);
    serverConnectingTimer_->setInterval(400);
    connect(serverConnectingTimer_, &QTimer::timeout, this, &AnalysisView::onServerConnectingTick);

    // New-event row highlight pulse animation (bright flash decaying onto the
    // static tint). Started from addEventRow, stopped automatically when the
    // row is no longer the live new-event row.
    newEventPulseTimer_ = new QTimer(this);
    newEventPulseTimer_->setInterval(kNewEventPulseTickMs);
    connect(newEventPulseTimer_, &QTimer::timeout, this, &AnalysisView::onNewEventPulseTick);

    // Multi-digit camera entry: typed digits accumulate until this fires, then
    // the requested camera opens (see handlePlayerCameraKey).
    cameraKeyTimer_ = new QTimer(this);
    cameraKeyTimer_->setSingleShot(true);
    cameraKeyTimer_->setInterval(kCameraKeyEntryDelayMs);
    connect(cameraKeyTimer_, &QTimer::timeout, this, &AnalysisView::resolveCameraKeyEntry);
    
    setupUI();
    
    // Initialize EventDatabase and load historical events
    EventDatabase::instance().initialize(CameraConfig::getEventStoragePath());
    reloadEventTables();
    selectLatestEvent();
    
    // Register callback for EventController
    // Callback receives metadata only - frames loaded from disk to avoid memory spike
    EventController::instance().setEventSavedCallback([this](const std::string& timestamp, int triggerIndex, int totalFrames, int primaryCameraId) {
        QMetaObject::invokeMethod(this, [this, timestamp, triggerIndex, totalFrames, primaryCameraId]() {
            addPaperBreakEvent(timestamp, triggerIndex, totalFrames, primaryCameraId);
        }, Qt::QueuedConnection);
    });

    // Placeholder row the moment a trigger is accepted (recording still in
    // progress); replaced by the real row when the save callback arrives.
    EventController::instance().setEventTriggeredCallback([this](const std::string& timestamp, const QString& reason) {
        QMetaObject::invokeMethod(this, [this, timestamp, reason]() {
            addPendingEventRow(QString::fromStdString(timestamp), reason);
        }, Qt::QueuedConnection);
    });
}

AnalysisView::~AnalysisView() {}

void AnalysisView::setupUI() {
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 0);
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
    videoReaderPaths_.clear();
    pendingScanPaths_.clear();
    cameraTimestamps_.clear();
    timelineCameraIdx_ = -1;
    
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
                videoReaderPaths_[i] = camPath;
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
    cameraFrameOffsets_.assign(eventCameraCount, 0);

    currentEventInfo_ = EventDatabase::EventInfo();
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
            currentEventInfo_ = EventDatabase::instance().getEventInfo(tsStr);
            currentEventCameraLabels_ = currentEventInfo_.cameraLabels;
        } catch (...) {
            currentEventInfo_ = EventDatabase::EventInfo();
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
    
    // The review timeline spans the LONGEST recording: with per-camera
    // acquisition fps enabled, a 125 fps camera saves ~2.5x the frames of a
    // 50 fps camera for the same wall-clock event window. Bounding the
    // playhead to the first reader's frame count made such events impossible
    // to play or scrub to the end (the all-camera and single-camera views
    // share this one timeline). Shorter cameras hold their last frame once
    // their file runs out (VideoStreamReader::getFrame returns empty).
    totalFrames_ = 0;
    for (auto& pair : videoReaders_) {
        totalFrames_ = std::max(totalFrames_,
            static_cast<double>(pair.second->getTotalFrames() - 1));
    }

    const QMap<QString, int> parsedOffsets = loadEventAnnotations(videoPath);
    const int maxOffset = static_cast<int>(std::floor(totalFrames_));
    for (auto it = parsedOffsets.begin(); it != parsedOffsets.end(); ++it) {
        const int camIndex = it.key().mid(3).toInt() - 1;
        if (camIndex < 0 || camIndex >= static_cast<int>(cameraFrameOffsets_.size())) {
            continue;
        }
        cameraFrameOffsets_[camIndex] = qBound(-maxOffset, it.value(), maxOffset);
    }

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

    // Load per-frame timestamp/frame counter metadata from the LONGEST
    // available RAW reader. New RAW layout stores pixels first and
    // FrameMetadata second for each frame. The time axis must span the whole
    // event: with per-camera acquisition fps the first reader's metadata
    // would end the relative-time scrubber before the longest recording does.
    frameMetadata_.clear();
    metadataTriggerIndex_ = -1;
    cameraTimestamps_.clear();
    timelineCameraIdx_ = -1;
    // Only recordings with real pixels may define the timeline: a camera
    // saved while starting can produce a header-valid but 0-width file whose
    // "metadata" is garbage and whose frame count outranks healthy cameras
    // (it would wreck the relative-time slider). Fall back to the widest
    // usable reader.
    VideoStreamReader* timelineReader = nullptr;
    for (auto& pair : videoReaders_) {
        if (pair.second->getWidth() <= 0 || pair.second->getTotalFrames() <= 0) {
            continue;
        }
        if (!timelineReader || pair.second->getTotalFrames() > timelineReader->getTotalFrames()) {
            timelineReader = pair.second.get();
            timelineCameraIdx_ = pair.first;
        }
    }
    // Cache every camera's per-frame hardware timestamps once (random-access
    // seeks, done a single time per event load): the mixed-fps scrub mapping
    // converts a timeline frame's timestamp to each camera's nearest own
    // frame, see displayedFrameIndexForCamera. Non-RAW readers return false
    // immediately and stay absent from the map (legacy fallback).
    for (auto& pair : videoReaders_) {
        const int count = pair.second->getTotalFrames();
        if (count <= 0 || pair.second->getWidth() <= 0) {
            continue;  // header-only/empty recording: no usable timestamps
        }
        std::vector<int64_t> ts;
        ts.reserve(count);
        for (int i = 0; i < count; ++i) {
            ::FrameMetadata rawMeta = {};
            if (!pair.second->getFrameMetadata(i, rawMeta)) {
                break;
            }
            ts.push_back(static_cast<int64_t>(rawMeta.timestamp));
        }
        if (static_cast<int>(ts.size()) == count) {
            cameraTimestamps_[pair.first] = std::move(ts);
        }
    }
    if (timelineReader) {
        const int frameCount = timelineReader->getTotalFrames();
        frameMetadata_.reserve(frameCount);
        for (int i = 0; i < frameCount; ++i) {
            ::FrameMetadata rawMeta = {};
            if (!timelineReader->getFrameMetadata(i, rawMeta)) {
                break;
            }
            // The raw header flags exactly one frame per camera file as the
            // trigger frame; keep this reader's own so the relative-time axis
            // starts at the true trigger instant even when this reader is not
            // the event's primary camera.
            if (metadataTriggerIndex_ < 0 && (rawMeta.flags & 1u) != 0u) {
                metadataTriggerIndex_ = i;
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
    
    // Set trigger index. The scrub timeline is the LONGEST recording's frame
    // domain (per-camera acquisition fps), but the index passed in / stored in
    // the database is the PRIMARY camera's own frame index. Using it directly
    // would land the playhead before/after the true trigger instant whenever
    // those cameras differ (mixed-fps events) — which is why "Jump to Trigger"
    // did not match the zero/trigger flag above the slider (that flag marks
    // slider value 0, anchored at the timeline reader's own trigger instant).
    // The timeline reader's own flagged trigger frame is the authoritative
    // trigger in the timeline domain; keep the passed index only for legacy
    // events without RAW metadata (shared-index behavior).
    if (metadataTriggerIndex_ >= 0) {
        triggerFrameIndex_ = metadataTriggerIndex_;
    } else if (triggerIndex < 0 || triggerIndex > totalFrames_) {
        triggerFrameIndex_ = totalFrames_;
    } else {
        triggerFrameIndex_ = triggerIndex;
    }
    
    // Initial position is at trigger (0 relative)
    currentFrame_ = triggerFrameIndex_;
    
    // Update UI with relative range
    configureReviewSliderRange();
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
    updateAlignmentStatus();
    // Wire up the event dashboards and start signal scans.
    setupEventDashboards();

    std::cout << "[AnalysisView] Review loaded from file: " << totalFrames_ + 1
              << " frames, trigger at " << triggerFrameIndex_ << std::endl;

    // A new review supersedes any half-typed camera number from before.
    cancelCameraKeyEntry();
    // Hand keyboard control to the media player (Left/Right step, Space
    // play/pause, digit keys to jump cameras) right away instead of waiting
    // for the first click.
    setFocus();
}

namespace {

QImage matToThumbnailImage(const cv::Mat& m) {
    if (m.empty()) {
        return QImage();
    }
    if (m.channels() == 1) {
        QImage g(m.data, m.cols, m.rows, static_cast<int>(m.step), QImage::Format_Grayscale8);
        return g.copy();
    }
    QImage bgr(m.data, m.cols, m.rows, static_cast<int>(m.step), QImage::Format_BGR888);
    return bgr.copy();
}

} // namespace

void AnalysisView::setupEventDashboards() {
    // A review can be loaded several times in quick succession (auto-open of
    // the newest event, tab switches). Never cancel an in-flight scan that is
    // already working on one of THIS event's files — restarting it would
    // starve the dashboard. Only cancel scans for foreign files.
    if (signalScanner_ && signalScanner_->isRunning()) {
        const QString running = signalScanner_->currentBinPath();
        bool belongsToEvent = false;
        for (const auto& pair : videoReaderPaths_) {
            if (pair.second == running) {
                belongsToEvent = true;
                break;
            }
        }
        if (!belongsToEvent) {
            signalScanner_->cancel();
        }
    }
    if (thumbWatcher_ && thumbWatcher_->isRunning()) {
        thumbWatcher_->cancel();
        thumbWatcher_->waitForFinished();
    }
    pendingScanPaths_.clear();

    int total = 0;
    double fps = 0.0;
    // Timeline truth: the longest recording defines the scrub/playback frame
    // domain (see loadEventFromDisk), so its fps makes 1.0x real-time. Using
    // the first reader instead ran mixed-fps events at the wrong speed (e.g.
    // a 25 fps cam1 driving a 35 fps cam2 timeline = ~40% slow).
    auto timelineIt = videoReaders_.find(timelineCameraIdx_);
    if (timelineIt == videoReaders_.end() || !timelineIt->second) {
        timelineIt = videoReaders_.begin();
    }
    if (timelineIt != videoReaders_.end() && timelineIt->second) {
        total = timelineIt->second->getTotalFrames();
        fps = timelineIt->second->getFps();
    }
    // Playback truth: 1.0x = this event's real capture rate (timeline camera).
    reviewFps_ = fps;

    // Show the camera the user already selected; otherwise the first opened.
    currentDashCam_ = -1;
    if (selectedCameraId_ >= 0 && videoReaders_.count(selectedCameraId_)) {
        currentDashCam_ = selectedCameraId_;
    } else if (!videoReaders_.empty()) {
        currentDashCam_ = videoReaders_.begin()->first;
    }
    const QString label = currentDashCam_ >= 0
                              ? currentEventCameraLabel(currentDashCam_)
                              : QStringLiteral("CAM-01");

    EventDashboard* dash = detailDashboard_;
    if (dash) {
        dash->setEventData(label, total, triggerFrameIndex_, fps, {}, {}, {}, {}, {}, {}, {});
        dash->setCurrentFrame(triggerFrameIndex_);
        dash->setThumbnails({});
    }
    dashProgressPercent_ = -1;

    // Queue a signal scan for every opened camera file that has no result
    // yet (cache hits return instantly via finished()).
    if (!signalScanner_) {
        signalScanner_ = new EventSignalScanner(this);
        connect(signalScanner_, &EventSignalScanner::finished,
                this, &AnalysisView::onSignalScanFinished);
        connect(signalScanner_, &EventSignalScanner::failed,
                this, &AnalysisView::onSignalScanFailed);
        connect(signalScanner_, &EventSignalScanner::windowFinished,
                this, &AnalysisView::onDetailWindowFinished);
        connect(signalScanner_, &EventSignalScanner::windowFailed,
                this, &AnalysisView::onDetailWindowFailed);
        connect(signalScanner_, &EventSignalScanner::progress, this,
                [this](int scanned, int totalSteps) {
            if (!detailDashboard_) return;
            auto it = videoReaderPaths_.find(currentDashCam_);
            if (it == videoReaderPaths_.end()) return;
            if (signalScanner_->currentBinPath() != it->second) return;
            detailDashboard_->setLoadingSignals(true);
            const int pct = totalSteps > 0
                ? static_cast<int>(100LL * scanned / totalSteps) : -1;
            dashProgressPercent_ = pct;
            detailDashboard_->setSignalProgress(pct);
            if (dashLoadingLabel_ && dashLoadingLabel_->isVisible() && pct >= 0) {
                dashLoadingLabel_->setText(QStringLiteral("Analyzing event… %1%").arg(pct));
            }
        });
    }
    if (!thumbWatcher_) {
        thumbWatcher_ = new QFutureWatcher<QVector<QImage>>(this);
        connect(thumbWatcher_, &QFutureWatcher<QVector<QImage>>::finished,
                this, &AnalysisView::refreshDashboardThumbnails);
    }
    for (const auto& pair : videoReaderPaths_) {
        const QString& path = pair.second;
        if (signalByCam_.count(path) || pendingScanPaths_.contains(path)) {
            continue;
        }
        pendingScanPaths_.append(path);
    }
    startNextSignalScan();

    if (currentDashCam_ >= 0) {
        refreshDashboardForCamera(currentDashCam_);
    }
    updateDashboardLoadingState();
}

void AnalysisView::updateDashboardLoadingState() {
    if (!detailDashboard_) {
        return;
    }
    QString curPath;
    if (currentDashCam_ >= 0) {
        auto it = videoReaderPaths_.find(currentDashCam_);
        if (it != videoReaderPaths_.end()) curPath = it->second;
    }
    const bool haveSignal = signalByCam_.count(curPath) > 0;
    const bool scanPending = !curPath.isEmpty() && !haveSignal
        && (pendingScanPaths_.contains(curPath)
            || (signalScanner_ && signalScanner_->isRunning()
                && signalScanner_->currentBinPath() == curPath));
    detailDashboard_->setLoadingSignals(scanPending);

    const bool thumbsLoading = thumbWatcher_ && thumbWatcher_->isRunning()
        && thumbCamPending_ == currentDashCam_;
    detailDashboard_->setLoadingThumbnails(thumbsLoading);

    if (dashLoadingLabel_) {
        // Text-only status — the phase (and running percentage when known)
        // without a bar graphic.
        if (scanPending) {
            dashLoadingLabel_->setText(dashProgressPercent_ >= 0
                ? QStringLiteral("Analyzing event… %1%").arg(dashProgressPercent_)
                : QStringLiteral("Analyzing event…"));
        } else if (thumbsLoading) {
            dashLoadingLabel_->setText(QStringLiteral("Preparing preview…"));
        }
    }
    updateDashboardVisibility(scanPending || thumbsLoading);
}

void AnalysisView::updateDashboardVisibility(bool loading) {
    if (!detailDashboard_) {
        return;
    }
    const bool toggleOn = !dashboardToggleCheck_ || dashboardToggleCheck_->isChecked();
    detailDashboard_->setVisible(!loading && toggleOn);
    if (dashLoadingLabel_) {
        dashLoadingLabel_->setVisible(loading && toggleOn);
    }
    updateTracksEdgeTabVisibility();
}

void AnalysisView::updateTracksEdgeTabVisibility() {
    if (!tracksEdgeTab_) {
        return;
    }
    const bool show = tabWidget_ && tabWidget_->currentIndex() == 1
        && detailDashboard_ && detailDashboard_->isVisible();
    tracksEdgeTab_->setVisible(show);
    if (!show && tracksPanel_) {
        tracksPanel_->hide();
        tracksTabHovered_ = false;
    }
}

void AnalysisView::startNextSignalScan() {
    if (!signalScanner_ || signalScanner_->isRunning()) {
        return;
    }
    while (!pendingScanPaths_.isEmpty()) {
        const QString path = pendingScanPaths_.takeFirst();
        if (QFile::exists(path)) {
            signalScanner_->scanAsync(path);
            updateDashboardLoadingState();
            return;
        }
    }
    updateDashboardLoadingState();
}

void AnalysisView::maybeScanDetailWindow(int frameIndex) {
    if (!isStreamingMode_ || !detailDashboard_ || !signalScanner_) return;
    if (!detailDashboard_->isDetailRegionVisible()) return;               // hidden via TRACKS or >= 1x
    if (currentDashCam_ < 0) return;
    const auto pathIt = videoReaderPaths_.find(currentDashCam_);
    if (pathIt == videoReaderPaths_.end()) return;
    // frameIndex and this scan run in the dash camera's OWN frame domain.
    auto readerIt = videoReaders_.find(currentDashCam_);
    const int camTotal = (readerIt != videoReaders_.end() && readerIt->second)
                             ? readerIt->second->getTotalFrames() : 0;
    if (camTotal <= EventSignalScanner::kMaxScannedFrames) return;        // full series is stride-1

    // Snap the window to a 15-frame grid so slow playback doesn't fire one
    // scan per frame; a new scan only when the snapped window changes.
    constexpr int kSnapGrid = 15;
    const int radius = EventDashboard::kDetailRadius;
    const int maxStart = std::max(0, camTotal - 1 - 2 * radius);
    int start = ((frameIndex - radius) / kSnapGrid) * kSnapGrid;
    start = std::max(0, std::min(start, (maxStart / kSnapGrid) * kSnapGrid));
    const int end = std::min(camTotal - 1, start + 2 * radius);

    const QString key = EventSignalScanner::windowCacheKey(pathIt->second, start, end);
    if (key == detailWindowKey_) return; // in flight or already pushed
    detailWindowKey_ = key;
    detailDashboard_->setDetailLoading(true);
    signalScanner_->scanWindowAsync(pathIt->second, start, end);
}

void AnalysisView::onDetailWindowFinished(const QString& binPath, int startFrame, int endFrame,
                                          const EventSignalData& data) {
    if (!detailDashboard_) return;
    // Drop stale results (camera switched / event changed while scanning).
    const auto it = videoReaderPaths_.find(currentDashCam_);
    if (it == videoReaderPaths_.end() || it->second != binPath) return;
    detailDashboard_->setDetailSeries(startFrame, endFrame, data.sampleFrames,
                                      data.brightness, data.stddev, data.spotPct,
                                      data.defectBrightness, data.defectLocal,
                                      data.defectContrast);
}

void AnalysisView::onDetailWindowFailed(const QString& binPath, int startFrame, int endFrame,
                                        const QString& reason) {
    Q_UNUSED(binPath); Q_UNUSED(startFrame); Q_UNUSED(endFrame);
    if (detailDashboard_) detailDashboard_->setDetailLoading(false);
    std::cerr << "[AnalysisView] Detail window scan failed: " << reason.toStdString() << std::endl;
}

void AnalysisView::onSignalScanFinished(const QString& binPath, const EventSignalData& data) {
    CameraSignal sig;
    sig.samples = data.sampleFrames;
    sig.brightness = data.brightness;
    sig.stddev = data.stddev;
    sig.spotPct = data.spotPct;
    sig.defects = data.defectBrightness;
    sig.localDefects = data.defectLocal;
    sig.contrastDefects = data.defectContrast;
    sig.totalFrames = data.totalFrames;
    sig.fps = data.fps;
    signalByCam_[binPath] = sig;

    // Refresh only if this result belongs to the camera being displayed.
    if (currentDashCam_ >= 0) {
        auto pathIt = videoReaderPaths_.find(currentDashCam_);
        if (pathIt != videoReaderPaths_.end() && pathIt->second == binPath) {
            refreshDashboardForCamera(currentDashCam_);
        }
    }
    startNextSignalScan();
}

void AnalysisView::onSignalScanFailed(const QString& binPath, const QString& reason) {
    std::cout << "[AnalysisView] Signal scan failed for "
              << binPath.toStdString() << ": " << reason.toStdString() << std::endl;
    startNextSignalScan();
    updateDashboardLoadingState();
}

void AnalysisView::refreshDashboardForCamera(int camIdx) {
    if (!detailDashboard_) {
        return;
    }
    if (camIdx < 0 || !videoReaders_.count(camIdx)) {
        if (!videoReaders_.empty()) {
            camIdx = videoReaders_.begin()->first;
        } else {
            return;
        }
    }
    currentDashCam_ = camIdx;

    auto it = videoReaders_.find(camIdx);
    const int total = (it != videoReaders_.end() && it->second)
                          ? it->second->getTotalFrames() : 0;
    double fps = (it != videoReaders_.end() && it->second) ? it->second->getFps() : 0.0;
    QVector<int> samples;
    QVector<double> brightness;
    QVector<double> stddev;
    QVector<double> spotPct;
    QVector<int> defects;
    QVector<int> localDefects;
    QVector<int> contrastDefects;
    auto pathIt = videoReaderPaths_.find(camIdx);
    if (pathIt != videoReaderPaths_.end()) {
        auto sigIt = signalByCam_.find(pathIt->second);
        if (sigIt != signalByCam_.end()) {
            samples = sigIt->second.samples;
            brightness = sigIt->second.brightness;
            stddev = sigIt->second.stddev;
            spotPct = sigIt->second.spotPct;
            defects = sigIt->second.defects;
            localDefects = sigIt->second.localDefects;
            contrastDefects = sigIt->second.contrastDefects;
            if (sigIt->second.fps > 0.0) {
                fps = sigIt->second.fps;
            }
        }
    }
    const QString label = currentEventCameraLabel(camIdx);
    EventDashboard* dash = detailDashboard_;
    if (dash) {
        // The series/axis are in THIS camera's own frame domain; the trigger
        // line and playhead arrive in master (timeline) domain - convert so
        // mixed-fps events line up (identity when camIdx is the timeline cam).
        dash->setEventData(label, total,
                           displayedFrameIndexForCamera(camIdx, triggerFrameIndex_), fps,
                           samples, brightness, stddev, spotPct, defects,
                           localDefects, contrastDefects);
        dash->setCurrentFrame(displayedFrameIndexForCamera(camIdx, currentReviewFrameIndex()));
    }
    generateThumbnails(camIdx);
}

void AnalysisView::generateThumbnails(int camIdx) {
    if (!thumbWatcher_) {
        return;
    }
    if (thumbWatcher_->isRunning()) {
        thumbWatcher_->cancel();
        thumbWatcher_->waitForFinished();
    }
    auto pathIt = videoReaderPaths_.find(camIdx);
    if (pathIt == videoReaderPaths_.end()) {
        return;
    }
    thumbCamPending_ = camIdx;
    updateDashboardLoadingState();
    // Own reader instance: keeps disk I/O off the playback reader.
    thumbWatcher_->setFuture(QtConcurrent::run([path = pathIt->second]() {
        VideoStreamReader reader;
        QVector<QImage> out;
        if (!reader.open(path)) {
            return out;
        }
        const int total = reader.getTotalFrames();
        const int count = qMin(EventDashboard::kThumbCount, std::max(1, total));
        for (int i = 0; i < count; ++i) {
            const int frameIdx = (count > 1)
                                     ? static_cast<int>(std::round(i * (total - 1) / (count - 1.0)))
                                     : 0;
            out.push_back(matToThumbnailImage(reader.getFrame(frameIdx)));
        }
        return out;
    }));
}

void AnalysisView::refreshDashboardThumbnails() {
    if (!thumbWatcher_) {
        return;
    }
    // A cancelled run emits finished() with an EMPTY result store; calling
    // result() then returns garbage memory (observed as SIGBUS). Only read
    // results that are actually present, and ignore stale cameras.
    const auto fut = thumbWatcher_->future();
    if (fut.isCanceled() || !fut.isResultReadyAt(0)) {
        return;
    }
    if (thumbCamPending_ < 0 || thumbCamPending_ != currentDashCam_) {
        return; // thumbnails for a camera the user has already moved past
    }
    const QVector<QImage> imgs = thumbWatcher_->result();
    if (detailDashboard_) {
        detailDashboard_->setThumbnails(imgs);
    }
    updateDashboardLoadingState();
}

void AnalysisView::onDashboardSeekRequested(int frame) {
    // The dashboard plots the selected camera's own-frame series, so it seeks
    // in that domain - map back to the master/timeline frame before seeking.
    seekToFrameIndex(masterFrameForCameraFrame(currentDashCam_, frame), true);
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
    controlsLayout->setSpacing(6);
    controlsLayout->setContentsMargins(8, 10, 8, 8);
    
    ThemeColors tc = CameraConfig::getThemeColors();
    leftSidebar_->setStyleSheet(makeSidebarPanelStyle(tc));

    // 1. Server Toggle (FIRST - left side)
    // Master switch for the whole live vision system: starts/stops camera
    // acquisition (triggers, recording, and defect detection are all gated on
    // streaming frames, so this effectively enables/disables the system).
    serverButton_ = new QPushButton("Server Offline", controlsGroup);
    serverButton_->setCheckable(true);
    serverButton_->setToolTip("Vision system OFFLINE — camera acquisition stopped. Click to start.");
    serverButton_->setMinimumHeight(28);
    serverButton_->setStyleSheet(makeSidebarStateButtonStyle(tc, false));
    connect(serverButton_, &QPushButton::clicked, this, &AnalysisView::onServerButtonClicked);
    
    // 2. Admin Login (SECOND - right side)
    adminButton_ = new QPushButton("Login", controlsGroup);
    adminButton_->setToolTip("Admin Login");
    adminButton_->setMinimumHeight(28);
    adminButton_->setStyleSheet(makeSidebarPrimaryButtonStyle(tc));
    connect(adminButton_, &QPushButton::clicked, this, &AnalysisView::onAdminButtonClicked);
    
    // Server Button Context Menu (for settings)
    serverButton_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(serverButton_, &QPushButton::customContextMenuRequested, [this](const QPoint& pos) {
        QMenu menu;
        // Raw Mode toggle removed - Always Raw now.

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
    logLayout->setContentsMargins(10, 10, 10, 8);
    logLayout->setSpacing(6);
    
    paperBreakTable_ = createLogTable(logGroup, false);
    permanentPaperBreakTable_ = createLogTable(logGroup, false);

    recentRecordsLabel_ = new QLabel("Recent Records (0)", logGroup);
    recentRecordsLabel_->setObjectName("analysisSidebarSectionLabel");
    logLayout->addWidget(recentRecordsLabel_);

    logLayout->addWidget(paperBreakTable_);

    togglePermanentTableButton_ = new QPushButton("Show Permanent Storage", logGroup);
    togglePermanentTableButton_->setCheckable(true);
    togglePermanentTableButton_->setMinimumHeight(24);
    togglePermanentTableButton_->setIcon(QIcon(":/assets/icons/arrow_down.svg"));
    togglePermanentTableButton_->setIconSize(QSize(12, 12));
    togglePermanentTableButton_->setStyleSheet(makeSidebarUtilityButtonStyle(tc));
    connect(togglePermanentTableButton_, &QPushButton::toggled, this, [this](bool checked) {
        permanentSectionWidget_->setVisible(checked);
        togglePermanentTableButton_->setText(checked ? "Hide Permanent Storage" : "Show Permanent Storage");
        togglePermanentTableButton_->setIcon(QIcon(checked ? ":/assets/icons/arrow_up.svg" : ":/assets/icons/arrow_down.svg"));
        permanentPaperBreakTable_->setSizePolicy(QSizePolicy::Expanding, checked ? QSizePolicy::Expanding : QSizePolicy::Preferred);
        updateLogTableReasonWidths(); // The now-visible permanent table needs its width.
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
    deleteModeRow->setContentsMargins(0, 2, 0, 0);
    deleteModeRow->setSpacing(6);

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
    
    // Combined action row: "Mark Permanent | Delete Selected" as a single
    // segmented control with a 1px divider, so both actions share one compact
    // rounded row instead of two separate buttons.
    splitActionRow_ = new QWidget(logGroup);
    splitActionRow_->setObjectName("splitActionRow");
    splitActionRow_->setAttribute(Qt::WA_StyledBackground, true); // paint the container's bg/border
    splitActionRow_->setStyleSheet(makeSidebarSplitActionStyle(tc));
    auto splitLayout = new QHBoxLayout(splitActionRow_);
    splitLayout->setContentsMargins(0, 0, 0, 0);
    splitLayout->setSpacing(0);

    permanentButton_ = new QPushButton("Mark Permanent", splitActionRow_);
    permanentButton_->setObjectName("permanentActionButton");
    permanentButton_->setMinimumHeight(24);
    permanentButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    permanentButton_->setCursor(Qt::PointingHandCursor);
    permanentButton_->setEnabled(false);
    connect(permanentButton_, &QPushButton::clicked, this, &AnalysisView::onTogglePermanentClicked);
    splitLayout->addWidget(permanentButton_, 1);

    deleteActionDivider_ = new QFrame(splitActionRow_);
    deleteActionDivider_->setObjectName("splitActionDivider");
    deleteActionDivider_->setFrameShape(QFrame::VLine);
    deleteActionDivider_->setFrameShadow(QFrame::Plain);
    // Span the full row height so the 1px line is always visible between the
    // two segments (an HBox would otherwise size a Preferred-height QFrame to
    // its tiny sizeHint height).
    deleteActionDivider_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    splitLayout->addWidget(deleteActionDivider_);

    deleteButton_ = new QPushButton("Delete Selected", splitActionRow_);
    deleteButton_->setObjectName("deleteActionButton");
    deleteButton_->setMinimumHeight(24);
    deleteButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    deleteButton_->setCursor(Qt::PointingHandCursor);
    deleteButton_->setEnabled(false); // Enabled only when admin turns on delete mode
    connect(deleteButton_, &QPushButton::clicked, this, &AnalysisView::onDeleteClicked);
    splitLayout->addWidget(deleteButton_, 1);

    logLayout->addWidget(splitActionRow_);
    
    // Instant Clear row: bulk-delete all non-permanent records, keeping the
    // N most recent (0 = clear everything). Permanent records are untouched.
    auto instantClearRow = new QHBoxLayout();
    instantClearRow->setContentsMargins(0, 2, 0, 0);
    instantClearRow->setSpacing(6);

    instantClearButton_ = new QPushButton("Instant Clear", logGroup);
    instantClearButton_->setMinimumHeight(24);
    instantClearButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    instantClearButton_->setCursor(Qt::PointingHandCursor);
    instantClearButton_->setStyleSheet(makeSidebarUtilityButtonStyle(tc));
    instantClearButton_->setToolTip("Delete all non-permanent records, keeping the configured number of the most recent.");
    connect(instantClearButton_, &QPushButton::clicked, this, &AnalysisView::onInstantClearClicked);
    instantClearRow->addWidget(instantClearButton_, 1);

    instantClearKeepSpin_ = new QSpinBox(logGroup);
    instantClearKeepSpin_->setRange(0, 10000);
    instantClearKeepSpin_->setValue(std::max(0, CameraConfig::getInstantClearKeepCount()));
    instantClearKeepSpin_->setSuffix(" keep");
    instantClearKeepSpin_->setMinimumHeight(24);
    instantClearKeepSpin_->setFixedWidth(92);
    instantClearKeepSpin_->setToolTip("How many of the most recent non-permanent records to keep when Instant Clear is pressed.");
    connect(instantClearKeepSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        CameraConfig::setInstantClearKeepCount(v);
    });
    instantClearRow->addWidget(instantClearKeepSpin_);

    logLayout->addLayout(instantClearRow);
    
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
    // Default to the metadata overlay mode configured in UI Preferences
    // (Analysis View -> Default Metadata), so events open with the operator's
    // preferred overlay. Falls back to "realtime" when the saved value is unknown.
    int defaultMetadataIndex = metadataDisplayCombo_->findData(
        CameraConfig::getAnalysisViewStyle().defaultMetadataMode);
    if (defaultMetadataIndex < 0) {
        defaultMetadataIndex = metadataDisplayCombo_->findData("realtime");
    }
    metadataDisplayCombo_->setCurrentIndex(defaultMetadataIndex);
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

    resetToolsButton_ = new QPushButton("Reset", detailToolsWidget_);
    resetToolsButton_->setToolTip("Reset marker, zoom, and brightness for the detail image.");

    cameraOffsetSpin_ = new QSpinBox(detailToolsWidget_);
    cameraOffsetSpin_->setRange(-5000, 5000);
    cameraOffsetSpin_->setSuffix(" f");
    cameraOffsetSpin_->setValue(0);
    cameraOffsetSpin_->setToolTip("Per-camera frame offset on top of the shared timeline (negative = earlier frames).");

    markDefectButton_ = new QPushButton("Mark Defect", detailToolsWidget_);
    markDefectButton_->setToolTip("Record this camera's currently displayed frame as a defect mark. Each click adds one mark; mark the same defect on at least two cameras, then Align to sync them.");

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
    toolsLayout->addWidget(resetToolsButton_);
    toolsLayout->addSpacing(6);
    toolsLayout->addWidget(cameraOffsetSpin_);
    toolsLayout->addWidget(markDefectButton_);
    toolsLayout->addStretch(1);
    connect(cameraOffsetSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &AnalysisView::onCameraOffsetChanged);
    connect(markDefectButton_, &QPushButton::clicked,
            this, &AnalysisView::markDefectForSelectedCamera);
    metadataLayout->addWidget(detailToolsWidget_);
    metadataLayout->addSpacing(4);
    metadataLayout->addWidget(headerToolsSeparator_);
    metadataLayout->addSpacing(4);
    metadataLayout->addWidget(metadataLabel);
    metadataLayout->addWidget(metadataDisplayCombo_);

    // (Emulation-mode badge now lives on the MainWindow status bar bottom line,
    // shared by Live View, Detail View, and Analysis View.)
    detailToolsWidget_->setVisible(false);
    
    selectedCameraWidget_ = new AnalysisVideoWidget(-1, "Select a camera", singleCameraTab_);
    singleLayout->addWidget(selectedCameraWidget_, 1);

    // Prototype: event dashboard below the video in the Camera tab.
    detailDashboard_ = new EventDashboard(singleCameraTab_);
    connect(detailDashboard_, &EventDashboard::seekRequested,
            this, &AnalysisView::onDashboardSeekRequested);
    singleLayout->addWidget(detailDashboard_);

    // Load gate: occupies the dashboard's slot while the event's signals +
    // thumbnails are still being scanned after a trigger. Text only — no
    // progress bar, just the running status ("Analyzing event… 42%").
    dashLoadingLabel_ = new QLabel(QStringLiteral("Analyzing event…"), singleCameraTab_);
    dashLoadingLabel_->setAlignment(Qt::AlignCenter);
    QFont dashLoadFont = dashLoadingLabel_->font();
    dashLoadFont.setPixelSize(13);
    dashLoadingLabel_->setFont(dashLoadFont);
    dashLoadingLabel_->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
        .arg(QColor(CameraConfig::getThemeColors().text).name()));
    dashLoadingLabel_->hide();
    singleLayout->addWidget(dashLoadingLabel_);

    connect(selectedCameraWidget_, &AnalysisVideoWidget::annotationChangedNormalized, this,
            [this](int cameraId, const QString& shape, const QVector<QPointF>& points) {
        const int frameIndex = displayedFrameIndexForCamera(cameraId, currentReviewFrameIndex());
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
        // Snap the playhead to the marked master frame. frameIndex is the
        // camera's OWN frame index — feeding it to the timeline-domain seek
        // made non-timeline cameras drift the scrubber on every mark.
        seekToRelativeFrame(currentReviewFrameIndex() - triggerFrameIndex_);
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
    connect(resetToolsButton_, &QPushButton::clicked, this, [this]() {
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

    // ── Right "Layer" tools panel ──────────────────────────────────────────
    // All review tools live here, stacked vertically in named groups. The
    // panel floats over the video frame area, docked to its right edge, with
    // a vertical TOOLS tab as the drawer handle: unpinned, hovering the tab
    // (or the panel) shows the panel and leaving hides it; Lock pins it open.
    // Widgets are reparented from the detail-tools row and the playback align
    // row; connections are unaffected by reparenting. The panel is not a
    // splitter child, so hide/show never resizes the camera frames.
    // The panel is a direct child of the frame area, floating over the camera
    // frames. No drop shadow is used: nesting a shadow under the fade's opacity
    // effect renders incorrectly on X11 (and a widget can own only one graphics
    // effect) — depth comes from the border instead. The fade effect itself is
    // attached only while animating (see ensure/clearToolsPanelOpacityEffect),
    // so no effect is ever active while the panel is idle or hidden.
    rightToolsPanel_ = new QWidget(mainArea_);
    rightToolsPanel_->setObjectName("rightToolsPanel");
    rightToolsPanel_->setAttribute(Qt::WA_StyledBackground, true);
    rightToolsPanel_->setFixedWidth(240);
    auto panelLayout = new QVBoxLayout(rightToolsPanel_);
    panelLayout->setContentsMargins(10, 10, 10, 10);
    panelLayout->setSpacing(8);

    // Header: title (icon + name) + lock/unlock.
    auto panelHeader = new QHBoxLayout();
    auto panelTitle = new QLabel(
        QString("<img src=':/assets/icons/tools_panel.svg' width='16' height='16' style='vertical-align: middle;'> Tools"),
        rightToolsPanel_);
    panelTitle->setStyleSheet(QString("color: %1; font-size: 13px; font-weight: 700;").arg(tc.text));
    toolsLockButton_ = new QPushButton("Lock", rightToolsPanel_);  // starts unpinned (hover-driven)
    toolsLockButton_->setStyleSheet(makeSidebarUtilityButtonStyle(tc));
    toolsLockButton_->setCursor(Qt::PointingHandCursor);
    toolsLockButton_->setToolTip("Unpinned: hover the TOOLS tab on the frame's right edge to show the panel.");
    connect(toolsLockButton_, &QPushButton::clicked, this, &AnalysisView::onToolsLockToggled);
    panelHeader->addWidget(panelTitle);
    panelHeader->addStretch(1);
    panelHeader->addWidget(toolsLockButton_);
    panelLayout->addLayout(panelHeader);
    // Push the tool rows down, clear of the header line.
    panelLayout->addSpacing(6);

    // Name caption helper (indented, not aligned with the header).
    auto captionLabel = [&](const QString& text) {
        auto* label = new QLabel(text, rightToolsPanel_);
        label->setStyleSheet(QString("color: %1; font-size: 11px;").arg(tc.text));
        return label;
    };
    auto indentRow = [](QHBoxLayout* row) { row->setContentsMargins(6, 0, 0, 0); };
    // Muted uppercase section header.
    auto sectionLabel = [&](const QString& text) {
        auto* label = new QLabel(text, rightToolsPanel_);
        label->setStyleSheet(QString(
            "color: %1; font-size: 10px; font-weight: 700; letter-spacing: 1px; padding: 0 2px;")
            .arg(QColor(tc.text).lighter(135).name()));
        return label;
    };
    // Thin divider between tool groups.
    auto panelDivider = [&]() -> QFrame* {
        auto* line = new QFrame(rightToolsPanel_);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Plain);
        line->setStyleSheet(QString("background-color: %1; border: none; max-height: 1px; min-height: 1px;")
                            .arg(QColor(tc.border).lighter(118).name()));
        return line;
    };

    // ── Marker ──
    panelLayout->addWidget(sectionLabel("MARKER"));
    auto markerRow = new QHBoxLayout();
    markerRow->addWidget(markerToolCheck_);
    markerRow->addWidget(markerShapeCombo_);
    markerRow->addStretch(1);
    indentRow(markerRow);
    panelLayout->addLayout(markerRow);

    // ── View ──
    panelLayout->addWidget(panelDivider());
    panelLayout->addWidget(sectionLabel("VIEW"));
    auto zoomRow = new QHBoxLayout();
    zoomRow->addWidget(captionLabel("Zoom"));
    zoomSlider_->setFixedWidth(110);
    zoomRow->addWidget(zoomSlider_);
    zoomRow->addWidget(zoomValueLabel_);
    zoomRow->addStretch(1);
    indentRow(zoomRow);
    panelLayout->addLayout(zoomRow);

    auto brightnessRow = new QHBoxLayout();
    brightnessRow->addWidget(captionLabel("Bright"));
    brightnessSlider_->setFixedWidth(110);
    brightnessRow->addWidget(brightnessSlider_);
    brightnessRow->addWidget(brightnessValueLabel_);
    brightnessRow->addStretch(1);
    indentRow(brightnessRow);
    panelLayout->addLayout(brightnessRow);

    resetToolsButton_->setCursor(Qt::PointingHandCursor);
    panelLayout->addWidget(resetToolsButton_);

    // ── Defect align ──
    panelLayout->addWidget(panelDivider());
    panelLayout->addWidget(sectionLabel("DEFECT ALIGN"));
    auto offsetRow = new QHBoxLayout();
    offsetRow->addWidget(captionLabel("Camera offset"));
    offsetRow->addWidget(cameraOffsetSpin_);
    offsetRow->addStretch(1);
    indentRow(offsetRow);
    panelLayout->addLayout(offsetRow);

    markDefectButton_->setCursor(Qt::PointingHandCursor);
    panelLayout->addWidget(markDefectButton_);

    alignButton_->setCursor(Qt::PointingHandCursor);
    resetOffsetsButton_->setCursor(Qt::PointingHandCursor);
    auto alignRow2 = new QHBoxLayout();
    alignRow2->addWidget(alignButton_);
    alignRow2->addWidget(resetOffsetsButton_);
    alignRow2->addStretch(1);
    indentRow(alignRow2);
    panelLayout->addLayout(alignRow2);
    if (alignStatusLabel_) {
        alignStatusLabel_->setWordWrap(true);
        panelLayout->addWidget(alignStatusLabel_);
    }

    // ── Event dashboard ──
    panelLayout->addWidget(panelDivider());
    panelLayout->addWidget(sectionLabel("EVENT DASHBOARD"));
    dashboardToggleCheck_ = new QCheckBox("Chart + Thumbnails", rightToolsPanel_);
    dashboardToggleCheck_->setChecked(true);
    dashboardToggleCheck_->setToolTip(
        "Show or hide the per-camera time-series dashboard (Camera tab).");
    connect(dashboardToggleCheck_, &QCheckBox::toggled, this, [this](bool) {
        // Single choke point: respects both the toggle and the load gate.
        updateDashboardLoadingState();
    });
    dashboardToggleCheck_->setStyleSheet(
        QString("QCheckBox { color: %1; font-size: 11px; }").arg(tc.text));
    panelLayout->addWidget(dashboardToggleCheck_);

    panelLayout->addStretch(1);

    // Reparent all tool widgets into the panel (removes them from old layouts).
    const QList<QWidget*> panelTools = {
        markerToolCheck_, markerShapeCombo_, zoomSlider_, zoomValueLabel_,
        brightnessSlider_, brightnessValueLabel_, resetToolsButton_,
        cameraOffsetSpin_, markDefectButton_, alignButton_,
        resetOffsetsButton_, alignStatusLabel_
    };
    for (QWidget* w : panelTools) {
        if (w) {
            w->setParent(rightToolsPanel_);
        }
    }

    // Drop the now-empty detail-tools corner widget.
    if (detailToolsWidget_) {
        metadataLayout->removeWidget(detailToolsWidget_);
        detailToolsWidget_->deleteLater();
        detailToolsWidget_ = nullptr;
    }

    // Drop the now-empty align row from the playback panel.
    if (QLayout* playLayout = playbackPanel_->layout()) {
        for (int i = playLayout->count() - 1; i >= 0; --i) {
            QLayoutItem* item = playLayout->itemAt(i);
            if (item->layout() && item->layout()->count() == 0) {
                playLayout->removeItem(item);
                delete item;
            }
        }
    }

    // Hover-driven auto-show/hide when unpinned (Lock toggles pinning).
    toolsHoverTimer_ = new QTimer(this);
    toolsHoverTimer_->setInterval(120);
    connect(toolsHoverTimer_, &QTimer::timeout, this, &AnalysisView::onToolsHoverTick);
    toolsHoverTimer_->start();

    // Vertical TOOLS tab on the frame's right edge: the hover target that
    // reveals the panel when unpinned. Always visible; the label text is
    // rotated 90° so it reads top-to-bottom down the edge. restyleToolsEdgeTab
    // renders the vertical pixmap + theme background.
    // Compact vertical tab: slim handle, shorter run than before.
    toolsEdgeTab_ = new QLabel(mainArea_);
    toolsEdgeTab_->setObjectName("toolsEdgeTab");
    toolsEdgeTab_->setFixedSize(22, 84);
    toolsEdgeTab_->setAlignment(Qt::AlignCenter);
    toolsEdgeTab_->setToolTip("Hover to show the Tools panel.");
    restyleToolsEdgeTab();
    // Initial tab is All Camera (index 0) — the TOOLS tab only belongs to the
    // Single Camera detail view. onTabChanged() re-evaluates on every switch.
    toolsEdgeTab_->hide();

    // Style every panel control with the current theme (also re-runs on theme
    // changes via updateTheme -> applyToolsPanelTheme).
    applyToolsPanelTheme();

    // Show/hide transition: fade the panel's opacity while sliding it sideways,
    // so it reads as a drawer retracting into the frame's right edge. The fade
    // effect is created on demand (ensureToolsPanelOpacityEffect) so nothing is
    // composited while the panel is idle or hidden.
    toolsPanelFadeAnim_ = new QPropertyAnimation(this);
    toolsPanelFadeAnim_->setPropertyName("opacity");
    toolsPanelFadeAnim_->setDuration(150);
    connect(toolsPanelFadeAnim_, &QPropertyAnimation::finished,
            this, &AnalysisView::onToolsPanelHideFinished);
    toolsPanelSlideAnim_ = new QPropertyAnimation(rightToolsPanel_, "geometry", this);
    toolsPanelSlideAnim_->setDuration(150);

    // Start hidden (unpinned): the hover timer reveals it when the cursor
    // reaches the tab or the panel.
    rightToolsPanel_->hide();

    // Float over the video frame area, clear of the playback panel.
    positionToolsPanel();

    // ── TRACKS hover tab + panel (dashboard per-region show/hide) ──
    // Separate from TOOLS for clarity: one vertical edge tab per panel.
    tracksEdgeTab_ = new QLabel(mainArea_);
    tracksEdgeTab_->setObjectName("tracksEdgeTab");
    tracksEdgeTab_->setFixedSize(22, 84);
    tracksEdgeTab_->setAlignment(Qt::AlignCenter);
    tracksEdgeTab_->setToolTip("Hover to choose which signal tracks to show.");
    restyleTracksEdgeTab();
    tracksEdgeTab_->hide();

    tracksPanel_ = new QWidget(mainArea_);
    tracksPanel_->setObjectName("tracksPanel");
    tracksPanel_->setFixedWidth(150);
    auto tracksLayout = new QVBoxLayout(tracksPanel_);
    tracksLayout->setContentsMargins(10, 8, 10, 8);
    tracksLayout->setSpacing(4);
    auto tracksTitle = new QLabel(QStringLiteral("TRACKS"), tracksPanel_);
    tracksLayout->addWidget(tracksTitle);
    const char* trackNames[5] = {
        "Brightness", "Detail", "Spots %", "Contrast", "Thumbnails"
    };
    for (int i = 0; i < 5; ++i) {
        trackChecks_[i] = new QCheckBox(QString::fromUtf8(trackNames[i]), tracksPanel_);
        trackChecks_[i]->setChecked(i == 0); // only BRIGHTNESS by default
        tracksLayout->addWidget(trackChecks_[i]);
        connect(trackChecks_[i], &QCheckBox::toggled, this, [this]() {
            if (!detailDashboard_) return;
            detailDashboard_->setBrightnessVisible(trackChecks_[0]->isChecked());
            detailDashboard_->setRegionsVisible(
                trackChecks_[1]->isChecked(), trackChecks_[2]->isChecked(),
                trackChecks_[3]->isChecked(), trackChecks_[4]->isChecked());
            if (trackChecks_[1]->isChecked()) {
                maybeScanDetailWindow(currentReviewFrameIndex());
            }
        });
    }
    tracksPanel_->setStyleSheet(QString(
        "QWidget#tracksPanel { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
        "QLabel { color: %3; font-size: 11px; font-weight: bold; }"
        "QCheckBox { color: %3; font-size: 11px; }")
        .arg(tc.bg, tc.border, tc.text));
    tracksPanel_->adjustSize(); // child widgets don't auto-size on show() — do it now
    tracksPanel_->hide();
    tracksEdgeTab_->installEventFilter(this);
    tracksPanel_->installEventFilter(this);

    // Initial position + visibility (Single Camera tab only).
    positionToolsPanel();
    updateTracksEdgeTabVisibility();
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
    // No fixed/min height — the panel hugs its content (buttons + flag-strip
    // margin) so no dead space sits above/below the controls; the freed space
    // goes to the video + dashboard above (tabWidget_ has stretch 1).
    playbackPanel_->setAutoFillBackground(true); // Force paint
    // Use background-color and ensure contrast. 
    ThemeColors tc = CameraConfig::getThemeColors();
    playbackPanel_->setStyleSheet(QString(
        "QWidget#playbackPanel { background-color: %1; border-top: 1px solid %2; }")
        .arg(tc.bg, tc.border));
    
    auto layout = new QVBoxLayout(playbackPanel_);
    layout->setContentsMargins(6, 4, 6, 3);
    layout->setSpacing(0);
    
    // Playback slider (System Standard)
    // === PLAYBACK CONTROL TOOLBAR (Single Line + SVGs) ===
    auto toolbarLayout = new QHBoxLayout();
    toolbarLayout->setSpacing(4);
    // Top margin reserves the flag strip where the zero/trigger marker sits,
    // above the scrubbing line (updateSliderZeroMarker positions it there).
    toolbarLayout->setContentsMargins(0, 14, 0, 0);

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
        btn->setFixedSize(28, 28);
        btn->setToolTip(tooltip);
        btn->setStyleSheet(makePlaybackIconButtonStyle(tc));
        return btn;
    };

    // 1. Speed
    speedButton_ = new QPushButton("1.0x", playbackPanel_);
    speedButton_->setFixedSize(72, 28);
    speedButton_->setStyleSheet(makePlaybackSpeedButtonStyle(tc));
    speedMenu_ = new QMenu(speedButton_);
    // Trimmed to the slow end — those are the speeds used to inspect defects
    // frame-by-frame at low event frame rates. Fast (2.0x) is kept as the top
    // speed for quick scanning.
    speedMenu_->addAction("Ultra Slow (0.05x)")->setData(0.05);
    speedMenu_->addAction("Very Slow (0.10x)")->setData(0.10);
    speedMenu_->addAction("Slow (0.25x)")->setData(0.25);
    speedMenu_->addAction("Normal (1.0x)")->setData(1.0);
    speedMenu_->addAction("Fast (2.0x)")->setData(2.0);
    speedButton_->setMenu(speedMenu_);
    connect(speedMenu_, &QMenu::triggered, this, &AnalysisView::onSpeedChanged);
    // After the speed menu closes (selection or Esc), keyboard focus lands back
    // on the menu button, which would swallow the media-player keys (Space
    // re-opens the menu, arrows do nothing visible). Hand focus back to the
    // view once the popup is gone — unless the user clicked another widget,
    // whose focus Qt has already set.
    connect(speedMenu_, &QMenu::aboutToHide, this, [this]() {
        QTimer::singleShot(0, this, [this]() {
            if (QApplication::focusWidget() == speedButton_) {
                setFocus();
            }
        });
    });
    // Compact dropdown: small font, tight rows, readable accent highlight.
    speedMenu_->setStyleSheet(QString(
        "QMenu { background-color: %1; border: 1px solid %2; color: %3; padding: 2px; font-size: 11px; }"
        "QMenu::item { padding: 2px 24px 2px 8px; font-size: 11px; }"
        "QMenu::item:selected { background-color: %4; color: %5; }"
        "QMenu::item:disabled { color: #888888; }"
    ).arg(tc.bg, tc.border, tc.text, tc.btnHover, tc.primary));
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
    prevButton_->setAutoRepeatDelay(300);
    prevButton_->setAutoRepeatInterval(33);  // matches Normal (1.0x)
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

    // 7. Step Forward
    nextButton_ = createSvgButton("Step Forward.svg", "Step Forward");
    nextButton_->setAutoRepeat(true);
    nextButton_->setAutoRepeatDelay(300);
    nextButton_->setAutoRepeatInterval(33);  // matches Normal (1.0x)
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
    // Route media-player keys (Left/Right step, Space play/pause) through the
    // view even when the slider holds focus after scrubbing.
    playbackSlider_->installEventFilter(this);

    // Zero/trigger marker: a small flag sitting in the flag strip ABOVE the
    // slider — never on the scrubbing line itself, so it can't be mistaken for
    // the playhead or block scrubbing (positioned by updateSliderZeroMarker).
    sliderZeroMarker_ = new QLabel(playbackPanel_);
    QPixmap pm(":/assets/icons/Zero Marker.svg");
    sliderZeroMarker_->setPixmap(pm.scaled(12, 12, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    sliderZeroMarker_->setFixedSize(12, 12);
    sliderZeroMarker_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    sliderZeroMarker_->raise();
    sliderZeroMarker_->show();
    
    layout->addLayout(toolbarLayout);

    // Second row: per-camera alignment (Align / Reset / speed / status).
    auto alignRow = new QHBoxLayout();
    alignRow->setSpacing(4);
    alignRow->setContentsMargins(0, 0, 0, 0);
    alignButton_ = new QPushButton("Align", playbackPanel_);
    alignButton_->setToolTip("Shift each camera's timeline so all defects show at the same time, using the machine speed captured with the event (OPC UA Machine Speed tag) and camera positions (mm).");
    alignButton_->setStyleSheet(makePlaybackSpeedButtonStyle(tc));
    connect(alignButton_, &QPushButton::clicked, this, &AnalysisView::applyCameraAlignment);
    alignRow->addWidget(alignButton_);

    resetOffsetsButton_ = new QPushButton("Reset", playbackPanel_);
    resetOffsetsButton_->setToolTip("Clear all per-camera offsets.");
    resetOffsetsButton_->setStyleSheet(makePlaybackSpeedButtonStyle(tc));
    connect(resetOffsetsButton_, &QPushButton::clicked, this, &AnalysisView::clearCameraOffsets);
    alignRow->addWidget(resetOffsetsButton_);

    // Same palette+font approach as the value label so this label's tooltip
    // keeps the standard look (it has a tooltip with alignment details).
    alignStatusLabel_ = new QLabel("—", playbackPanel_);
    QPalette alignPal = alignStatusLabel_->palette();
    alignPal.setColor(QPalette::WindowText, QColor(tc.text));
    alignStatusLabel_->setPalette(alignPal);
    QFont alignFont = alignStatusLabel_->font();
    alignFont.setPixelSize(11);
    alignStatusLabel_->setFont(alignFont);
    alignRow->addWidget(alignStatusLabel_, 1);

    layout->addLayout(alignRow);
}

// Slot implementations
void AnalysisView::onServerButtonClicked() {
    const bool running = serverButton_->isChecked();

    // Taking the vision system offline is a deliberate action: ask first, so a
    // stray click never silently stops acquisition (or aborts a startup). The
    // button stays clickable during Connecting… precisely to allow cancelling.
    if (!running && serverRunning_) {
        const bool wasConnecting = serverConnecting_;
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, wasConnecting ? "Cancel Camera Startup" : "Stop Vision System",
            wasConnecting
                ? "Camera acquisition is still starting up.\n\n"
                  "Cancel the startup and take the vision system offline?"
                : "Camera acquisition is currently running.\n\n"
                  "Stop acquisition and take the vision system offline?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            // Restore the checked state; the Connecting… animation keeps running.
            serverButton_->setChecked(true);
            return;
        }
    }

    setServerRunning(running);
    emit serverToggled(running);
}

void AnalysisView::setServerRunning(bool running) {
    serverRunning_ = running;
    serverConnecting_ = false; // Terminal state reached; clear any in-progress flag.
    if (serverConnectingTimer_) {
        serverConnectingTimer_->stop();
    }
    if (!serverButton_) {
        return;
    }
    serverButton_->setChecked(running);
    serverButton_->setText(running ? "Server Online" : "Server Offline");
    serverButton_->setStyleSheet(makeSidebarStateButtonStyle(CameraConfig::getThemeColors(), running));
    serverButton_->setToolTip(running
        ? "Vision system ONLINE — cameras are streaming. Click to stop acquisition."
        : "Vision system OFFLINE — camera acquisition stopped. Click to start.");
}

void AnalysisView::setServerConnecting(bool connecting) {
    serverConnecting_ = connecting;
    if (!serverButton_) {
        return;
    }
    if (connecting) {
        // Keep the button checkable so the operator can click again to cancel.
        serverConnectingDots_ = 0;
        serverButton_->setText("Connecting");
        serverButton_->setStyleSheet(makeSidebarConnectingButtonStyle(CameraConfig::getThemeColors()));
        serverButton_->setToolTip("Vision system starting — camera acquisition in progress. Click to cancel.");
        if (serverConnectingTimer_) {
            serverConnectingTimer_->start();
        }
    } else {
        if (serverConnectingTimer_) {
            serverConnectingTimer_->stop();
        }
        setServerRunning(serverRunning_);
    }
}

void AnalysisView::onServerConnectingTick() {
    if (!serverButton_ || !serverConnecting_) {
        return;
    }
    serverConnectingDots_ = (serverConnectingDots_ + 1) % 4; // 0..3 dots
    serverButton_->setText(QStringLiteral("Connecting%1").arg(QString(serverConnectingDots_, QLatin1Char('.'))));
}

int AnalysisView::locateNewEventRow(QTableWidget* table) const {
    if (!table) {
        return -1;
    }
    for (int r = 0; r < table->rowCount(); ++r) {
        QTableWidgetItem* item = table->item(r, 0);
        if (item && item->data(Qt::UserRole + 3).toBool()) {
            return r;
        }
    }
    return -1;
}

void AnalysisView::startNewEventPulse(QTableWidget* table) {
    stopNewEventPulse();
    if (!table) {
        return;
    }
    const int row = locateNewEventRow(table);
    if (row < 0) {
        return;
    }
    newEventPulseTable_ = table;
    newEventPulseRow_ = row;
    newEventPulseSteps_ = 0;
    // Kick off with the brightest frame immediately, then let the timer decay it.
    applyNewEventPulseAlpha(table, row, newEventPulseAlphaForStep(0));
    if (newEventPulseTimer_) {
        newEventPulseTimer_->start();
    }
}

void AnalysisView::stopNewEventPulse() {
    if (newEventPulseTimer_) {
        newEventPulseTimer_->stop();
    }
    newEventPulseTable_ = nullptr;
    newEventPulseRow_ = -1;
    newEventPulseSteps_ = 0;
}

int AnalysisView::newEventPulseAlphaForStep(int step) {
    // Damped oscillation: bright flashes that decay onto the static tint.
    // alpha(t) = base + amplitude * exp(-t/tau) * |cos(2*pi*f*t)|
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kDecayTauSec = 0.28;
    constexpr double kPulseFreqHz = 2.0;
    const double t = step * kNewEventPulseTickMs / 1000.0;
    const double decay = std::exp(-t / kDecayTauSec);
    const double pulse = std::abs(std::cos(kPi * 2.0 * kPulseFreqHz * t));
    const int alpha = static_cast<int>(kNewEventPulseBaseAlpha + kNewEventPulseAmplitude * decay * pulse);
    return qBound(kNewEventPulseBaseAlpha, alpha, 255);
}

void AnalysisView::onNewEventPulseTick() {
    ++newEventPulseSteps_;
    QTableWidget* table = newEventPulseTable_;
    if (!table) {
        stopNewEventPulse();
        return;
    }
    // Re-locate the flagged row every tick: it may have been moved by a later
    // sort, rebuilt by a newer event, moved between tables, or acknowledged by
    // a click. If it is gone, stop cleanly.
    const int row = locateNewEventRow(table);
    if (row < 0) {
        stopNewEventPulse();
        return;
    }
    newEventPulseRow_ = row; // Keep the state in sync with the live position.
    if (newEventPulseSteps_ * kNewEventPulseTickMs >= kNewEventPulseDurationMs) {
        // Settle on the static tint and stop.
        applyNewEventPulseAlpha(table, row, kNewEventPulseBaseAlpha);
        stopNewEventPulse();
        return;
    }
    applyNewEventPulseAlpha(table, row, newEventPulseAlphaForStep(newEventPulseSteps_));
}

void AnalysisView::applyNewEventPulseAlpha(QTableWidget* table, int row, int alpha) {
    if (!table || row < 0 || row >= table->rowCount()) {
        return;
    }
    QColor tint(CameraConfig::getThemeColors().primary);
    tint.setAlpha(alpha);
    for (int col = 0; col < table->columnCount(); ++col) {
        if (QTableWidgetItem* item = table->item(row, col)) {
            item->setBackground(tint);
        }
    }
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

    int pendingSkipped = 0;
    for (int row : uniqueRows) {
        QTableWidgetItem* item = activeTable->item(row, 0); 
        if (item) {
            // Pending placeholder ("Recording…"): the .bin is still being
            // written and nothing is registered in the database yet, so
            // deleting it would race the in-progress save (and it would
            // reappear once the real event lands anyway). Skip it.
            if (item->data(Qt::UserRole + 4).toBool()) {
                ++pendingSkipped;
                continue;
            }
            QString ts = item->data(Qt::UserRole).toString();
            if (!ts.isEmpty()) {
                rowsToDelete.append(row);
                timestampsToDelete.append(ts);
                std::cout << "[AnalysisView] Row " << row << " SELECTED. TS: " << ts.toStdString() << std::endl;
            }
        }
    }
    
    if (timestampsToDelete.isEmpty()) {
        if (pendingSkipped > 0) {
            QMessageBox::information(this, "Delete",
                "The selected event is still recording (\"Recording…\") and cannot be "
                "deleted yet.\nWait for the recording to finish saving, then delete it.");
        }
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

void AnalysisView::onInstantClearClicked() {
    const int keep = instantClearKeepSpin_ ? instantClearKeepSpin_->value() : 0;

    // Count non-permanent records so the confirmation is accurate.
    int nonPermanentCount = 0;
    for (int row = 0; row < paperBreakTable_->rowCount(); ++row) {
        QTableWidgetItem* item = paperBreakTable_->item(row, 0);
        if (item && !item->data(Qt::UserRole + 2).toBool()) {
            ++nonPermanentCount;
        }
    }
    const int toDelete = std::max(0, nonPermanentCount - keep);
    if (toDelete <= 0) {
        QMessageBox::information(this, "Instant Clear",
                                 QString("No non-permanent records to clear (keeping the %1 most recent).").arg(keep));
        return;
    }

    const QString msg = keep > 0
        ? QString("Delete %1 non-permanent record(s), keeping the %2 most recent?\n\nPermanent records are not affected.")
              .arg(toDelete).arg(keep)
        : QString("Delete ALL %1 non-permanent record(s)?\n\nPermanent records are not affected.").arg(toDelete);
    if (QMessageBox::question(this, "Confirm Instant Clear", msg,
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    // Stop review/playback and close file handles before deleting files on disk.
    clearData();

    const int deleted = EventDatabase::instance().clearNonPermanentEvents(keep);
    std::cout << "[AnalysisView] Instant clear: deleted " << deleted
              << " non-permanent event(s), keeping " << keep << std::endl;

    reloadEventTables();
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

    // Placeholder row: recording still in progress, no .bin on disk yet.
    if (item->data(Qt::UserRole + 4).toBool()) return;

    if (!suppressNewEventIndicatorClear_ && item->data(Qt::UserRole + 3).toBool()) {
        // Acknowledge the new-event highlight: clear the row tint and the bold.
        item->setBackground(QBrush());
        item->setData(Qt::UserRole + 3, false);
        QFont timeFont = item->font();
        timeFont.setBold(false);
        item->setFont(timeFont);
        if (QTableWidgetItem* reasonItem = sourceTable->item(row, 1)) {
            reasonItem->setBackground(QBrush());
            reasonItem->setData(Qt::UserRole + 3, false);
            QFont reasonFont = reasonItem->font();
            reasonFont.setBold(false);
            reasonItem->setFont(reasonFont);
        }
        if (QTableWidgetItem* groupItem = sourceTable->item(row, 2)) {
            groupItem->setBackground(QBrush());
            groupItem->setData(Qt::UserRole + 3, false);
        }
        if (QTableWidgetItem* frameItem = sourceTable->item(row, 3)) {
            frameItem->setBackground(QBrush());
            frameItem->setData(Qt::UserRole + 3, false);
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
    // Any manual camera selection supersedes a pending typed camera number.
    cancelCameraKeyEntry();
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
    selectedCameraWidget_ = new AnalysisVideoWidget(cameraId, QString(), singleCameraTab_);
    if (markerShapeCombo_) {
        selectedCameraWidget_->setMarkerShape(markerShapeCombo_->currentData().toString());
    }
    selectedCameraWidget_->setMarkerToolEnabled(markerToolCheck_ && markerToolCheck_->isChecked());
    selectedCameraWidget_->setZoomFactor(zoomSlider_ ? zoomSlider_->value() / 100.0 : 1.0);
    selectedCameraWidget_->setBrightnessOffset(brightnessSlider_ ? brightnessSlider_->value() : 0);
    connect(selectedCameraWidget_, &AnalysisVideoWidget::annotationChangedNormalized, this,
            [this](int cameraId, const QString& shape, const QVector<QPointF>& points) {
        const int frameIndex = displayedFrameIndexForCamera(cameraId, currentReviewFrameIndex());
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
        // Snap the playhead to the marked master frame. frameIndex is the
        // camera's OWN frame index — feeding it to the timeline-domain seek
        // made non-timeline cameras drift the scrubber on every mark.
        seekToRelativeFrame(currentReviewFrameIndex() - triggerFrameIndex_);
        applyAnnotationToSelectedFrame();
        updateAnnotationSliderMarkers();
    });
    connect(selectedCameraWidget_, &AnalysisVideoWidget::doubleClicked,
            this, &AnalysisView::onSelectedCameraDoubleClicked);
    if (layout) {
        // Keep the video above the dashboard (prototype B) when present.
        const int dashIdx = detailDashboard_ ? layout->indexOf(detailDashboard_) : -1;
        if (dashIdx >= 0) {
            layout->insertWidget(dashIdx, selectedCameraWidget_, 1);
        } else {
            layout->addWidget(selectedCameraWidget_, 1);
        }
    }
    refreshDashboardForCamera(cameraId);
    if (cameraOffsetSpin_) {
        cameraOffsetSpin_->blockSignals(true);
        if (cameraId >= 0 && cameraId < static_cast<int>(cameraFrameOffsets_.size())) {
            cameraOffsetSpin_->setValue(cameraFrameOffsets_[cameraId]);
        } else {
            cameraOffsetSpin_->setValue(0);
        }
        cameraOffsetSpin_->blockSignals(false);
    }
    applyAnnotationToSelectedFrame();
    updateAnnotationSliderMarkers();
    updateAlignmentStatus();
    // The detail widget was just recreated, so push the current review frame
    // into it. Switching camera while already on the Camera tab does not change
    // the tab index, so the tab-change re-render never runs and the new widget
    // would stay blank until playback starts ("no frame until Play").
    if (isReviewMode_) {
        renderCurrentReviewFrame(false);
    }
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

    // Hide the right Tools edge tab and panel on All Camera (index 0) and
    // Diagnostic (index 2) — they are only meaningful for the Single Camera
    // detail view (index 1).
    if (toolsEdgeTab_) {
        toolsEdgeTab_->setVisible(index == 1);
    }
    if (tracksEdgeTab_) {
        tracksEdgeTab_->setVisible(index == 1 && detailDashboard_
                                   && detailDashboard_->isVisible());
    }
    if (tracksPanel_ && index != 1) {
        tracksPanel_->hide();
        tracksTabHovered_ = false;
        restyleTracksEdgeTab();
    }
    if (rightToolsPanel_ && index != 1) {
        rightToolsPanel_->hide();
        toolsPanelShown_ = false;
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
    const int frameIndex = frameIndexForSliderValue(value);
    seekToFrameIndex(frameIndex, false);
}

int AnalysisView::currentReviewFrameIndex() const {
    const int maxFrame = std::max(0, static_cast<int>(std::floor(totalFrames_)));
    return qBound(0, static_cast<int>(std::floor(currentFrame_ + 0.0001)), maxFrame);
}

int AnalysisView::tlIndexOfOwnFrame(int camIdx, int ownFrame) const {
    if (camIdx == timelineCameraIdx_) {
        return ownFrame;
    }
    const auto tlIt = cameraTimestamps_.find(timelineCameraIdx_);
    const auto camIt = cameraTimestamps_.find(camIdx);
    if (tlIt == cameraTimestamps_.end() || tlIt->second.empty()
            || camIt == cameraTimestamps_.end() || camIt->second.empty()) {
        return -1;
    }
    const std::vector<int64_t>& ts = camIt->second;
    const int64_t t = ts[qBound(0, ownFrame, static_cast<int>(ts.size()) - 1)];
    // Same clock-comparability guard as the display mapping below: camera-local
    // ticks are not cross-comparable with the timeline camera's epoch stamps.
    if (std::llabs(ts.front() - tlIt->second.front()) >= 60000000000LL) {
        return -1;
    }
    int best = static_cast<int>(
        std::lower_bound(tlIt->second.begin(), tlIt->second.end(), t) - tlIt->second.begin());
    if (best >= static_cast<int>(tlIt->second.size())) {
        best = static_cast<int>(tlIt->second.size()) - 1;
    } else if (best > 0 && tlIt->second[best] - t > t - tlIt->second[best - 1]) {
        --best;
    }
    return best;
}

double AnalysisView::timelineFps() const {
    auto it = videoReaders_.find(timelineCameraIdx_);
    if (it == videoReaders_.end() || !it->second) {
        it = videoReaders_.begin();
    }
    if (it != videoReaders_.end() && it->second) {
        const double fps = it->second->getFps();
        if (fps > 0.0) {
            return fps;
        }
    }
    const double fallback = CameraConfig::getFps();
    return fallback > 0.0 ? fallback : 10.0;
}

int AnalysisView::masterFrameForCameraFrame(int camIdx, int ownFrame) const {
    const int offset = (camIdx >= 0 && camIdx < static_cast<int>(cameraFrameOffsets_.size()))
        ? cameraFrameOffsets_[camIdx] : 0;
    const int tl = tlIndexOfOwnFrame(camIdx, ownFrame);
    return (tl >= 0 ? tl : ownFrame) - offset;
}

int AnalysisView::displayedFrameIndexForCamera(int camIdx, int masterFrameIndex) const {
    const int maxIdx = std::max(0, static_cast<int>(std::floor(totalFrames_)));
    if (camIdx < 0 || camIdx >= static_cast<int>(cameraFrameOffsets_.size())) {
        return qBound(0, masterFrameIndex, maxIdx);
    }
    const int requested = qBound(0, masterFrameIndex + cameraFrameOffsets_[camIdx], maxIdx);

    // Mixed-fps events: the timeline index counts the LONGEST camera's
    // frames, so sharing it raw shows different wall-clock moments per tile
    // and runs shorter cameras out of frames before the scrub bar ends (the
    // last stretch froze on their final frame). Map the timeline frame's
    // hardware timestamp to this camera's nearest own frame instead. Readers
    // without timestamps (legacy video events) keep the shared-index
    // behavior; a >60s clock disagreement means the timestamps are not
    // cross-comparable (camera-local ticks), same fallback.
    if (camIdx != timelineCameraIdx_) {
        const auto tlIt = cameraTimestamps_.find(timelineCameraIdx_);
        const auto camIt = cameraTimestamps_.find(camIdx);
        if (tlIt != cameraTimestamps_.end() && !tlIt->second.empty()
                && camIt != cameraTimestamps_.end() && !camIt->second.empty()) {
            const std::vector<int64_t>& ts = camIt->second;
            const int64_t t = tlIt->second[qBound(0, requested,
                static_cast<int>(tlIt->second.size()) - 1)];
            if (std::llabs(ts.front() - tlIt->second.front()) < 60000000000LL) {
                int best = static_cast<int>(
                    std::lower_bound(ts.begin(), ts.end(), t) - ts.begin());
                if (best >= static_cast<int>(ts.size())) {
                    best = static_cast<int>(ts.size()) - 1;
                } else if (best > 0 && ts[best] - t > t - ts[best - 1]) {
                    --best;
                }
                return best;
            }
        }
    }
    return requested;
}

void AnalysisView::renderCurrentReviewFrame(bool updateSlider) {
    const int idx = currentReviewFrameIndex();
    const double relativeFrame = currentFrame_ - triggerFrameIndex_;
    const double relativeSeconds = relativeSecondsForFrameIndex(idx);

    if (updateSlider && playbackSlider_) {
        const bool sliderBlocked = playbackSlider_->blockSignals(true);
        playbackSlider_->setValue(sliderValueForFrameIndex(idx));
        playbackSlider_->blockSignals(sliderBlocked);
    }
    // Keep the dashboard on the playhead. The dashboard plots the selected
    // camera's own-frame series, so feed it the playhead in that camera's
    // frame domain (identity when it shows the timeline camera).
    if (detailDashboard_) {
        const int dashFrame = displayedFrameIndexForCamera(currentDashCam_, idx);
        detailDashboard_->setCurrentFrame(dashFrame);
        // Lazy detail sub-scan for >600-frame events (cheap no-op otherwise).
        maybeScanDetailWindow(dashFrame);
    }

    frameInput_->setText(hasRelativeTimeAxis()
        ? QString::number(relativeSeconds, 'f', 3)
        : QString::number(relativeFrame, 'f', 1));
    updatePlaybackInfoLabel();
    // Keep the sync crosshair in step with the scrub position (and clear it when
    // not in review mode).
    applySyncIndicators();

    if (!isReviewMode_) {
        return;
    }

    QString overlayText = getMetadataOverlayText(idx, relativeFrame);
    QString tooltipText = getMetadataTooltip(idx, relativeFrame);

    // The playback info (relative frame + time from trigger) and the metadata
    // overlay are complementary, so each is passed to the widget as-is.
    const QString playbackText = playbackInfoText_;

    if (isStreamingMode_) {
        for (auto& pair : videoReaders_) {
            int camIdx = pair.first;
            int displayIdx = displayedFrameIndexForCamera(camIdx, idx);
            cv::Mat cvFrame = pair.second->getFrame(displayIdx);

            if (!cvFrame.empty() && camIdx < static_cast<int>(cameraWidgets_.size())) {
                cv::Mat rgb;
                if (cvFrame.channels() == 1) {
                    cv::cvtColor(cvFrame, rgb, cv::COLOR_GRAY2RGB);
                } else if (cvFrame.channels() == 3) {
                    cv::cvtColor(cvFrame, rgb, cv::COLOR_BGR2RGB);
                } else {
                    rgb = cvFrame.clone();
                }
                QImage frameImage(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);

                QImage finalImage = frameImage.copy();
                cameraWidgets_[camIdx]->setFrame(finalImage);
                cameraWidgets_[camIdx]->setTimestamp(overlayText, tooltipText);
                cameraWidgets_[camIdx]->setPlaybackInfo(playbackText);
                applyAnnotationToWidget(cameraWidgets_[camIdx], camIdx, displayIdx);

                if (selectedCameraWidget_ && selectedCameraWidget_->getCameraId() == camIdx) {
                    selectedCameraWidget_->setFrame(finalImage);
                    selectedCameraWidget_->setTimestamp(overlayText, tooltipText);
                    selectedCameraWidget_->setPlaybackInfo(playbackText);
                    applyAnnotationToSelectedFrame();
                }
            }
        }
        return;
    }

    if (recordedSequence_.empty()) {
        return;
    }

    QImage frameImage = recordedSequence_[qBound(0, idx, static_cast<int>(recordedSequence_.size()) - 1)];
    if (frameImage.isNull()) {
        return;
    }

    const bool isTiled = (frameImage.width() > baseWidth_ || frameImage.height() > baseHeight_);
    if (isTiled) {
        int cols = 4;
        int rows = (numCameras_ + cols - 1) / cols;
        int cellW = frameImage.width() / cols;
        int cellH = frameImage.height() / rows;

        for (int i = 0; i < numCameras_; ++i) {
            int r = i / cols;
            int c = i % cols;
            QImage slice = frameImage.copy(c * cellW, r * cellH, cellW, cellH);

            if (i < static_cast<int>(cameraWidgets_.size())) {
                cameraWidgets_[i]->setFrame(slice);
                cameraWidgets_[i]->setTimestamp(overlayText, tooltipText);
                cameraWidgets_[i]->setPlaybackInfo(playbackText);
                applyAnnotationToWidget(cameraWidgets_[i], i, idx);
            }
        }

        if (selectedCameraWidget_) {
            int scId = selectedCameraWidget_->getCameraId();
            if (scId >= 0 && scId < numCameras_) {
                int r = scId / cols;
                int c = scId % cols;
                QImage slice = frameImage.copy(c * cellW, r * cellH, cellW, cellH);
                selectedCameraWidget_->setFrame(slice);
                selectedCameraWidget_->setTimestamp(overlayText, tooltipText);
                selectedCameraWidget_->setPlaybackInfo(playbackText);
                applyAnnotationToSelectedFrame();
            }
        }
        return;
    }

    if (!cameraWidgets_.empty()) {
        cameraWidgets_[0]->setFrame(frameImage);
        cameraWidgets_[0]->setTimestamp(overlayText, tooltipText);
        cameraWidgets_[0]->setPlaybackInfo(playbackText);
        applyAnnotationToWidget(cameraWidgets_[0], 0, idx);
    }
    for (int wi = 1; wi < static_cast<int>(cameraWidgets_.size()); ++wi) {
        cameraWidgets_[wi]->clear();
    }

    if (selectedCameraWidget_) {
        selectedCameraWidget_->setFrame(frameImage);
        selectedCameraWidget_->setTimestamp(overlayText, tooltipText);
        selectedCameraWidget_->setPlaybackInfo(playbackText);
        applyAnnotationToSelectedFrame();
    }
}

void AnalysisView::seekToFrameIndex(int frameIndex, bool updateSlider) {
    currentFrame_ = qBound(0.0, static_cast<double>(frameIndex), totalFrames_);
    renderCurrentReviewFrame(updateSlider);
    updatePlaybackControlsState();
}

QString AnalysisView::annotationKey(int cameraId, int frameIndex) const {
    return QString("cam%1_frame%2").arg(cameraId + 1).arg(frameIndex);
}

QMap<QString, int> AnalysisView::loadEventAnnotations(const QString& videoPath) {
    eventAnnotations_ = QJsonObject();
    defectMarks_ = QJsonObject();
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
        return QMap<QString, int>();
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isObject()) {
        eventAnnotations_ = doc.object();
    }

    QMap<QString, int> parsedOffsets;
    // Alignment keys: "defectMarks" and "cameraOffsets", each "cam{N}" -> int.
    if (eventAnnotations_.contains("defectMarks") && eventAnnotations_["defectMarks"].isObject()) {
        const QJsonObject marks = eventAnnotations_["defectMarks"].toObject();
        for (auto it = marks.begin(); it != marks.end(); ++it) {
            // Preserve values as-is: legacy single int or new array of ints.
            if (it.key().startsWith("cam")) {
                defectMarks_[it.key()] = it.value();
            }
        }
    }
    if (eventAnnotations_.contains("cameraOffsets") && eventAnnotations_["cameraOffsets"].isObject()) {
        const QJsonObject offsets = eventAnnotations_["cameraOffsets"].toObject();
        for (auto it = offsets.begin(); it != offsets.end(); ++it) {
            if (it.key().startsWith("cam") && it.value().isDouble()) {
                parsedOffsets[it.key()] = it.value().toInt();
            }
        }
    }
    eventAnnotations_.remove("defectMarks");
    eventAnnotations_.remove("cameraOffsets");
    return parsedOffsets;
}

void AnalysisView::saveEventAnnotations() {
    if (currentAnnotationPath_.isEmpty()) {
        return;
    }
    eventAnnotations_["defectMarks"] = defectMarks_;
    QJsonObject offsetsObj;
    for (int i = 0; i < static_cast<int>(cameraFrameOffsets_.size()); ++i) {
        offsetsObj[QString("cam%1").arg(i + 1)] = cameraFrameOffsets_[i];
    }
    eventAnnotations_["cameraOffsets"] = offsetsObj;
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
    const int frameIndex = displayedFrameIndexForCamera(cameraId, currentReviewFrameIndex());
    applyAnnotationToWidget(selectedCameraWidget_, cameraId, frameIndex);
    if (markerShapeCombo_ && !eventAnnotations_.contains(annotationKey(cameraId, frameIndex))) {
        selectedCameraWidget_->setMarkerShape(markerShapeCombo_->currentData().toString());
    }
}

QString AnalysisView::getMetadataOverlayText(int frameIndex, double relativeFrame) {
    Q_UNUSED(relativeFrame);
    const QString mode = metadataDisplayCombo_ ? metadataDisplayCombo_->currentData().toString() : QString("standard");

    if (mode == "none") {
        return QString();
    }

    if (mode == "relative") {
        return QString(); // Relative frame/time is already shown in the left playback info.
    }

    if (frameIndex < 0 || frameIndex >= (int)frameMetadata_.size()) {
        return QString();
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
        return QString("TS: %1").arg(meta.displayTime);
    }
    if (mode == "framecounter") {
        return QString("FC: %1").arg(meta.frameCounter);
    }
    if (mode == "realtime") {
        return realTimeText.isEmpty()
            ? QString("Time: N/A")
            : QString("Time: %1").arg(realTimeText);
    }

    QString text = QString("TS: %1 | FC: %2").arg(meta.displayTime).arg(meta.frameCounter);
    if (!realTimeText.isEmpty()) {
        text += QString(" | Time: %1").arg(realTimeText);
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
            playbackTimer_->start(playbackTickIntervalMs());
        } else {
            playbackTimer_->stop();
        }
    }

    updatePlaybackControlsState();
}

int AnalysisView::playbackTickIntervalMs() const {
    // 1.0x = true real-time when the event's capture fps is known (RAW header);
    // legacy 33 ms (~30 fps) assumption otherwise.
    const double fps = (reviewFps_ > 0.0) ? reviewFps_ : (1000.0 / 33.0);
    return std::max(1, static_cast<int>(std::lround(1000.0 / (fps * playbackSpeed_))));
}

void AnalysisView::seekToRelativeFrame(double relativeFrame) {
    if (!playbackSlider_) {
        return;
    }
    seekToFrameIndex(static_cast<int>(std::round(relativeFrame + triggerFrameIndex_)));
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
    seekToFrameIndex(0);
}

int AnalysisView::playbackStepSize() const {
    // The speed selector also drives scrubbing: at 1.0x one step = 1 frame;
    // slower speeds still step 1 frame (rate differs), faster speeds jump
    // multiple frames per step.
    return std::max(1, static_cast<int>(std::round(playbackSpeed_)));
}

void AnalysisView::onPreviousPressed() {
    seekToFrameIndex(currentReviewFrameIndex() - playbackStepSize());
}

void AnalysisView::onPreviousReleased() {}

void AnalysisView::onResetClicked() {
    seekToFrameIndex(triggerFrameIndex_);
}

void AnalysisView::onNextPressed() {
    seekToFrameIndex(currentReviewFrameIndex() + playbackStepSize());
}

void AnalysisView::onNextReleased() {}

void AnalysisView::onEndClicked() {
    seekToFrameIndex(static_cast<int>(std::floor(totalFrames_)));
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
    // Slow speeds need 2 decimals (0.05x would otherwise round to 0.1x);
    // 1.0x / 2.0x keep 1 decimal to match the menu labels.
    const QString speedText = playbackSpeed_ < 1.0
        ? QString("%1x").arg(playbackSpeed_, 0, 'f', 2)
        : QString("%1x").arg(playbackSpeed_, 0, 'f', 1);
    speedButton_->setText(speedText);

    // Scrubbing speed: holding a step button repeats at the chosen rate so
    // Ultra Slow scrubs frame-by-frame slowly and Very Fast jumps multiple
    // frames per repeat. Same real-time calibration as playback.
    const int repeatInterval = playbackTickIntervalMs();
    if (prevButton_) {
        prevButton_->setAutoRepeatInterval(repeatInterval);
    }
    if (nextButton_) {
        nextButton_->setAutoRepeatInterval(repeatInterval);
    }

    // Update timer interval if playing
    if (isPlaying_) {
        setPlaybackPlaying(true);
    }

    // Dashboard detail strip is an inspection aid: show it only at slow
    // speeds, hide again at 1x and above.
    if (detailDashboard_) {
        detailDashboard_->setDetailZoomEnabled(playbackSpeed_ < 1.0);
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
        
        configureReviewSliderRange();
        
        updateSliderZeroMarker();
        
        // Go to start (Trigger Frame)
        currentFrame_ = triggerFrameIndex_; 
        onSliderMoved(0); 
        
        updatePlaybackControlsState();
        // Media-player keys (Left/Right step, Space play/pause) work
        // immediately once the review is shown.
        setFocus();
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
    configureReviewSliderRange();
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
    // Right tools panel: mode gating only (Lock pins visibility, not the tools).
    if (cameraOffsetSpin_) {
        cameraOffsetSpin_->setEnabled(isReviewMode_);
    }
    if (markDefectButton_) {
        markDefectButton_->setEnabled(isReviewMode_ && selectedCameraId_ >= 0);
    }
    if (alignButton_) {
        alignButton_->setEnabled(isReviewMode_ && !videoReaders_.empty());
    }
    if (resetOffsetsButton_) {
        resetOffsetsButton_->setEnabled(isReviewMode_ && !videoReaders_.empty());
    }

    // Gray out appearance when disabled, restore theme colors when enabled
    ThemeColors tc = CameraConfig::getThemeColors();
    if (!hasData) {
        playbackPanel_->setStyleSheet(QString(
            "QWidget#playbackPanel { background-color: %1; border-top: 1px solid %2; }"
            // Scoped to descendants of the panel: a bare QWidget rule would
            // also color tooltips shown over the panel's controls.
            "QWidget#playbackPanel QWidget { color: %3; }"
        ).arg(tc.bg, tc.border, tc.border));
    } else {
        playbackPanel_->setStyleSheet(QString(
            "QWidget#playbackPanel { background-color: %1; border-top: 1px solid %2; }")
            .arg(tc.bg, tc.border));
    }
}

void AnalysisView::setLiveMode() {
    cancelCameraKeyEntry();  // no pending camera number outlives the mode
    isReviewMode_ = false;
    isRecording_ = false;
    reviewFps_ = 0.0;
    detailWindowKey_.clear();
    if (detailDashboard_) detailDashboard_->setDetailLoading(false);
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

    // Dashboards are review-only visualizations.
    if (detailDashboard_) {
        detailDashboard_->clear();
    }
}

void AnalysisView::updatePlaybackInfoLabel() {
    if (!isReviewMode_) {
        playbackInfoText_.clear();
        return;
    }

    const double relFrames = currentFrame_ - triggerFrameIndex_;
    double fps = 0.0;
    if (!videoReaders_.empty() && videoReaders_.begin()->second) {
        fps = videoReaders_.begin()->second->getFps();
    }
    if (fps <= 0.0) {
        fps = 1.0;
    }
    const int frameIndex = currentReviewFrameIndex();
    const double seconds = frameIndex >= 0
        ? relativeSecondsForFrameIndex(frameIndex)
        : (relFrames / fps);
    // Values only; the named description lives in the tooltip.
    const QString baseText = QString("%1 | %2%3 s")
        .arg(relFrames, 0, 'f', 1)
        .arg(seconds >= 0.0 ? "+" : "")
        .arg(seconds, 0, 'f', 3);
    const QString speedSummary = currentSpeedSummary(seconds);
    playbackInfoText_ = speedSummary.isEmpty() ? baseText : QString("%1 | %2").arg(baseText, speedSummary);

    // Full, descriptive tooltip: explain every value the label shows.
    QStringList tooltipLines;
    tooltipLines << QString("Relative frame: %1 (0 = the trigger frame; negative = before the trigger, positive = after)")
        .arg(relFrames, 0, 'f', 1);
    tooltipLines << QString("Time from trigger: %1%2 s")
        .arg(seconds >= 0.0 ? "+" : "")
        .arg(seconds, 0, 'f', 3);
    if (SpeedProfile::hasValidAnchors(currentEventInfo_.speedAnchors)
            || std::isfinite(currentEventInfo_.speedValue)) {
        const bool detailTabActive = tabWidget_ && tabWidget_->currentIndex() == 1 && selectedCameraId_ >= 0;
        const int cameraId = selectedCameraId_;
        const int basePositionMm = currentEventCameraPositionMm(cameraId);
        // Local speed at this camera (interpolated between the recorded speed
        // anchors), so draw between drive groups is reflected in the distance.
        const double localSpeed = SpeedProfile::speedAt(
            basePositionMm, currentEventInfo_.speedAnchors, currentEventInfo_.speedValue);
        if (std::isfinite(localSpeed)) {
            const QString speedUnit = currentEventInfo_.speedUnit.isEmpty()
                ? QStringLiteral("m/min") : currentEventInfo_.speedUnit;
            tooltipLines << QString("Machine speed: %1 %2")
                .arg(localSpeed, 0, 'f', 2)
                .arg(speedUnit);
            if (detailTabActive) {
                const double deltaMm = localSpeed * 1000.0 / 60.0 * seconds
                    * static_cast<double>(currentEventInfo_.positionDirectionSign >= 0 ? 1 : -1);
                tooltipLines << QString("Distance traveled from trigger: %1 mm").arg(deltaMm, 0, 'f', 1);
                tooltipLines << QString("Camera position: %1 mm").arg(basePositionMm + deltaMm, 0, 'f', 1);
            }
        }
        if (currentEventInfo_.speedStale) {
            tooltipLines << QStringLiteral("Note: machine speed was stale at capture — distance/alignment may be inaccurate");
        }
    }
    const QString fullTooltip = tooltipLines.join("\n");
    for (AnalysisVideoWidget* widget : cameraWidgets_) {
        if (widget) {
            widget->setToolTip(fullTooltip);
        }
    }
    if (selectedCameraWidget_) {
        selectedCameraWidget_->setToolTip(fullTooltip);
    }
}

bool AnalysisView::hasRelativeTimeAxis() const {
    return !frameMetadata_.empty()
        && triggerFrameIndex_ >= 0
        && triggerFrameIndex_ < static_cast<int>(frameMetadata_.size());
}

double AnalysisView::relativeSecondsForFrameIndex(int frameIndex) const {
    const int clampedFrameIndex = qBound(0, frameIndex, std::max(0, static_cast<int>(frameMetadata_.size()) - 1));
    if (!hasRelativeTimeAxis()) {
        return static_cast<double>(clampedFrameIndex - triggerFrameIndex_);
    }

    // Zero reference = the trigger frame of the reader that provided the
    // metadata (its own flagged trigger frame), not necessarily the event's
    // primary camera.
    int zeroIndex = metadataTriggerIndex_;
    if (zeroIndex < 0 || zeroIndex >= static_cast<int>(frameMetadata_.size())) {
        zeroIndex = qBound(0, triggerFrameIndex_, static_cast<int>(frameMetadata_.size()) - 1);
    }
    const int64_t triggerTimestamp = frameMetadata_[zeroIndex].timestamp;
    const int64_t frameTimestamp = frameMetadata_[clampedFrameIndex].timestamp;
    return static_cast<double>(frameTimestamp - triggerTimestamp) / 1000000000.0;
}

int AnalysisView::sliderValueForFrameIndex(int frameIndex) const {
    const int clampedFrameIndex = qBound(0, frameIndex, std::max(0, static_cast<int>(std::floor(totalFrames_))));
    if (hasRelativeTimeAxis()) {
        return static_cast<int>(std::round(relativeSecondsForFrameIndex(clampedFrameIndex) * kReviewSliderUnitsPerSecond));
    }
    return static_cast<int>(std::round((clampedFrameIndex - triggerFrameIndex_) * 10.0));
}

int AnalysisView::frameIndexForSliderValue(int value) const {
    if (!hasRelativeTimeAxis()) {
        const int frameIndex = static_cast<int>(std::round(value / 10.0)) + triggerFrameIndex_;
        return qBound(0, frameIndex, std::max(0, static_cast<int>(std::floor(totalFrames_))));
    }

    const double targetSeconds = static_cast<double>(value) / kReviewSliderUnitsPerSecond;
    int bestIndex = triggerFrameIndex_;
    double bestError = std::numeric_limits<double>::max();
    const int maxIndex = std::min(static_cast<int>(frameMetadata_.size()) - 1,
                                  std::max(0, static_cast<int>(std::floor(totalFrames_))));
    for (int i = 0; i <= maxIndex; ++i) {
        const double error = std::abs(relativeSecondsForFrameIndex(i) - targetSeconds);
        if (error < bestError) {
            bestError = error;
            bestIndex = i;
        }
    }
    return bestIndex;
}

void AnalysisView::configureReviewSliderRange() {
    if (!playbackSlider_) {
        return;
    }

    if (hasRelativeTimeAxis()) {
        playbackSlider_->setRange(sliderValueForFrameIndex(0), sliderValueForFrameIndex(static_cast<int>(std::floor(totalFrames_))));
        return;
    }

    const int minRange = -triggerFrameIndex_ * 10;
    const int maxRange = (totalFrames_ - triggerFrameIndex_) * 10;
    playbackSlider_->setRange(minRange, maxRange);
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
    int handleWidth = 10;  // From playback slider stylesheet
    int usableWidth = sliderRect.width() - handleWidth;
    int zeroValue = 0;  // The trigger frame is always at value 0
    
    // Map value to pixel position
    float ratio = static_cast<float>(zeroValue - sliderMin) / (sliderMax - sliderMin);
    int xPos = sliderRect.x() + (handleWidth / 2) + static_cast<int>(ratio * usableWidth);
    // The flag sits ABOVE the scrubbing line, inside the strip reserved by the
    // toolbar row's top margin — never on the track, so it can't be confused
    // with the playhead nor block scrubbing.
    const int yPos = sliderRect.y() - sliderZeroMarker_->height() - 4;
    
    // Position and show the marker
    sliderZeroMarker_->move(xPos - sliderZeroMarker_->width() / 2, yPos);
    sliderZeroMarker_->raise();
    sliderZeroMarker_->show();
}

void AnalysisView::updateAnnotationSliderMarkers() {
    for (QLabel* marker : annotationSliderMarkers_) {
        if (marker) {
            marker->deleteLater();
        }
    }
    annotationSliderMarkers_.clear();

    // Marks live in their own object (stripped from eventAnnotations_ on load),
    // so a marks-only sidecar must still render its slider dots.
    if (!isReviewMode_ || !playbackSlider_
        || (eventAnnotations_.isEmpty() && defectMarks_.isEmpty())) {
        return;
    }

    const int sliderMin = playbackSlider_->minimum();
    const int sliderMax = playbackSlider_->maximum();
    if (sliderMax == sliderMin) {
        return;
    }

    const ThemeColors tc = CameraConfig::getThemeColors();
    const QRect sliderRect = playbackSlider_->geometry();
    const int handleWidth = 10;
    const int usableWidth = std::max(1, sliderRect.width() - handleWidth);
    // Draw annotation dots below the slider groove so they don't crowd the zero marker.
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
            // Annotation frames live in the placing camera's own frame domain;
            // the scrub bar runs on the shared timeline, so convert to the
            // master frame where that camera displays the marked frame.
            const int camIndex = key.left(framePos).mid(3).toInt() - 1;
            const int masterIndex = masterFrameForCameraFrame(camIndex, frameIndex);
            framesWithMarkers.insert(masterIndex);
            const QString cameraLabel = key.left(framePos);
            frameCameraLabels[masterIndex].append(cameraLabel);
        }
    }

    for (int frameIndex : framesWithMarkers) {
        const int sliderValue = sliderValueForFrameIndex(frameIndex);
        if (sliderValue < sliderMin || sliderValue > sliderMax) {
            continue;
        }
        const double ratio = static_cast<double>(sliderValue - sliderMin) / (sliderMax - sliderMin);
        const int xPos = sliderRect.x() + (handleWidth / 2) + static_cast<int>(ratio * usableWidth);
        const int relativeFrame = frameIndex - triggerFrameIndex_;
        const double relativeSeconds = relativeSecondsForFrameIndex(frameIndex);

        QLabel* marker = new QLabel(playbackPanel_);
        marker->setFixedSize(7, 7);
        const QString cameraText = frameCameraLabels.value(frameIndex).join(", ").replace("cam", "Camera ");
        marker->setToolTip(detailTabActive
            ? QString("Marker on Camera %1\nFrame %2 (absolute %3)\nTime %4%5 s")
                .arg(selectedCameraId_ + 1)
                .arg(relativeFrame)
                .arg(frameIndex)
                .arg(relativeSeconds >= 0.0 ? "+" : "")
                .arg(relativeSeconds, 0, 'f', 3)
            : QString("Marker on frame %1 (absolute %2)\nTime %3%4 s\n%5")
                .arg(relativeFrame)
                .arg(frameIndex)
                .arg(relativeSeconds >= 0.0 ? "+" : "")
                .arg(relativeSeconds, 0, 'f', 3)
                .arg(cameraText));
        marker->setStyleSheet(QString("background-color: %1; border: 1px solid white; border-radius: 3px;")
            .arg(QColor(tc.primary).lighter(110).name()));
        marker->move(xPos - 3, yPos);
        marker->setCursor(Qt::PointingHandCursor);
        marker->installEventFilter(this);
        marker->setProperty("annotationFrame", frameIndex);
        marker->raise();
        marker->show();
        annotationSliderMarkers_.append(marker);
    }

    // Second pass: manual defect marks (per-camera, several per camera allowed),
    // drawn one row lower in danger red. Marks from cameras outside this event
    // (stale sidecar data) are skipped.
    const int defectYPos = yPos + 10;
    for (auto it = defectMarks_.begin(); it != defectMarks_.end(); ++it) {
        const QString camKey = it.key();
        const int camNumber = camKey.mid(3).toInt();
        const int camIndex = camNumber - 1;
        if (camNumber <= 0 || videoReaders_.count(camIndex) == 0) {
            continue;
        }
        if (detailTabActive && camNumber != selectedCameraId_ + 1) {
            continue;
        }
        const QVector<int> frames = defectMarkFrames(it.value());
        if (frames.isEmpty()) {
            continue;
        }
        const bool multi = frames.size() > 1;
        for (int mi = 0; mi < frames.size(); ++mi) {
            // Mark frames are the camera's own frames — convert to the master
            // domain shared by the scrub bar (same as the annotation pass).
            const int frameIndex = masterFrameForCameraFrame(camIndex, frames.at(mi));
            const int sliderValue = sliderValueForFrameIndex(frameIndex);
            if (sliderValue < sliderMin || sliderValue > sliderMax) {
                continue;
            }
            const double ratio = static_cast<double>(sliderValue - sliderMin) / (sliderMax - sliderMin);
            const int xPos = sliderRect.x() + (handleWidth / 2) + static_cast<int>(ratio * usableWidth);
            const double relativeSeconds = relativeSecondsForFrameIndex(frameIndex);

            QLabel* marker = new QLabel(playbackPanel_);
            marker->setFixedSize(9, 9);
            marker->setToolTip(multi
                ? QString("Defect mark %1: Camera %2 @ frame %3 (%4%5 s)")
                    .arg(mi + 1).arg(camNumber).arg(frameIndex)
                    .arg(relativeSeconds >= 0.0 ? "+" : "")
                    .arg(relativeSeconds, 0, 'f', 3)
                : QString("Defect mark: Camera %1 @ frame %2 (%3%4 s)")
                    .arg(camNumber).arg(frameIndex)
                    .arg(relativeSeconds >= 0.0 ? "+" : "")
                    .arg(relativeSeconds, 0, 'f', 3));
            marker->setStyleSheet("background-color: #FF5A5A; border: 1px solid white; border-radius: 4px;");
            marker->move(xPos - 4, defectYPos);
            marker->setCursor(Qt::PointingHandCursor);
            marker->installEventFilter(this);
            marker->setProperty("annotationFrame", frameIndex);
            marker->raise();
            marker->show();
            annotationSliderMarkers_.append(marker);
        }
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
    
    const int nextFrameIndex = currentReviewFrameIndex() + std::max(1, static_cast<int>(std::round(playbackSpeed_)));

    if (nextFrameIndex >= totalFrames_) {
        seekToFrameIndex(static_cast<int>(std::floor(totalFrames_)));
        setPlaybackPlaying(false);
        return;
    }

    seekToFrameIndex(nextFrameIndex);
}

void AnalysisView::addPaperBreakEvent(const std::string& timestamp, int triggerIndex, int totalFrames,
                                      int primaryCameraId) {
    QString rawTs = QString::fromStdString(timestamp);
    latestAddedEventTimestamp_ = rawTs;
    reloadEventTables();
    selectLatestEvent();
    
    // Load RAW BINARY from disk using the primary camera of the event. For a
    // group-restricted trigger the primary camera may not be camera 1, and only
    // that group's cam files exist on disk.
    const int primaryCam = primaryCameraId > 0 ? primaryCameraId : 1;
    QString binPath = QDir(CameraConfig::getEventStoragePath())
        .filePath(QString("event_%1_cam%2.bin").arg(rawTs).arg(primaryCam));
    if (!QFile::exists(binPath)) {
        // Fallback: scan for whichever camera file exists for this timestamp.
        QDir eventDir(CameraConfig::getEventStoragePath());
        const QStringList files = eventDir.entryList(
            QStringList() << QString("event_%1_cam*.bin").arg(rawTs), QDir::Files, QDir::Name);
        if (!files.isEmpty()) {
            binPath = eventDir.filePath(files.first());
        }
    }
    startReviewFromFile(binPath, triggerIndex);
    
}

int AnalysisView::addEventRow(const QString& timestamp, const QString& reason, bool permanent,
                              bool selectRow, int group, int defectFrame) {
    QTableWidget* targetTable = permanent ? permanentPaperBreakTable_ : paperBreakTable_;
    const int row = targetTable->rowCount();
    targetTable->insertRow(row);

    const bool isNewRecentEvent = !permanent && !latestAddedEventTimestamp_.isEmpty() && timestamp == latestAddedEventTimestamp_;
    QTableWidgetItem* timeItem = new QTableWidgetItem(formatTimestamp(timestamp));
    QTableWidgetItem* reasonItem = new QTableWidgetItem(reason);
    const QString groupText = group >= 0 ? CameraGroup::name(group) : QStringLiteral("All");
    QTableWidgetItem* groupItem = new QTableWidgetItem(groupText);
    QTableWidgetItem* frameItem = new QTableWidgetItem(
        defectFrame >= 0 ? QString::number(defectFrame) : QStringLiteral("—"));
    timeItem->setData(Qt::UserRole, timestamp);
    timeItem->setData(Qt::UserRole + 2, permanent);
    timeItem->setData(Qt::UserRole + 3, isNewRecentEvent);
    reasonItem->setData(Qt::UserRole + 2, permanent);
    reasonItem->setData(Qt::UserRole + 3, isNewRecentEvent);
    groupItem->setData(Qt::UserRole + 1, group);
    groupItem->setData(Qt::UserRole + 2, permanent);
    groupItem->setData(Qt::UserRole + 3, isNewRecentEvent);
    frameItem->setData(Qt::UserRole + 1, defectFrame);
    frameItem->setData(Qt::UserRole + 2, permanent);
    frameItem->setData(Qt::UserRole + 3, isNewRecentEvent);
    frameItem->setToolTip("Frame position of the defect within the recording (0-based). The same physical defect lands at this frame in every camera of the group.");
    groupItem->setToolTip(group >= 0
        ? QString("Trigger wired to the %1 section. Only cameras assigned to this group were recorded.").arg(groupText)
        : "Trigger recorded all active cameras.");

    if (isNewRecentEvent) {
        ThemeColors tc = CameraConfig::getThemeColors();

        // Row highlight instead of a timestamp icon: the whole new-event row
        // gets a subtle primary tint so the latest trigger stands out without
        // an icon eating into the Trigger Time column's timestamp text.
        QColor rowTint(tc.primary);
        rowTint.setAlpha(36);
        timeItem->setBackground(rowTint);
        reasonItem->setBackground(rowTint);
        groupItem->setBackground(rowTint);
        frameItem->setBackground(rowTint);

        QFont newEventFont = timeItem->font();
        newEventFont.setBold(true);
        timeItem->setFont(newEventFont);
        reasonItem->setFont(newEventFont);
    }

    targetTable->setItem(row, 0, timeItem);
    targetTable->setItem(row, 1, reasonItem);
    targetTable->setItem(row, 2, groupItem);
    targetTable->setItem(row, 3, frameItem);
    sortLogTable(targetTable);
    // The sort moves rows around, so re-resolve the added row's index from
    // its item rather than reusing the pre-sort append position.
    const int sortedRow = targetTable->row(timeItem);

    if (isNewRecentEvent) {
        // Pulse the highlight: a brief bright flash that decays onto the
        // static tint. The row is re-located by flag on every tick, so the
        // exact index here does not need to be stable.
        startNewEventPulse(targetTable);
    }

    if (selectRow) {
        targetTable->selectRow(sortedRow);
        targetTable->scrollToItem(timeItem);
    }
    return sortedRow;
}

void AnalysisView::addPendingEventRow(const QString& timestamp, const QString& reason) {
    pendingEventTimestamp_ = timestamp;
    pendingEventStartMs_ = QDateTime::currentMSecsSinceEpoch();
    insertPendingEventRow();
    updateRecordCountLabel();
}

void AnalysisView::insertPendingEventRow() {
    if (pendingEventTimestamp_.isEmpty()) return;
    // Stale guard: an event that was armed but never saved (all cameras died,
    // save aborted) would otherwise show "Recording…" forever. 2 min is far
    // beyond the normal capture+save window; drop it after that.
    if (QDateTime::currentMSecsSinceEpoch() - pendingEventStartMs_ > 120000) {
        pendingEventTimestamp_.clear();
        return;
    }
    // Idempotence guard: never stack a second "Recording…" row while one is
    // already visible. Callers normally clear the table first (reloadEventTables
    // starts with setRowCount(0)), but a direct double call would otherwise
    // append a duplicate placeholder.
    QTableWidget* table = paperBreakTable_;
    for (int r = 0; r < table->rowCount(); ++r) {
        if (QTableWidgetItem* it = table->item(r, 0)) {
            if (it->data(Qt::UserRole + 4).toBool()) {
                return;
            }
        }
    }
    // addEventRow re-sorts the table (newest first), so the just-added row is
    // not necessarily the last one anymore; style it at the index returned.
    const int row = addEventRow(pendingEventTimestamp_, QStringLiteral("Recording…"),
                                false, false);
    for (int col = 0; col < table->columnCount() && col < 4; ++col) {
        if (QTableWidgetItem* it = table->item(row, col)) {
            it->setData(Qt::UserRole + 4, true); // pending marker
            QFont f = it->font();
            f.setItalic(true);
            it->setFont(f);
            it->setForeground(QColor(128, 128, 128));
        }
    }
}

void AnalysisView::reloadEventTables() {
    const auto events = EventDatabase::instance().getAllEvents();

    std::cout << "[AnalysisView] Loading " << events.size() << " historical events..." << std::endl;

    paperBreakTable_->setRowCount(0);
    permanentPaperBreakTable_->setRowCount(0);

    for (const auto& event : events) {
        const QString triggerReason = event.triggerReason.trimmed().isEmpty()
            ? QStringLiteral("Triggered")
            : event.triggerReason.trimmed();
        addEventRow(event.timestamp, triggerReason, event.permanent, false,
                    event.triggerGroup, event.triggerIndex);
        if (event.timestamp == pendingEventTimestamp_) {
            pendingEventTimestamp_.clear(); // real event landed — retire placeholder
        }
    }
    insertPendingEventRow();

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
    cameraTimestamps_.clear();
    timelineCameraIdx_ = -1;
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
    // Enable the Delete segment (always visible so the segmented control keeps
    // its divider; only the enabled state is gated by admin + this toggle).
    if (deleteButton_) {
        deleteButton_->setEnabled(enabled);
    }

    // Instant Clear follows the same admin + delete-mode gate.
    const bool canClear = enabled && adminMode_;
    if (instantClearButton_) {
        instantClearButton_->setEnabled(canClear);
    }
    if (instantClearKeepSpin_) {
        instantClearKeepSpin_->setEnabled(canClear);
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
    
    // Re-sync the metadata overlay default from UI Preferences: apply the
    // configured mode to the current event (and to any future ones) whenever
    // the user saves preferences.
    if (metadataDisplayCombo_) {
        const QString savedMode = CameraConfig::getAnalysisViewStyle().defaultMetadataMode;
        const int savedIndex = metadataDisplayCombo_->findData(savedMode);
        if (savedIndex >= 0 && metadataDisplayCombo_->currentData().toString() != savedMode) {
            metadataDisplayCombo_->setCurrentIndex(savedIndex);
        }
    }
    
    // 1. Sidebar Buttons
    serverButton_->setStyleSheet(serverConnecting_ ? makeSidebarConnectingButtonStyle(tc)
                                                   : makeSidebarStateButtonStyle(tc, serverRunning_));
    
    adminButton_->setStyleSheet(adminMode_ ? makeSidebarOutlineButtonStyle(tc, true)
                                           : makeSidebarPrimaryButtonStyle(tc));
    
    if (splitActionRow_) {
        splitActionRow_->setStyleSheet(makeSidebarSplitActionStyle(tc));
    }
    
    // 2. Playback and tab surface/typography
    // First, ensure the current disabled/enabled state uses the new colors
    updatePlaybackControlsState();
    
    applyAnalysisViewStyle();
    
    if (diagnosticTab_) {
        // Rebuild the single-camera tab only when a valid camera is selected.
        updateDynamicTab(selectedCameraId_);
    }
    
    togglePermanentTableButton_->setStyleSheet(makeSidebarUtilityButtonStyle(tc));
    if (instantClearButton_) {
        instantClearButton_->setStyleSheet(makeSidebarUtilityButtonStyle(tc));
    }
    if (toolsLockButton_) {
        toolsLockButton_->setStyleSheet(makeSidebarUtilityButtonStyle(tc));
    }
    applyToolsPanelTheme();

    // Re-apply the current delete-mode state (the segment itself is always
    // visible now, so drive it from the toggle instead of button visibility).
    setDeleteEnabled(enableDeleteCheck_ ? enableDeleteCheck_->isChecked() : false);
}

void AnalysisView::setPlaybackPosition(double frame) {
    currentFrame_ = frame;
    playbackSlider_->setValue(static_cast<int>(frame * 10));
    frameInput_->setText(QString::number(frame, 'f', 1));
}

void AnalysisView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    positionToolsPanel();
    updateSliderZeroMarker();
    updateAnnotationSliderMarkers();
    updateLogTableReasonWidths();
}

void AnalysisView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    positionToolsPanel();

    // The sidebar is fixed-width, so this only runs once the tables have their
    // final size; makes Trigger Time + Reason fill the visible width.
    updateLogTableReasonWidths();
    
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

    cancelCameraKeyEntry();
    isPlaying_ = false;
    reviewFps_ = 0.0;
    detailWindowKey_.clear();
    if (detailDashboard_) detailDashboard_->setDetailLoading(false);
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
    // Stop dashboard scan/thumbnail workers; the scanner owns its own reader
    // so releasing videoReaders_ below stays safe.
    if (signalScanner_ && signalScanner_->isRunning()) {
        signalScanner_->cancel();
    }
    pendingScanPaths_.clear();
    if (thumbWatcher_ && thumbWatcher_->isRunning()) {
        thumbWatcher_->cancel();
        thumbWatcher_->waitForFinished();
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
    cameraTimestamps_.clear();
    timelineCameraIdx_ = -1;
    videoReaders_.clear();
    videoReaderPaths_.clear();
    signalByCam_.clear();
    if (detailDashboard_) {
        detailDashboard_->clear();
    }
    currentAnnotationPath_.clear();
    currentEventCameraLabels_.clear();
    currentEventInfo_ = EventDatabase::EventInfo();

    const int configuredCameraCount = CameraConfig::getCameraCount();
    if (configuredCameraCount > 0 && configuredCameraCount != static_cast<int>(cameraWidgets_.size())) {
        setCameraCount(configuredCameraCount);
    }
    eventAnnotations_ = QJsonObject();
    cameraFrameOffsets_.clear();
    defectMarks_ = QJsonObject();
    syncedMasterFrames_.clear();
    if (cameraOffsetSpin_) {
        cameraOffsetSpin_->blockSignals(true);
        cameraOffsetSpin_->setValue(0);
        cameraOffsetSpin_->blockSignals(false);
    }
    updateAnnotationSliderMarkers();
    updatePlaybackControlsState();
    updateAlignmentStatus();

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
int AnalysisView::currentEventCameraPositionMm(int cameraId) const {
    if (cameraId >= 0 && cameraId < static_cast<int>(currentEventInfo_.cameraPositionsMm.size())) {
        return currentEventInfo_.cameraPositionsMm[static_cast<size_t>(cameraId)];
    }
    if (cameraId >= 0 && cameraId < CameraConfig::getCameraCount()) {
        return CameraConfig::getCameraInfo(cameraId).machinePosition;
    }
    return 0;
}

QString AnalysisView::currentSpeedSummary(double relativeSeconds) const {
    if (!SpeedProfile::hasValidAnchors(currentEventInfo_.speedAnchors)
            && !std::isfinite(currentEventInfo_.speedValue)) {
        return QString();
    }

    const bool detailTabActive = tabWidget_ && tabWidget_->currentIndex() == 1 && selectedCameraId_ >= 0;
    const int basePositionMm = detailTabActive ? currentEventCameraPositionMm(selectedCameraId_) : 0;
    // Local speed at the selected camera (interpolated between the recorded
    // speed anchors), so draw between drive groups is reflected here too.
    const double localSpeed = SpeedProfile::speedAt(
        basePositionMm, currentEventInfo_.speedAnchors, currentEventInfo_.speedValue);

    QStringList parts;
    parts.append(QString("%1 %2")
        .arg(localSpeed, 0, 'f', 2)
        .arg(currentEventInfo_.speedUnit.isEmpty() ? QStringLiteral("m/min") : currentEventInfo_.speedUnit));

    if (detailTabActive) {
        const double deltaMm = localSpeed * 1000.0 / 60.0 * relativeSeconds
            * static_cast<double>(currentEventInfo_.positionDirectionSign >= 0 ? 1 : -1);
        parts.append(QString("%1 mm").arg(deltaMm, 0, 'f', 1));
        parts.append(QString("%1 mm").arg(basePositionMm + deltaMm, 0, 'f', 1));
    }

    if (currentEventInfo_.speedStale) {
        parts.append(QStringLiteral("Stale"));
    }
    return parts.join(" | ");
}

void AnalysisView::onCameraOffsetChanged(int value) {
    if (!isReviewMode_ || selectedCameraId_ < 0) {
        return;
    }
    if (selectedCameraId_ >= static_cast<int>(cameraFrameOffsets_.size())) {
        return;
    }
    cameraFrameOffsets_[selectedCameraId_] = value;
    renderCurrentReviewFrame(false);
    saveEventAnnotations();
    updateAlignmentStatus();
}

void AnalysisView::onToolsLockToggled() {
    toolsLocked_ = !toolsLocked_;
    if (toolsLockButton_) {
        toolsLockButton_->setText(toolsLocked_ ? "Unlock" : "Lock");
        toolsLockButton_->setToolTip(toolsLocked_
            ? "Pinned: the panel stays visible even when the mouse leaves it."
            : "Unpinned: the panel auto-shows when the mouse hovers its area and hides on leave.");
    }
    if (toolsLocked_) {
        toolsPanelShown_ = true;
        animateToolsPanelShow();
    } else {
        onToolsHoverTick();  // hide immediately if the cursor is elsewhere
    }
}

void AnalysisView::restyleToolsEdgeTab() {
    if (!toolsEdgeTab_) {
        return;
    }
    const ThemeColors tc = CameraConfig::getThemeColors();
    const bool hovered = toolsTabHovered_;
    const QString bg = hovered ? QColor(tc.primary).darker(175).name() : tc.bg;
    const QString border = hovered ? tc.primary : tc.border;
    toolsEdgeTab_->setStyleSheet(QString(
        "QWidget#toolsEdgeTab { background-color: %1; border: 1px solid %2;"
        " border-radius: 4px; }")
        .arg(bg, border));
    const QColor fg = hovered ? QColor(tc.primary).lighter(140) : QColor(tc.text).lighter(125);
    toolsEdgeTab_->setPixmap(makeVerticalTabPixmap(QStringLiteral("TOOLS"), fg,
                                                   toolsEdgeTab_->width(),
                                                   toolsEdgeTab_->height()));
}

void AnalysisView::restyleTracksEdgeTab() {
    if (!tracksEdgeTab_) {
        return;
    }
    const ThemeColors tc = CameraConfig::getThemeColors();
    const QString bg = tracksTabHovered_ ? QColor(tc.primary).darker(175).name() : tc.bg;
    const QString border = tracksTabHovered_ ? tc.primary : tc.border;
    tracksEdgeTab_->setStyleSheet(QString(
        "QWidget#tracksEdgeTab { background-color: %1; border: 1px solid %2;"
        " border-radius: 4px; }")
        .arg(bg, border));
    const QColor fg = tracksTabHovered_ ? QColor(tc.primary).lighter(140)
                                        : QColor(tc.text).lighter(125);
    tracksEdgeTab_->setPixmap(makeVerticalTabPixmap(QStringLiteral("TRACKS"), fg,
                                                    tracksEdgeTab_->width(),
                                                    tracksEdgeTab_->height()));
}

void AnalysisView::applyToolsPanelTheme() {
    const ThemeColors tc = CameraConfig::getThemeColors();
    if (rightToolsPanel_) {
        rightToolsPanel_->setStyleSheet(QString(
            "QWidget#rightToolsPanel { background-color: %1; border: 1px solid %2; border-radius: 8px; }")
            .arg(tc.bg, tc.border));
    }
    if (markerToolCheck_) {
        markerToolCheck_->setStyleSheet(QString("QCheckBox { color: %1; font-size: 11px; }").arg(tc.text));
    }
    if (dashboardToggleCheck_) {
        dashboardToggleCheck_->setStyleSheet(QString("QCheckBox { color: %1; font-size: 11px; }").arg(tc.text));
    }
    if (tracksEdgeTab_) {
        restyleTracksEdgeTab();
    }
    if (tracksPanel_) {
        tracksPanel_->setStyleSheet(QString(
            "QWidget#tracksPanel { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
            "QLabel { color: %3; font-size: 11px; font-weight: bold; }"
            "QCheckBox { color: %3; font-size: 11px; }")
            .arg(tc.bg, tc.border, tc.text));
    }
    if (dashLoadingLabel_) {
        // Text-only load status (no bar) — keep the label readable in both
        // light/dark themes.
        dashLoadingLabel_->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
            .arg(QColor(tc.text).name()));
    }
    if (markerShapeCombo_) {
        markerShapeCombo_->setStyleSheet(QString(
            "QComboBox { background: %1; color: %2; border: 1px solid %3; border-radius: 5px; padding: 2px 6px; }"
            "QComboBox:hover { border-color: %4; }"
            "QComboBox::drop-down { border: none; width: 18px; }"
            "QComboBox QAbstractItemView { background: %1; color: %2; selection-background-color: %5; }")
            .arg(tc.btnBg, tc.text, tc.border, tc.primary, tc.btnHover));
    }
    if (zoomSlider_) {
        zoomSlider_->setStyleSheet(makePlaybackSliderStyle(tc));
    }
    if (brightnessSlider_) {
        brightnessSlider_->setStyleSheet(makePlaybackSliderStyle(tc));
    }
    if (zoomValueLabel_) {
        zoomValueLabel_->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: 700; min-width: 34px;").arg(tc.text));
    }
    if (brightnessValueLabel_) {
        brightnessValueLabel_->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: 700; min-width: 30px;").arg(tc.text));
    }
    if (resetToolsButton_) {
        resetToolsButton_->setStyleSheet(makeSidebarOutlineButtonStyle(tc));
    }
    if (cameraOffsetSpin_) {
        cameraOffsetSpin_->setStyleSheet(QString(
            "QSpinBox { background: %1; color: %2; border: 1px solid %3; border-radius: 5px; padding: 2px 4px; }"
            "QSpinBox:focus { border-color: %4; }").arg(tc.btnBg, tc.text, tc.border, tc.primary));
    }
    if (markDefectButton_) {
        markDefectButton_->setStyleSheet(makeSidebarPrimaryButtonStyle(tc));
    }
    if (alignButton_) {
        alignButton_->setStyleSheet(makeSidebarUtilityButtonStyle(tc));
    }
    if (resetOffsetsButton_) {
        resetOffsetsButton_->setStyleSheet(makeSidebarUtilityButtonStyle(tc));
    }
    restyleToolsEdgeTab();
}

void AnalysisView::onToolsHoverTick() {
    if (!rightToolsPanel_ || !mainArea_ || toolsLocked_) {
        return;
    }
    // The vertical TOOLS tab is hidden outside the Single Camera detail tab —
    // its visibility is the invariant that the tools panel is usable here, so
    // never reveal the panel while the handle is invisible (its geometry would
    // still match hovers over the All Camera / Diagnostic tabs).
    if (!toolsEdgeTab_ || !toolsEdgeTab_->isVisible()) {
        return;
    }
    // Unpinned: the vertical TOOLS tab is the handle — hovering it reveals the
    // panel, and it stays open while the cursor is over the tab or the panel
    // itself. Leaving both hides it again (with the fade+slide transition).
    const QPoint pos = mainArea_->mapFromGlobal(QCursor::pos());
    const QRect panelRect = toolsPanelRestingRect_;
    // Widen the tab's hit area slightly so the 2px seam between the tab and
    // the panel body doesn't hide the panel while the cursor crosses it.
    const QRect tabRect = toolsEdgeTab_
        ? toolsEdgeTab_->geometry().adjusted(-3, 0, 0, 0)
        : QRect();
    const bool overTab = tabRect.contains(pos);
    if (overTab != toolsTabHovered_) {
        toolsTabHovered_ = overTab;
        restyleToolsEdgeTab();
    }
    const bool overPanel = panelRect.contains(pos);
    // "Actually visible" means shown and not yet faded out, so moving back
    // onto the panel mid-hide reverses the transition instead of waiting.
    const bool shouldShow = overTab || (toolsPanelActuallyVisible() && overPanel);
    if (shouldShow != toolsPanelShown_) {
        toolsPanelShown_ = shouldShow;
        if (shouldShow) {
            animateToolsPanelShow();
        } else {
            animateToolsPanelHide();
        }
    }
}

bool AnalysisView::toolsPanelActuallyVisible() const {
    return rightToolsPanel_ && rightToolsPanel_->isVisible()
        && (!toolsPanelOpacity_ || toolsPanelOpacity_->opacity() > 0.01);
}

// Attach the fade effect only for the duration of a transition, so the panel
// renders natively (no compositing, no X11 artifacts) while idle or hidden.
void AnalysisView::ensureToolsPanelOpacityEffect() {
    if (!toolsPanelOpacity_) {
        toolsPanelOpacity_ = new QGraphicsOpacityEffect(rightToolsPanel_);
        rightToolsPanel_->setGraphicsEffect(toolsPanelOpacity_);
        if (toolsPanelFadeAnim_) {
            toolsPanelFadeAnim_->setTargetObject(toolsPanelOpacity_);
        }
    }
}

void AnalysisView::clearToolsPanelOpacityEffect() {
    if (rightToolsPanel_ && toolsPanelOpacity_) {
        rightToolsPanel_->setGraphicsEffect(nullptr);  // deletes the effect
        toolsPanelOpacity_ = nullptr;
        if (toolsPanelFadeAnim_) {
            toolsPanelFadeAnim_->setTargetObject(nullptr);
        }
    }
}

void AnalysisView::animateToolsPanelShow() {
    if (!rightToolsPanel_ || !toolsPanelFadeAnim_ || !toolsPanelSlideAnim_) {
        return;
    }
    // Already fully shown and idle (no effect attached): just keep it on top.
    if (rightToolsPanel_->isVisible() && !toolsPanelOpacity_) {
        rightToolsPanel_->raise();
        toolsEdgeTab_->raise();
        return;
    }
    // Reverse any in-flight hide.
    toolsPanelFadeAnim_->stop();
    toolsPanelSlideAnim_->stop();
    ensureToolsPanelOpacityEffect();

    const QRect target = toolsPanelRestingRect_;
    QRect start = rightToolsPanel_->geometry();
    if (!rightToolsPanel_->isVisible() || start.isEmpty()) {
        start = target.translated(kToolsSlidePx, 0);  // slide in from the right
        toolsPanelOpacity_->setOpacity(0.0);
    }
    toolsPanelSlideAnim_->setStartValue(start);
    toolsPanelSlideAnim_->setEndValue(target);
    toolsPanelSlideAnim_->setDuration(160);
    toolsPanelSlideAnim_->setEasingCurve(QEasingCurve::OutCubic);
    toolsPanelFadeAnim_->setStartValue(toolsPanelOpacity_->opacity());
    toolsPanelFadeAnim_->setEndValue(1.0);
    toolsPanelFadeAnim_->setDuration(160);
    toolsPanelFadeAnim_->setEasingCurve(QEasingCurve::OutCubic);

    rightToolsPanel_->show();
    rightToolsPanel_->raise();
    toolsEdgeTab_->raise();  // tab stays on top so the panel slides behind it
    toolsPanelFadeAnim_->start();
    toolsPanelSlideAnim_->start();
}

void AnalysisView::animateToolsPanelHide() {
    if (!rightToolsPanel_ || !toolsPanelFadeAnim_ || !toolsPanelSlideAnim_) {
        return;
    }
    // Reverse any in-flight show.
    toolsPanelFadeAnim_->stop();
    toolsPanelSlideAnim_->stop();
    // A freshly-created effect starts at opacity 1.0 — exactly the right
    // starting point when hiding from the idle (effect-less) shown state.
    ensureToolsPanelOpacityEffect();
    if (!rightToolsPanel_->isVisible() || toolsPanelOpacity_->opacity() <= 0.001) {
        rightToolsPanel_->hide();
        clearToolsPanelOpacityEffect();
        return;
    }

    const QRect start = rightToolsPanel_->geometry();
    const QRect target = start.translated(kToolsSlidePx, 0);  // retract toward the edge
    toolsPanelSlideAnim_->setStartValue(start);
    toolsPanelSlideAnim_->setEndValue(target);
    toolsPanelSlideAnim_->setDuration(140);
    toolsPanelSlideAnim_->setEasingCurve(QEasingCurve::InCubic);
    toolsPanelFadeAnim_->setStartValue(toolsPanelOpacity_->opacity());
    toolsPanelFadeAnim_->setEndValue(0.0);
    toolsPanelFadeAnim_->setDuration(140);
    toolsPanelFadeAnim_->setEasingCurve(QEasingCurve::InCubic);
    toolsPanelFadeAnim_->start();
    toolsPanelSlideAnim_->start();
}

void AnalysisView::onToolsPanelHideFinished() {
    if (!rightToolsPanel_) {
        return;
    }
    if (toolsPanelShown_) {
        // Show completed: drop the fade effect so the fully-opaque panel isn't
        // permanently composited (avoids the X11 opacity-artifact warnings).
        clearToolsPanelOpacityEffect();
        return;
    }
    if (toolsPanelOpacity_ && toolsPanelOpacity_->opacity() <= 0.001) {
        rightToolsPanel_->hide();
        // Snap back so the next show animates from the resting position.
        if (!toolsPanelRestingRect_.isEmpty()) {
            rightToolsPanel_->setGeometry(toolsPanelRestingRect_);
        }
        clearToolsPanelOpacityEffect();
    }
}

void AnalysisView::positionToolsPanel() {
    if (!rightToolsPanel_ || !mainArea_ || !tabWidget_) {
        return;
    }
    // Panel aligned exactly with the video frame area (below the tab bar,
    // above the playback panel). The vertical TOOLS tab sticks out of the
    // frame's right edge like a drawer handle; the panel body sits just left
    // of it so the tab stays visible as a grip. Hiding/showing never resizes
    // the cameras.
    const int margin = 6;
    const QRect tabRect = tabWidget_->geometry();
    const int tabBarH = tabWidget_->tabBar() ? tabWidget_->tabBar()->height() : 32;
    const int top = tabRect.y() + tabBarH + margin;
    const int bottom = tabRect.y() + tabRect.height() - margin;
    const int panelH = qMax(120, bottom - top);
    const int rightEdge = mainArea_->width() - margin;
    const int tabW = toolsEdgeTab_ ? toolsEdgeTab_->width() : 0;
    const int tabH = toolsEdgeTab_ ? toolsEdgeTab_->height() : 0;
    if (toolsEdgeTab_) {
        // Flush against the frame's right edge, vertically centered.
        toolsEdgeTab_->setGeometry(rightEdge - tabW,
                                   top + (panelH - tabH) / 2,
                                   tabW, tabH);
    }
    // TRACKS tab: right edge, bottom-aligned with the tab page (the event
    // dashboard lives at the bottom of the Single Camera page).
    if (tracksEdgeTab_) {
        tracksEdgeTab_->setGeometry(rightEdge - tracksEdgeTab_->width(),
                                    bottom - tracksEdgeTab_->height(),
                                    tracksEdgeTab_->width(),
                                    tracksEdgeTab_->height());
    }
    // TRACKS panel: left of its tab, bottom-aligned.
    if (tracksPanel_) {
        const int tx = rightEdge - tracksEdgeTab_->width() - 2 - tracksPanel_->width();
        const int ty = bottom - tracksPanel_->height();
        tracksPanel_->setGeometry(tx, ty, tracksPanel_->width(), tracksPanel_->height());
        tracksPanel_->raise();
    }
    if (tracksEdgeTab_) tracksEdgeTab_->raise();
    // Panel body immediately left of the tab handle (2px breathing room).
    const QRect panelRect(rightEdge - tabW - 2 - rightToolsPanel_->width(),
                          top,
                          rightToolsPanel_->width(),
                          panelH);
    toolsPanelRestingRect_ = panelRect;
    rightToolsPanel_->setGeometry(panelRect);
    // A resize while animating would fight the animation targets — stop and
    // snap to the resting state; the hover timer re-evaluates on its next tick.
    if (toolsPanelSlideAnim_ && toolsPanelSlideAnim_->state() == QAbstractAnimation::Running) {
        toolsPanelSlideAnim_->stop();
    }
    if (toolsPanelFadeAnim_ && toolsPanelFadeAnim_->state() == QAbstractAnimation::Running) {
        toolsPanelFadeAnim_->stop();
    }
    if (toolsPanelShown_) {
        rightToolsPanel_->show();
    } else {
        rightToolsPanel_->hide();
    }
    clearToolsPanelOpacityEffect();  // idle: no compositing while static
    rightToolsPanel_->raise();
    toolsEdgeTab_->raise();  // tab on top: the panel slides behind it when hiding
}

QVector<int> AnalysisView::defectMarkFrames(const QJsonValue& value) const {
    QVector<int> frames;
    if (value.isArray()) {
        const QJsonArray arr = value.toArray();
        for (const QJsonValue& item : arr) {
            if (item.isDouble()) {
                frames.append(static_cast<int>(item.toDouble()));
            }
        }
    } else if (value.isDouble()) {
        // Legacy sidecar: single frame index per camera.
        frames.append(static_cast<int>(value.toDouble()));
    }
    return frames;
}

QVector<int> AnalysisView::defectMarksForCamera(int camIndex) const {
    const auto it = defectMarks_.find(QString("cam%1").arg(camIndex + 1));
    if (it == defectMarks_.end()) {
        return QVector<int>();
    }
    return defectMarkFrames(it.value());
}

void AnalysisView::markDefectForSelectedCamera() {
    if (!isReviewMode_ || selectedCameraId_ < 0) {
        return;
    }
    const int frame = displayedFrameIndexForCamera(selectedCameraId_, currentReviewFrameIndex());
    QVector<int> frames = defectMarksForCamera(selectedCameraId_);
    if (!frames.contains(frame)) {
        frames.append(frame);
        QJsonArray arr;
        for (int f : frames) {
            arr.append(f);
        }
        defectMarks_[QString("cam%1").arg(selectedCameraId_ + 1)] = arr;
        saveEventAnnotations();
    }
    updateAnnotationSliderMarkers();
    updateAlignmentStatus();
}

bool AnalysisView::tryAlignToMarks() {
    // Collect marks for cameras present in this event only (stale sidecar marks
    // from cameras outside the event never participate).
    QMap<int, QVector<int>> marksByCam;
    for (auto it = defectMarks_.begin(); it != defectMarks_.end(); ++it) {
        const int camIndex = it.key().mid(3).toInt() - 1;
        if (camIndex < 0 || videoReaders_.count(camIndex) == 0) {
            continue;
        }
        const QVector<int> frames = defectMarkFrames(it.value());
        if (!frames.isEmpty()) {
            marksByCam[camIndex] = frames;
        }
    }
    if (marksByCam.size() < 2) {
        return false;
    }

    const int refCam = marksByCam.firstKey();
    const QVector<int>& refMarks = marksByCam[refCam];

    // Offsets live in the timeline camera's frame domain (see
    // displayedFrameIndexForCamera), so seconds/speed conversions use its fps.
    const double fps = timelineFps();
    const int sign = currentEventInfo_.positionDirectionSign >= 0 ? 1 : -1;
    const bool speedOk = (SpeedProfile::hasValidAnchors(currentEventInfo_.speedAnchors)
            || (std::isfinite(currentEventInfo_.speedValue) && currentEventInfo_.speedValue > 0.0))
        && !currentEventInfo_.speedStale;
    const int refPos = speedOk ? currentEventCameraPositionMm(refCam) : 0;

    QStringList applied;
    QStringList appliedNamed;
    QStringList fallbackCams;
    QStringList fallbackReason;
    for (auto& pair : videoReaders_) {
        const int cam = pair.first;
        if (marksByCam.contains(cam)) {
            const QVector<int>& marks = marksByCam[cam];
            const int n = qMin(refMarks.size(), marks.size());
            if (n <= 0) {
                cameraFrameOffsets_[cam] = 0;
                continue;
            }
            // Offset = mean over the k-th mark pairs of (this cam's mark minus
            // the reference cam's mark): master = mark - offset must equal the
            // reference master, so offset[cam] = marks[cam] - marks[ref].
            // Marks live in each camera's own frame domain; the offset is
            // applied in timeline frames — convert both sides via the
            // timestamp mapping (legacy events without comparable timestamps
            // keep the raw shared-index difference).
            double sum = 0.0;
            for (int k = 0; k < n; ++k) {
                const int camTl = tlIndexOfOwnFrame(cam, marks[k]);
                const int refTl = tlIndexOfOwnFrame(refCam, refMarks[k]);
                sum += (camTl >= 0 && refTl >= 0)
                    ? static_cast<double>(camTl - refTl)
                    : static_cast<double>(marks[k] - refMarks[k]);
            }
            const int offset = static_cast<int>(std::lround(sum / n));
            cameraFrameOffsets_[cam] = offset;
            if (offset != 0) {
                const QString seconds = QString("%1 s").arg(static_cast<double>(offset) / fps, 0, 'f', 3);
                applied << QString("%1 %2 f (%3)").arg(cam + 1).arg(offset).arg(seconds);
                appliedNamed << QString("cam%1 %2 f (%3)").arg(cam + 1).arg(offset).arg(seconds);
            }
        } else {
            // Unmarked camera: fall back to the speed/position formula.
            if (speedOk && refPos > 0) {
                const int pos = currentEventCameraPositionMm(cam);
                if (pos <= 0) {
                    cameraFrameOffsets_[cam] = 0;
                    fallbackCams << QString("cam%1").arg(cam + 1);
                    fallbackReason << QString("cam%1 no position").arg(cam + 1);
                    continue;
                }
                // Local speed at this camera (interpolated between the recorded
                // speed anchors), so the draw between drive groups is reflected.
                const double localSpeed = SpeedProfile::speedAt(
                    pos, currentEventInfo_.speedAnchors, currentEventInfo_.speedValue);
                const double framesPerMm = fps * 60.0 / (localSpeed * 1000.0);
                const int deltaMm = (pos - refPos) * sign;
                const int offset = static_cast<int>(std::lround(deltaMm * framesPerMm));
                cameraFrameOffsets_[cam] = offset;
                fallbackCams << QString("cam%1").arg(cam + 1);
                if (offset != 0) {
                    applied << QString("%1 %2 f").arg(cam + 1).arg(offset);
                }
            } else {
                cameraFrameOffsets_[cam] = 0;
                fallbackCams << QString("cam%1").arg(cam + 1);
                fallbackReason << QString("cam%1 no speed").arg(cam + 1);
            }
        }
    }

    saveEventAnnotations();
    if (cameraOffsetSpin_ && selectedCameraId_ >= 0) {
        cameraOffsetSpin_->blockSignals(true);
        cameraOffsetSpin_->setValue(cameraFrameOffsets_[selectedCameraId_]);
        cameraOffsetSpin_->blockSignals(false);
    }
    renderCurrentReviewFrame(false);
    updateAlignmentStatus();
    if (alignStatusLabel_) {
        const QString head = QString("Aligned to marks (ref cam%1)").arg(refCam + 1);
        const QString detail = applied.isEmpty()
            ? QString("cameras already in sync")
            : applied.join(", ");
        if (fallbackCams.isEmpty()) {
            alignStatusLabel_->setText(QString("%1: %2").arg(head, detail));
            alignStatusLabel_->setToolTip(QString("%1: %2 (marked cameras; no speed needed)").arg(head, appliedNamed.isEmpty()
                ? QString("cameras already in sync") : appliedNamed.join(", ")));
        } else {
            alignStatusLabel_->setText(QString("%1: %2 · speed fallback: %3")
                .arg(head, detail, fallbackCams.join(", ")));
            alignStatusLabel_->setToolTip(QString("%1: %2 · speed fallback for %3")
                .arg(head, detail, fallbackReason.join(", ")));
        }
    }
    return true;
}

void AnalysisView::applyCameraAlignment() {
    if (!isReviewMode_ || videoReaders_.empty()) {
        return;
    }

    // Placed defect marks are the ground truth of the sync: prefer them whenever
    // at least two cameras in this event carry marks.
    if (tryAlignToMarks()) {
        return;
    }

    // Fallback: machine speed comes from the OPC UA speed tags captured with
    // the event (EventInfo.speedValue + speedAnchors). No manual override.
    const bool speedOk = SpeedProfile::hasValidAnchors(currentEventInfo_.speedAnchors)
        || (std::isfinite(currentEventInfo_.speedValue) && currentEventInfo_.speedValue > 0.0);
    if (!speedOk) {
        if (alignStatusLabel_) alignStatusLabel_->setText("No Machine Speed captured for this event — mark defects on >= 2 cameras, then Align");
        return;
    }
    if (currentEventInfo_.speedStale) {
        if (alignStatusLabel_) alignStatusLabel_->setText("Machine Speed was stale at capture — mark defects on >= 2 cameras, then Align");
        return;
    }
    const QString speedUnit = currentEventInfo_.speedUnit.isEmpty()
        ? QStringLiteral("m/min") : currentEventInfo_.speedUnit;

    // Offsets live in the timeline camera's frame domain, so framesPerMm
    // must use its fps (identical to the first reader's for uniform-fps
    // legacy events, correct for mixed-fps ones).
    const double fps = timelineFps();

    const int sign = currentEventInfo_.positionDirectionSign >= 0 ? 1 : -1;

    const int refCam = videoReaders_.begin()->first;
    const int refPos = currentEventCameraPositionMm(refCam);
    if (refPos <= 0) {
        if (alignStatusLabel_) alignStatusLabel_->setText("Set camera positions in Camera config, then Align");
        return;
    }
    const double refLocalSpeed = SpeedProfile::speedAt(
        refPos, currentEventInfo_.speedAnchors, currentEventInfo_.speedValue);

    QStringList applied;
    QStringList appliedNamed;
    for (auto& pair : videoReaders_) {
        const int pos = currentEventCameraPositionMm(pair.first);
        if (pos <= 0) {
            cameraFrameOffsets_[pair.first] = 0;
            continue;
        }
        // Local speed at this camera (interpolated between the recorded speed
        // anchors), so the draw between drive groups is reflected in the offset.
        const double localSpeed = SpeedProfile::speedAt(
            pos, currentEventInfo_.speedAnchors, currentEventInfo_.speedValue);
        const double framesPerMm = fps * 60.0 / (localSpeed * 1000.0);
        const int deltaMm = (pos - refPos) * sign;
        const int offset = static_cast<int>(std::lround(deltaMm * framesPerMm));
        cameraFrameOffsets_[pair.first] = offset;
        if (offset != 0) {
            const QString seconds = QString("%1 s").arg(static_cast<double>(offset) / fps, 0, 'f', 3);
            applied << QString("%1 %2 f (%3)").arg(pair.first + 1).arg(offset).arg(seconds);
            appliedNamed << QString("cam%1 %2 f (%3)").arg(pair.first + 1).arg(offset).arg(seconds);
        }
    }

    saveEventAnnotations();
    if (cameraOffsetSpin_ && selectedCameraId_ >= 0) {
        cameraOffsetSpin_->blockSignals(true);
        cameraOffsetSpin_->setValue(cameraFrameOffsets_[selectedCameraId_]);
        cameraOffsetSpin_->blockSignals(false);
    }
    renderCurrentReviewFrame(false);
    updateAlignmentStatus();
    if (alignStatusLabel_) {
        const QString speedText = QString("%1 %2").arg(refLocalSpeed, 0, 'f', 1).arg(speedUnit);
        if (applied.isEmpty()) {
            alignStatusLabel_->setText(QString("Aligned @ %1 — no offset needed (cameras already aligned)").arg(speedText));
            alignStatusLabel_->setToolTip(QString("Aligned at %1 — no camera needed a shift.").arg(speedText));
        } else {
            alignStatusLabel_->setText(QString("Aligned @ %1: %2").arg(speedText, applied.join(", ")));
            alignStatusLabel_->setToolTip(QString("Aligned at %1: %2").arg(speedText, appliedNamed.join(", ")));
        }
    }
}

void AnalysisView::clearCameraOffsets() {
    std::fill(cameraFrameOffsets_.begin(), cameraFrameOffsets_.end(), 0);
    saveEventAnnotations();
    renderCurrentReviewFrame(false);
    updateAlignmentStatus();
    if (cameraOffsetSpin_ && selectedCameraId_ >= 0) {
        cameraOffsetSpin_->blockSignals(true);
        cameraOffsetSpin_->setValue(0);
        cameraOffsetSpin_->blockSignals(false);
    }
}

void AnalysisView::updateAlignmentStatus() {
    if (!alignStatusLabel_) {
        return;
    }
    if (!isReviewMode_) {
        alignStatusLabel_->setText("—");
        syncedMasterFrames_.clear();
        applySyncIndicators();
        return;
    }
    const int numReaders = static_cast<int>(videoReaders_.size());

    // Alignment verdict: when the k-th marked defect of every marked camera
    // displays at the same master frame (mark - offset), all cameras are in sync.
    // Only marks from cameras present in this event count (stale sidecar marks
    // for cameras outside the event are ignored).
    int markedCamCount = 0;
    QMap<int, QVector<int>> markMasterFrames;  // camera index -> master frames
    for (auto it = defectMarks_.begin(); it != defectMarks_.end(); ++it) {
        const int camIndex = it.key().mid(3).toInt() - 1;
        if (camIndex < 0 || videoReaders_.count(camIndex) == 0) {
            continue;
        }
        const QVector<int> frames = defectMarkFrames(it.value());
        if (frames.isEmpty()) {
            continue;
        }
        ++markedCamCount;
        QVector<int> masters;
        masters.reserve(frames.size());
        const int offset = (camIndex >= 0 && camIndex < static_cast<int>(cameraFrameOffsets_.size()))
            ? cameraFrameOffsets_[camIndex] : 0;
        for (int f : frames) {
            // Master = where this camera shows the mark, in the same frame
            // domain as the scrub bar: timeline index for mixed-fps events
            // (same mapping as the display path), raw shared index for legacy.
            const int tl = tlIndexOfOwnFrame(camIndex, f);
            masters.append(tl >= 0 ? tl - offset : f - offset);
        }
        markMasterFrames[camIndex] = masters;
    }

    QString text = QString("Marks: %1/%2 cameras — mark the same defect on >= 2 cameras, then Align")
        .arg(markedCamCount).arg(numReaders);
    QString named = text;

    syncedMasterFrames_.clear();
    if (markMasterFrames.size() >= 2) {
        // Compare marks pairwise (k-th mark of every camera). A camera with fewer
        // marks simply doesn't participate in the k-th comparison.
        int maxMarks = 0;
        for (auto it = markMasterFrames.constBegin(); it != markMasterFrames.constEnd(); ++it) {
            maxMarks = qMax(maxMarks, it.value().size());
        }
        QVector<int> synced;
        QStringList disagreeNotes;
        int firstMarkMin = 0, firstMarkMax = 0;
        bool haveFirst = false;
        for (int k = 0; k < maxMarks; ++k) {
            int value = -1;
            bool agree = true;
            int kMin = 0, kMax = 0;
            bool haveK = false;
            for (auto it = markMasterFrames.constBegin(); it != markMasterFrames.constEnd(); ++it) {
                if (k >= it.value().size()) {
                    continue;  // this camera has no k-th mark — not a disagreement
                }
                const int cur = it.value().at(k);
                if (value < 0) {
                    value = cur;
                    kMin = kMax = cur;
                    haveK = true;
                    if (k == 0) {
                        firstMarkMin = firstMarkMax = cur;
                        haveFirst = true;
                    }
                } else if (cur != value) {
                    agree = false;
                    kMin = qMin(kMin, cur);
                    kMax = qMax(kMax, cur);
                    if (k == 0) {
                        firstMarkMin = qMin(firstMarkMin, cur);
                        firstMarkMax = qMax(firstMarkMax, cur);
                    }
                }
            }
            if (agree && value >= 0) {
                synced.append(value);
            } else if (haveK) {
                disagreeNotes << QString("mark %1 off by %2 f").arg(k + 1).arg(kMax - kMin);
            }
        }
        syncedMasterFrames_ = synced;

        if (!synced.isEmpty()) {
            QStringList framesText;
            QStringList framesTextNamed;
            for (int f : synced) {
                const double seconds = relativeSecondsForFrameIndex(f);
                framesText << QString("%1 (%2%3 s)")
                    .arg(f).arg(seconds >= 0.0 ? "+" : "").arg(seconds, 0, 'f', 3);
                framesTextNamed << QString("frame %1 (%2%3 s)")
                    .arg(f).arg(seconds >= 0.0 ? "+" : "").arg(seconds, 0, 'f', 3);
            }
            text += QString(" · all defects @ %1").arg(framesText.join(", "));
            named += QString(" · all defects @ %1").arg(framesTextNamed.join(", "));
            if (!disagreeNotes.isEmpty()) {
                text += QString(" · %1 — re-check marks").arg(disagreeNotes.join(", "));
                named += QString(" · %1 — re-check marks").arg(disagreeNotes.join(", "));
            }
        } else if (haveFirst) {
            text += QString(" · first marks differ by %1 f — re-check marks").arg(firstMarkMax - firstMarkMin);
            named += QString(" · first marks differ by %1 frames — re-check marks").arg(firstMarkMax - firstMarkMin);
        }
    }
    alignStatusLabel_->setText(text);
    alignStatusLabel_->setToolTip(named);
    applySyncIndicators();
}

void AnalysisView::applySyncIndicators() {
    // Show the crosshair only while viewing a master frame where every marked
    // camera's defect is confirmed to line up.
    const bool show = isReviewMode_ && !syncedMasterFrames_.isEmpty()
        && syncedMasterFrames_.contains(currentReviewFrameIndex());
    const QString label = show
        ? QString("SYNC @ frame %1").arg(currentReviewFrameIndex())
        : QString();

    for (auto& pair : videoReaders_) {
        const int cam = pair.first;
        if (cam >= 0 && cam < static_cast<int>(cameraWidgets_.size()) && cameraWidgets_[cam]) {
            cameraWidgets_[cam]->setSyncIndicator(show, label, syncIndicatorPosForCamera(cam));
        }
    }
    if (selectedCameraWidget_) {
        const int cam = selectedCameraWidget_->getCameraId();
        if (cam >= 0) {
            selectedCameraWidget_->setSyncIndicator(show, label, syncIndicatorPosForCamera(cam));
        }
    }
}

QPointF AnalysisView::syncIndicatorPosForCamera(int camIndex) const {
    // Anchor the crosshair on the drawn annotation of this camera's first marked
    // frame (the defect spot); fall back to frame center.
    const QVector<int> marks = defectMarksForCamera(camIndex);
    if (!marks.isEmpty()) {
        const QString key = annotationKey(camIndex, marks.first());
        if (eventAnnotations_.contains(key) && eventAnnotations_[key].isObject()) {
            const QJsonObject ann = eventAnnotations_[key].toObject();
            const QJsonArray pts = ann["points"].toArray();
            double sx = 0.0, sy = 0.0;
            int n = 0;
            for (const QJsonValue& val : pts) {
                const QJsonObject p = val.toObject();
                if (p.contains("nx") && p.contains("ny")) {
                    sx += p["nx"].toDouble();
                    sy += p["ny"].toDouble();
                    ++n;
                }
            }
            if (n > 0) {
                return QPointF(sx / n, sy / n);
            }
        }
    }
    return QPointF(0.5, 0.5);
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

// Digits (main row or numpad) plus Up/Down are the camera-selection keys.
// Shift/Alt/Ctrl variants are left alone (they are text input elsewhere).
static bool isCameraNavigationKey(int key) {
    return (key >= Qt::Key_0 && key <= Qt::Key_9)
        || key == Qt::Key_Up || key == Qt::Key_Down;
}

bool AnalysisView::handlePlayerCameraKey(int key) {
    // Never switch cameras while the TOOLS layer panel is open: the user may
    // be about to type digits / arrows into its controls, so the keys must
    // stay available to the panel instead of being grabbed by the view.
    if (rightToolsPanel_ && rightToolsPanel_->isVisible()) {
        return false;
    }
    // Digits buffer for the entry delay so cameras beyond id 9 can be reached
    // by typing two digits quickly ("1", "2" → camera 12). While digits are
    // pending, 0 extends the number; an idle 0 alone returns to the grid.
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        if (key == Qt::Key_0 && cameraKeyBuffer_.isEmpty()) {
            tabWidget_->setCurrentIndex(0);  // All Cameras grid
            return true;
        }
        handleCameraDigit(key - Qt::Key_0);
        return true;
    }
    if (key == Qt::Key_Down || key == Qt::Key_Up) {
        // Immediate navigation cancels any half-typed camera number.
        cancelCameraKeyEntry();
        if (numCameras_ <= 0) {
            return false;
        }
        // Next camera (Down): start at camera 1 when nothing is selected yet.
        // Previous camera (Up): start at the last camera when nothing selected.
        const int target = (key == Qt::Key_Down)
            ? qBound(0, (selectedCameraId_ >= 0 ? selectedCameraId_ : -1) + 1, numCameras_ - 1)
            : qBound(0, (selectedCameraId_ >= 0 ? selectedCameraId_ : numCameras_) - 1,
                     numCameras_ - 1);
        onCameraClicked(target);
        return true;
    }
    return false;
}

void AnalysisView::handleCameraDigit(int digit) {
    // Two digits are enough for any camera count this system supports.
    if (cameraKeyBuffer_.length() >= 2) {
        cameraKeyBuffer_.clear();
    }
    cameraKeyBuffer_ += QString::number(digit);
    // Restart the clock on every digit so "1 2" reads as 12, not 1 then 2.
    if (cameraKeyTimer_) {
        cameraKeyTimer_->start();
    }
    refreshCameraKeyPreview();
}

void AnalysisView::resolveCameraKeyEntry() {
    const int id = cameraKeyBuffer_.toInt();
    cameraKeyBuffer_.clear();
    if (cameraKeyTimer_) {
        cameraKeyTimer_->stop();
    }
    if (id >= 1 && id <= numCameras_) {
        if (cameraKeyBanner_) {
            cameraKeyBanner_->hide();
        }
        onCameraClicked(id - 1);  // visible id is index + 1
        return;
    }
    // Unknown camera: flash a short warning instead of switching.
    if (cameraKeyBanner_) {
        const ThemeColors tc = CameraConfig::getThemeColors();
        const QString accent = QColor("#ff6b6b").name();
        cameraKeyBanner_->setStyleSheet(QString(
            "QLabel { background-color: rgba(30, 16, 18, 235); color: %1;"
            " border: 1px solid %2; border-radius: 8px; padding: 6px 16px;"
            " font-size: 14px; font-weight: 700; }").arg(accent, accent));
        cameraKeyBanner_->setText(QString("No camera %1").arg(id));
        cameraKeyBanner_->adjustSize();
        positionCameraKeyBanner();
        cameraKeyBanner_->show();
        cameraKeyBanner_->raise();
        QPointer<QLabel> banner = cameraKeyBanner_;
        QTimer::singleShot(1000, this, [banner]() {
            if (banner) {
                banner->hide();
            }
        });
        Q_UNUSED(tc);
    }
}

void AnalysisView::cancelCameraKeyEntry() {
    cameraKeyBuffer_.clear();
    if (cameraKeyTimer_) {
        cameraKeyTimer_->stop();
    }
    if (cameraKeyBanner_) {
        cameraKeyBanner_->hide();
    }
}

void AnalysisView::refreshCameraKeyPreview() {
    if (!cameraKeyBanner_) {
        cameraKeyBanner_ = new QLabel(this);
        // Pure overlay: never intercepts mouse events meant for the video.
        cameraKeyBanner_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        cameraKeyBanner_->hide();
    }
    const int id = cameraKeyBuffer_.isEmpty() ? 0 : cameraKeyBuffer_.toInt();
    const bool valid = id >= 1 && id <= numCameras_;
    const ThemeColors tc = CameraConfig::getThemeColors();
    const QColor accentColor = valid ? QColor(tc.primary)
                                     : QColor("#8a8a8a");
    cameraKeyBanner_->setStyleSheet(QString(
        "QLabel { background-color: rgba(16, 16, 22, 225); color: %1;"
        " border: 1px solid %2; border-radius: 8px; padding: 6px 16px;"
        " font-size: 15px; font-weight: 700; }").arg(
            accentColor.lighter(175).name(), accentColor.name()));
    // Fast preview of id + name so the user knows the target before it opens.
    cameraKeyBanner_->setText(valid
        ? QString("Camera %1 · %2").arg(id).arg(currentEventCameraLabel(id - 1))
        : QString("Camera %1").arg(id));
    cameraKeyBanner_->adjustSize();
    positionCameraKeyBanner();
    cameraKeyBanner_->show();
    cameraKeyBanner_->raise();
}

void AnalysisView::positionCameraKeyBanner() {
    if (!cameraKeyBanner_ || !mainArea_) {
        return;
    }
    // Top-center over the video area (just below the tab bar), in view coords.
    const QPoint anchor = mainArea_->mapTo(this, QPoint(mainArea_->width() / 2, 70));
    int x = anchor.x() - cameraKeyBanner_->width() / 2;
    x = qBound(8, x, width() - cameraKeyBanner_->width() - 8);
    cameraKeyBanner_->move(x, qBound(8, anchor.y(), height() - cameraKeyBanner_->height() - 8));
}

bool AnalysisView::eventFilter(QObject* watched, QEvent* event) {
    // TRACKS hover tab/panel: enter reveals the panel, leaving both (with a
    // short grace period for the gap crossing) hides it again.
    if (watched == tracksEdgeTab_ || watched == tracksPanel_) {
        if (event->type() == QEvent::Enter) {
            tracksPanel_->show();
            tracksPanel_->raise();
            tracksEdgeTab_->raise();
            if (!tracksTabHovered_) {
                tracksTabHovered_ = true;
                restyleTracksEdgeTab();
            }
            return false;
        }
        if (event->type() == QEvent::Leave) {
            QPointer<QWidget> panel = tracksPanel_;
            QPointer<QWidget> tab = tracksEdgeTab_;
            QTimer::singleShot(250, this, [this, panel, tab]() {
                if (!panel || panel->underMouse()
                    || (tab && tab->underMouse())) {
                    return;
                }
                panel->hide();
                tracksTabHovered_ = false;
                restyleTracksEdgeTab();
            });
            return false;
        }
    }
    if (event->type() == QEvent::MouseButtonPress) {
        QWidget* widget = qobject_cast<QWidget*>(watched);
        if (widget && widget->property("annotationFrame").isValid()) {
            const int frameIndex = widget->property("annotationFrame").toInt();
            seekToRelativeFrame(frameIndex - triggerFrameIndex_);
            return true;
        }
    }
    // Media-player keys on the scrub slider: after scrubbing, the slider keeps
    // keyboard focus and would swallow Left/Right (nudging its own value by a
    // single unit) and let Space fall through unused. Route them to the same
    // handlers as AnalysisView::keyPressEvent so stepping and play/pause work
    // no matter which control the user last clicked. setStepButtonDown() mirrors
    // the toolbar button's visual state for keyboard-driven stepping.
    if (watched == playbackSlider_ && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Left) {
            cancelCameraKeyEntry();  // a half-typed camera number is aborted
            if (prevButton_) prevButton_->setDown(true);
            onPreviousPressed();
            onPreviousReleased();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Right) {
            cancelCameraKeyEntry();
            if (nextButton_) nextButton_->setDown(true);
            onNextPressed();
            onNextReleased();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Space) {
            cancelCameraKeyEntry();
            onPlayPauseClicked();
            return true;
        }
        // Camera-selection keys: QSlider would swallow Up/Down for its own
        // 1-unit nudges (digits already bubble to the view), so route them
        // through the same handler as keyPressEvent(). Repeats are consumed
        // without re-selecting so holding a key can't rebuild the camera tab.
        if (isCameraNavigationKey(keyEvent->key())
            && (keyEvent->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
            if (!keyEvent->isAutoRepeat()) {
                handlePlayerCameraKey(keyEvent->key());
            }
            return true;
        }
    }
    // Clear the keyboard-pressed visuals when the key is released over the
    // slider (its KeyRelease would otherwise vanish inside the slider).
    if (watched == playbackSlider_ && event->type() == QEvent::KeyRelease) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Left && prevButton_) {
            prevButton_->setDown(false);
            return true;
        }
        if (keyEvent->key() == Qt::Key_Right && nextButton_) {
            nextButton_->setDown(false);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void AnalysisView::keyPressEvent(QKeyEvent* event) {
    // Media-player keyboard control: Left/Right step one frame (or more at
    // higher speeds), Space toggles play/pause. Works whenever focus reaches
    // this widget; children that consume keys themselves (e.g. the scrub
    // slider) are handled in eventFilter().
    if (event->key() == Qt::Key_Left) {
        cancelCameraKeyEntry();  // a half-typed camera number is aborted
        // Mirror the pressed look of the Step Back button while the key is
        // held (repeated KeyPress events keep it down; keyReleaseEvent clears
        // it), so keyboard stepping gives the same feedback as Space on Play.
        if (prevButton_) prevButton_->setDown(true);
        onPreviousPressed();
        onPreviousReleased(); // Simulate single step
        event->accept();
    } else if (event->key() == Qt::Key_Right) {
        cancelCameraKeyEntry();
        if (nextButton_) nextButton_->setDown(true);
        onNextPressed();
        onNextReleased(); // Simulate single step
        event->accept();
    } else if (event->key() == Qt::Key_Space) {
        cancelCameraKeyEntry();
        onPlayPauseClicked();
        event->accept();
    } else if (isCameraNavigationKey(event->key())
               && (event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
        // Camera selection: 1..N opens that camera by its visible id, 0 goes
        // back to the All Cameras grid, Up/Down move to the previous/next
        // camera (numpad keys allowed). Ignored (but still consumed) on key
        // auto-repeat so holding a key doesn't rebuild the camera tab
        // repeatedly, and disabled while the TOOLS panel is open (see
        // handlePlayerCameraKey). Shift/Alt/Ctrl combos are left alone.
        if (!event->isAutoRepeat()) {
            handlePlayerCameraKey(event->key());
        }
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void AnalysisView::keyReleaseEvent(QKeyEvent* event) {
    // Release the keyboard-driven pressed visuals when the user lets go of the
    // step key (arrives here when focus is on this view or via parent-chain
    // propagation from child widgets that don't consume the arrow keys).
    if (event->key() == Qt::Key_Left) {
        if (prevButton_) prevButton_->setDown(false);
        event->accept();
    } else if (event->key() == Qt::Key_Right) {
        if (nextButton_) nextButton_->setDown(false);
        event->accept();
    } else {
        QWidget::keyReleaseEvent(event);
    }
}

void AnalysisView::setAdminMode(bool isAdmin) {
    adminMode_ = isAdmin;
    if (adminButton_) {
        const ThemeColors tc = CameraConfig::getThemeColors();
        adminButton_->setText(isAdmin ? "Logout" : "Login");
        adminButton_->setToolTip(isAdmin ? "Logout Administrator" : "Admin Login");
        adminButton_->setStyleSheet(isAdmin ? makeSidebarOutlineButtonStyle(tc, true)
                                            : makeSidebarPrimaryButtonStyle(tc));
    }
    if (enableDeleteCheck_) {
        enableDeleteCheck_->setEnabled(isAdmin);
        if (!isAdmin) {
            enableDeleteCheck_->setChecked(false);
        }
    }

    // Instant Clear is destructive — admin only, and requires delete mode to
    // be enabled so it can never be pressed accidentally by an operator.
    if (instantClearButton_) {
        instantClearButton_->setEnabled(isAdmin && enableDeleteCheck_
            ? enableDeleteCheck_->isChecked()
            : false);
    }
    if (instantClearKeepSpin_) {
        instantClearKeepSpin_->setEnabled(isAdmin && enableDeleteCheck_
            ? enableDeleteCheck_->isChecked()
            : false);
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
    auto* table = new QTableWidget(0, 4, parent);
    table->setHorizontalHeaderLabels({"Trigger Time", "Reason", "Group", "Defect Frame"});
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
    // All four columns are always present; the default scroll position shows
    // exactly Trigger Time + Reason, and the horizontal scrollbar reveals
    // Group + Defect Frame without widening the 304px sidebar. Trigger Time is
    // fixed wide enough for a full yyyy/MM/dd HH:mm:ss timestamp (the newest
    // event is marked by a row tint, not an icon); the Reason column is sized
    // by updateLogTableReasonWidths() to fill the remaining visible width
    // (proportional), so the pair always spans the whole viewport and the
    // following columns start beyond its right edge, even on an empty table.
    // (Plain Stretch is avoided: it would collapse to zero when the fixed
    // siblings alone overflow the viewport.)
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    table->horizontalHeader()->setStretchLastSection(false);
    table->setColumnWidth(0, 138);
    table->setColumnWidth(1, 110);
    table->setColumnWidth(2, 115);
    table->setColumnWidth(3, 100);
    table->horizontalScrollBar()->setValue(0);
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
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setStyleSheet(makeTableStyle(CameraConfig::getThemeColors(), deleteMode));
}

void AnalysisView::updateLogTableReasonWidths() {
    const std::vector<QTableWidget*> tables = {paperBreakTable_, permanentPaperBreakTable_};
    for (QTableWidget* table : tables) {
        if (!table) {
            continue;
        }
        const int viewportWidth = table->viewport()->width();
        if (viewportWidth <= 0) {
            continue; // Not laid out yet (e.g. hidden permanent table).
        }
        const int reasonWidth = std::max(80, viewportWidth - table->columnWidth(0));
        table->setColumnWidth(1, reasonWidth);
    }
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
        const QString ts = timeItem->data(Qt::UserRole).toString();
        int group = CameraGroup::kUnassigned;
        int defectFrame = -1;
        if (QTableWidgetItem* groupItem = sourceTable->item(row, 2)) {
            const QVariant groupData = groupItem->data(Qt::UserRole + 1);
            if (groupData.isValid()) {
                group = groupData.toInt();
            }
        }
        if (QTableWidgetItem* frameItem = sourceTable->item(row, 3)) {
            const QVariant frameData = frameItem->data(Qt::UserRole + 1);
            if (frameData.isValid()) {
                defectFrame = frameData.toInt();
            }
        }
        if (group == CameraGroup::kUnassigned && defectFrame < 0) {
            // Legacy row (or event without metadata): recover from the database.
            try {
                const EventDatabase::EventInfo info = EventDatabase::instance().getEventInfo(ts);
                group = info.triggerGroup;
                defectFrame = info.triggerIndex;
            } catch (...) {}
        }
        addEventRow(ts, reasonItem->text(), permanent, false, group, defectFrame);
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
        "RAM Frames", "RAM [MB]", "Drops", "Drops/s", "Stream Health", "Link"
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
    diagTable_->setColumnWidth(12, 130); // Drops/s (rate + trend sparkline)
    diagTable_->setColumnWidth(13, 140); // Stream Health
    diagTable_->setColumnWidth(14, 90);  // Link
    diagTable_->horizontalHeader()->setSectionResizeMode(13, QHeaderView::Stretch); // Stream Health

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
    // Helper: clear the Drops/s trend history for a row (used when a camera
    // is disabled or offline so a stale sparkline never reappears).
    auto clearDropHistory = [this](int row) {
        if (static_cast<size_t>(row) >= diagDropRateHistory_.size())
            diagDropRateHistory_.resize(row + 1);
        diagDropRateHistory_[row].clear();
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
            for (int col = 2; col <= 14; ++col)
                diagTable_->setItem(row, col, makeNA());
            applyRowColors(row, QColor(55, 55, 55, 140), QColor(tc.border));
            clearDropHistory(row);
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

        // Col 12: Drops/s — rate of incomplete grabs since the last refresh
        // (~3s). Tracks the delta of the cumulative counter, so a healthy
        // stream reads 0.0 while a failing link climbs quickly.
        double dropsPerSec = 0.0;
        {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (cameraManager_ && isConnected) {
                if (static_cast<size_t>(row) < diagPrevDropCount_.size()
                    && diagPrevDropSampleMs_[row] > 0) {
                    const uint64_t prev = diagPrevDropCount_[row];
                    const qint64 elapsedMs = nowMs - diagPrevDropSampleMs_[row];
                    if (elapsedMs > 0 && dropCount >= prev) {
                        dropsPerSec = static_cast<double>(dropCount - prev)
                                    * 1000.0 / static_cast<double>(elapsedMs);
                    }
                }
                if (static_cast<size_t>(row) >= diagPrevDropCount_.size()) {
                    diagPrevDropCount_.resize(row + 1, 0);
                    diagPrevDropSampleMs_.resize(row + 1, 0);
                }
                diagPrevDropCount_[row] = dropCount;
                diagPrevDropSampleMs_[row] = nowMs;
            } else {
                // Camera offline: reset the sample so the rate starts clean
                // when it reconnects.
                if (static_cast<size_t>(row) >= diagPrevDropCount_.size()) {
                    diagPrevDropCount_.resize(row + 1, 0);
                    diagPrevDropSampleMs_.resize(row + 1, 0);
                }
                diagPrevDropCount_[row] = 0;
                diagPrevDropSampleMs_[row] = 0;
            }
            QTableWidgetItem* rateItem = nullptr;
            if (cameraManager_ && isConnected) {
                // Append this sample to the trend history (capped at N samples).
                if (static_cast<size_t>(row) >= diagDropRateHistory_.size())
                    diagDropRateHistory_.resize(row + 1);
                std::vector<double>& history = diagDropRateHistory_[row];
                history.push_back(dropsPerSec);
                if (static_cast<int>(history.size()) > kDiagDropRateHistoryMax)
                    history.erase(history.begin());

                rateItem = makeItem(dropsPerSec > 0.0
                    ? QString::number(dropsPerSec, 'f', 1)
                    : QString("0.0"));
                rateItem->setToolTip("Incomplete grabs per second since the last refresh (~3s). "
                                     "0.0 = healthy. Rising values = bandwidth/packet-loss trend. "
                                     "The mini chart shows the last "
                                     + QString::number(kDiagDropRateHistoryMax) + " samples.");
                QColor sparkColor("#4CAF50");
                if (dropsPerSec >= 10.0) {
                    rateItem->setBackground(QColor(0xFF, 0x40, 0x40, 180));
                    rateItem->setForeground(QColor("#FFFFFF"));
                    sparkColor = QColor("#FF4040");
                } else if (dropsPerSec > 0.0) {
                    rateItem->setBackground(QColor(0xFF, 0xAA, 0x00, 180));
                    rateItem->setForeground(QColor("#1A1A1A"));
                    sparkColor = QColor("#FFAA00");
                } else {
                    rateItem->setForeground(QColor("#4CAF50"));
                }
                rateItem->setIcon(QIcon(makeSparklinePixmap(history, sparkColor)));
            } else {
                clearDropHistory(row);
                rateItem = makeNA();
            }
            diagTable_->setItem(row, 12, rateItem);
        }

        // Col 14: Link speed (Mbps) of the NIC carrying this camera's subnet.
        const int linkMbps = (cameraManager_ && isConnected)
            ? cameraManager_->getCameraLinkSpeedMbps(configIndex)
            : -1;
        {
            QTableWidgetItem* linkItem = nullptr;
            if (linkMbps <= 0) {
                linkItem = makeNA();
            } else {
                const QString linkText = (linkMbps >= 1000)
                    ? QString::number(linkMbps / 1000.0, 'f', linkMbps % 1000 == 0 ? 0 : 1) + " Gb/s"
                    : QString::number(linkMbps) + " Mb/s";
                linkItem = makeItem(linkText);
                linkItem->setToolTip(QString(
                    "Negotiated NIC link speed for this camera's subnet: %1 Mb/s. "
                    "A 100 Mb/s link cannot carry a 50 fps 780x580 Mono8 stream "
                    "(~181 Mb/s) and causes incomplete grabs / 0 frames.")
                    .arg(linkMbps));
                if (linkMbps < 1000) {
                    linkItem->setBackground(QColor(0xFF, 0xAA, 0x00, 180));   // slow link
                    linkItem->setForeground(QColor("#1A1A1A"));
                } else {
                    linkItem->setForeground(QColor("#4CAF50"));
                }
            }
            diagTable_->setItem(row, 14, linkItem);
        }

        // Slow link (<1 Gb/s) is itself a warning even without drops yet.
        const bool slowLink = linkMbps > 0 && linkMbps < 1000;
        const QString healthText = (cameraManager_ && isConnected)
            ? (consecutiveDrops > 0 || slowLink
                   ? QString("WARNING")
                   : (dropCount > 0 ? QString("Recovered") : QString("OK")))
            : QString("Offline");
        diagTable_->setItem(row, 13, (cameraManager_ && isConnected)
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

        QTableWidgetItem* healthItem = diagTable_->item(row, 13);
        if (healthItem) {
            healthItem->setToolTip("OK: no incomplete grabs. Recovered: drops occurred earlier. WARNING: ongoing incomplete grabs or a slow NIC link (<1 Gb/s) - usually bandwidth/packet timing/cable or switch port issue.");
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
