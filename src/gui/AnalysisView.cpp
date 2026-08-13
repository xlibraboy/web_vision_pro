#include "AnalysisView.h"
#include <cmath>
#include <QKeyEvent>
#include <QMouseEvent>
#include "widgets/AnalysisVideoWidget.h"
#include "../config/CameraConfig.h"
#include "../core/EventController.h"
#include "../core/EventDatabase.h"
#include "../core/EventDatabase.h"
#include "../core/VideoStreamReader.h"
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
    
    // Get video properties from first available reader
    totalFrames_ = 0;
    if (!videoReaders_.empty()) {
        totalFrames_ = videoReaders_.begin()->second->getTotalFrames() - 1;
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

    resetToolsButton_ = new QPushButton("Reset", detailToolsWidget_);
    resetToolsButton_->setToolTip("Reset marker, zoom, and brightness for the detail image.");

    cameraOffsetSpin_ = new QSpinBox(detailToolsWidget_);
    cameraOffsetSpin_->setRange(-5000, 5000);
    cameraOffsetSpin_->setSuffix(" f");
    cameraOffsetSpin_->setValue(0);
    cameraOffsetSpin_->setToolTip("Per-camera frame offset on top of the shared timeline (negative = earlier frames).");

    markDefectButton_ = new QPushButton("Mark Defect", detailToolsWidget_);
    markDefectButton_->setToolTip("Record this camera's currently displayed frame as its defect mark.");

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
        // Palette/font, never a local stylesheet (see setupPlaybackControls).
        QPalette infoPal = playbackInfoLabel_->palette();
        infoPal.setColor(QPalette::WindowText, QColor(tc.text));
        playbackInfoLabel_->setPalette(infoPal);
        QFont infoFont = playbackInfoLabel_->font();
        infoFont.setFamily(style.tabFontFamily);
        infoFont.setPixelSize(std::max(11, style.tabFontSize));
        infoFont.setWeight(QFont::Normal);
        playbackInfoLabel_->setFont(infoFont);
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
    // No fixed height — let the panel fit tightly around its content (buttons +
    // value) after the align-row widgets are reparented to the tools panel.
    playbackPanel_->setMinimumHeight(78);
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
    speedButton_->setFixedSize(64, 28);
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

    // Playback value (relative frame + time from trigger). Kept out of the
    // button toolbar — it lives on its own row below the media buttons,
    // regular (non-bold) weight. Styled via palette+font (NOT a local
    // stylesheet): a bare color rule on the label would leak into this label's
    // tooltip and make its text a different color.
    playbackInfoLabel_ = new QLabel("-- | --", playbackPanel_);
    playbackInfoLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    playbackInfoLabel_->setToolTip("Relative frame and time from trigger.");
    QPalette infoPal = playbackInfoLabel_->palette();
    infoPal.setColor(QPalette::WindowText, QColor(tc.text));
    playbackInfoLabel_->setPalette(infoPal);
    QFont infoFont = playbackInfoLabel_->font();
    infoFont.setPixelSize(12);
    infoFont.setWeight(QFont::Normal);
    playbackInfoLabel_->setFont(infoFont);

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

    // Value row: the frame/time readout sits below the media buttons with a
    // little gap, but not at the very bottom of the panel (the align row stays
    // below).
    auto infoRow = new QHBoxLayout();
    infoRow->setSpacing(4);
    infoRow->setContentsMargins(0, 6, 0, 0);
    infoRow->addWidget(playbackInfoLabel_, 1);
    layout->addLayout(infoRow);

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
        seekToRelativeFrame(frameIndex - triggerFrameIndex_);
        applyAnnotationToSelectedFrame();
        updateAnnotationSliderMarkers();
    });
    connect(selectedCameraWidget_, &AnalysisVideoWidget::doubleClicked,
            this, &AnalysisView::onSelectedCameraDoubleClicked);
    if (layout) {
        layout->addWidget(selectedCameraWidget_, 1);
    }
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
    const int frameIndex = frameIndexForSliderValue(value);
    seekToFrameIndex(frameIndex, false);
}

int AnalysisView::currentReviewFrameIndex() const {
    const int maxFrame = std::max(0, static_cast<int>(std::floor(totalFrames_)));
    return qBound(0, static_cast<int>(std::floor(currentFrame_ + 0.0001)), maxFrame);
}

int AnalysisView::displayedFrameIndexForCamera(int camIdx, int masterFrameIndex) const {
    const int maxIdx = std::max(0, static_cast<int>(std::floor(totalFrames_)));
    if (camIdx < 0 || camIdx >= static_cast<int>(cameraFrameOffsets_.size())) {
        return qBound(0, masterFrameIndex, maxIdx);
    }
    return qBound(0, masterFrameIndex + cameraFrameOffsets_[camIdx], maxIdx);
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

    frameInput_->setText(hasRelativeTimeAxis()
        ? QString::number(relativeSeconds, 'f', 3)
        : QString::number(relativeFrame, 'f', 1));
    updatePlaybackInfoLabel();

    if (!isReviewMode_) {
        return;
    }

    QString overlayText = getMetadataOverlayText(idx, relativeFrame);
    QString tooltipText = getMetadataTooltip(idx, relativeFrame);

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
                applyAnnotationToWidget(cameraWidgets_[camIdx], camIdx, displayIdx);

                if (selectedCameraWidget_ && selectedCameraWidget_->getCameraId() == camIdx) {
                    selectedCameraWidget_->setFrame(finalImage);
                    selectedCameraWidget_->setTimestamp(overlayText, tooltipText);
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
                applyAnnotationToSelectedFrame();
            }
        }
        return;
    }

    if (!cameraWidgets_.empty()) {
        cameraWidgets_[0]->setFrame(frameImage);
        cameraWidgets_[0]->setTimestamp(overlayText, tooltipText);
        applyAnnotationToWidget(cameraWidgets_[0], 0, idx);
    }
    for (int wi = 1; wi < static_cast<int>(cameraWidgets_.size()); ++wi) {
        cameraWidgets_[wi]->clear();
    }

    if (selectedCameraWidget_) {
        selectedCameraWidget_->setFrame(frameImage);
        selectedCameraWidget_->setTimestamp(overlayText, tooltipText);
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
            if (it.key().startsWith("cam") && it.value().isDouble()) {
                defectMarks_[it.key()] = it.value().toInt();
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

void AnalysisView::onPreviousPressed() {
    seekToFrameIndex(currentReviewFrameIndex() - 1);
}

void AnalysisView::onPreviousReleased() {}

void AnalysisView::onResetClicked() {
    seekToFrameIndex(triggerFrameIndex_);
}

void AnalysisView::onNextPressed() {
    seekToFrameIndex(currentReviewFrameIndex() + 1);
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
        
        configureReviewSliderRange();
        
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
        playbackInfoLabel_->setText("-- | --");
        playbackInfoLabel_->setToolTip("Relative frame and time from trigger.");
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
    playbackInfoLabel_->setText(speedSummary.isEmpty() ? baseText : QString("%1 | %2").arg(baseText, speedSummary));

    // Full, descriptive tooltip: explain every value the label shows.
    QStringList tooltipLines;
    tooltipLines << QString("Relative frame: %1 (0 = the trigger frame; negative = before the trigger, positive = after)")
        .arg(relFrames, 0, 'f', 1);
    tooltipLines << QString("Time from trigger: %1%2 s")
        .arg(seconds >= 0.0 ? "+" : "")
        .arg(seconds, 0, 'f', 3);
    if (std::isfinite(currentEventInfo_.speedValue)) {
        const QString speedUnit = currentEventInfo_.speedUnit.isEmpty()
            ? QStringLiteral("m/min") : currentEventInfo_.speedUnit;
        tooltipLines << QString("Machine speed: %1 %2")
            .arg(currentEventInfo_.speedValue, 0, 'f', 2)
            .arg(speedUnit);
        const bool detailTabActive = tabWidget_ && tabWidget_->currentIndex() == 1 && selectedCameraId_ >= 0;
        if (detailTabActive) {
            const int cameraId = selectedCameraId_;
            const int basePositionMm = currentEventCameraPositionMm(cameraId);
            const double deltaMm = currentEventInfo_.speedValue * 1000.0 / 60.0 * seconds
                * static_cast<double>(currentEventInfo_.positionDirectionSign >= 0 ? 1 : -1);
            tooltipLines << QString("Distance traveled from trigger: %1 mm").arg(deltaMm, 0, 'f', 1);
            tooltipLines << QString("Camera position: %1 mm").arg(basePositionMm + deltaMm, 0, 'f', 1);
        }
        if (currentEventInfo_.speedStale) {
            tooltipLines << QStringLiteral("Note: machine speed was stale at capture — distance/alignment may be inaccurate");
        }
    }
    playbackInfoLabel_->setToolTip(tooltipLines.join("\n"));
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

    const int64_t triggerTimestamp = frameMetadata_[triggerFrameIndex_].timestamp;
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

    if (!isReviewMode_ || !playbackSlider_ || eventAnnotations_.isEmpty()) {
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
            framesWithMarkers.insert(frameIndex);
            const QString cameraLabel = key.left(framePos);
            frameCameraLabels[frameIndex].append(cameraLabel);
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

    // Second pass: manual defect marks (per-camera), drawn one row lower in danger red.
    const int defectYPos = yPos + 10;
    for (auto it = defectMarks_.begin(); it != defectMarks_.end(); ++it) {
        const QString camKey = it.key();
        const int camNumber = camKey.mid(3).toInt();
        if (camNumber <= 0) {
            continue;
        }
        if (detailTabActive && camNumber != selectedCameraId_ + 1) {
            continue;
        }
        if (!it.value().isDouble()) {
            continue;
        }
        const int frameIndex = static_cast<int>(it.value().toDouble());
        const int sliderValue = sliderValueForFrameIndex(frameIndex);
        if (sliderValue < sliderMin || sliderValue > sliderMax) {
            continue;
        }
        const double ratio = static_cast<double>(sliderValue - sliderMin) / (sliderMax - sliderMin);
        const int xPos = sliderRect.x() + (handleWidth / 2) + static_cast<int>(ratio * usableWidth);
        const double relativeSeconds = relativeSecondsForFrameIndex(frameIndex);

        QLabel* marker = new QLabel(playbackPanel_);
        marker->setFixedSize(9, 9);
        marker->setToolTip(QString("Defect mark: Camera %1 @ frame %2 (%3%4 s)")
            .arg(camNumber)
            .arg(frameIndex)
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

void AnalysisView::addEventRow(const QString& timestamp, const QString& reason, bool permanent,
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

    if (isNewRecentEvent) {
        // Pulse the highlight: a brief bright flash that decays onto the
        // static tint. The row is re-located by flag on every tick, so the
        // exact index here does not need to be stable.
        startNewEventPulse(targetTable);
    }

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
        const QString triggerReason = event.triggerReason.trimmed().isEmpty()
            ? QStringLiteral("Triggered")
            : event.triggerReason.trimmed();
        addEventRow(event.timestamp, triggerReason, event.permanent, false,
                    event.triggerGroup, event.triggerIndex);
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
    // Enable the Delete segment (always visible so the segmented control keeps
    // its divider; only the enabled state is gated by admin + this toggle).
    if (deleteButton_) {
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
    currentEventInfo_ = EventDatabase::EventInfo();

    const int configuredCameraCount = CameraConfig::getCameraCount();
    if (configuredCameraCount > 0 && configuredCameraCount != static_cast<int>(cameraWidgets_.size())) {
        setCameraCount(configuredCameraCount);
    }
    eventAnnotations_ = QJsonObject();
    cameraFrameOffsets_.clear();
    defectMarks_ = QJsonObject();
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
    if (!std::isfinite(currentEventInfo_.speedValue)) {
        return QString();
    }

    QStringList parts;
    parts.append(QString("%1 %2")
        .arg(currentEventInfo_.speedValue, 0, 'f', 2)
        .arg(currentEventInfo_.speedUnit.isEmpty() ? QStringLiteral("m/min") : currentEventInfo_.speedUnit));

    const bool detailTabActive = tabWidget_ && tabWidget_->currentIndex() == 1 && selectedCameraId_ >= 0;
    if (detailTabActive) {
        const int cameraId = selectedCameraId_;
        const int basePositionMm = currentEventCameraPositionMm(cameraId);
        const double deltaMm = currentEventInfo_.speedValue * 1000.0 / 60.0 * relativeSeconds
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

void AnalysisView::markDefectForSelectedCamera() {
    if (!isReviewMode_ || selectedCameraId_ < 0) {
        return;
    }
    defectMarks_[QString("cam%1").arg(selectedCameraId_ + 1)] =
        displayedFrameIndexForCamera(selectedCameraId_, currentReviewFrameIndex());
    saveEventAnnotations();
    updateAnnotationSliderMarkers();
    updateAlignmentStatus();
}

void AnalysisView::applyCameraAlignment() {
    if (!isReviewMode_ || videoReaders_.empty()) {
        return;
    }

    // Machine speed comes from the OPC UA "Machine Speed" tag captured with
    // the event (EventInfo.speedValue). No manual override.
    if (!std::isfinite(currentEventInfo_.speedValue) || currentEventInfo_.speedValue <= 0.0) {
        if (alignStatusLabel_) alignStatusLabel_->setText("No Machine Speed captured for this event");
        return;
    }
    if (currentEventInfo_.speedStale) {
        if (alignStatusLabel_) alignStatusLabel_->setText("Machine Speed was stale at capture — Align skipped");
        return;
    }
    const double speed = currentEventInfo_.speedValue;
    const QString speedUnit = currentEventInfo_.speedUnit.isEmpty()
        ? QStringLiteral("m/min") : currentEventInfo_.speedUnit;

    double fps = videoReaders_.begin()->second->getFps();
    if (fps <= 0.0) fps = CameraConfig::getFps();
    if (fps <= 0.0) fps = 10.0;

    const int sign = currentEventInfo_.positionDirectionSign >= 0 ? 1 : -1;
    const double framesPerMm = fps * 60.0 / (speed * 1000.0);

    const int refCam = videoReaders_.begin()->first;
    const int refPos = currentEventCameraPositionMm(refCam);
    if (refPos <= 0) {
        if (alignStatusLabel_) alignStatusLabel_->setText("Set camera positions in Camera config, then Align");
        return;
    }

    QStringList applied;
    QStringList appliedNamed;
    for (auto& pair : videoReaders_) {
        const int pos = currentEventCameraPositionMm(pair.first);
        if (pos <= 0) {
            cameraFrameOffsets_[pair.first] = 0;
            continue;
        }
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
        const QString speedText = QString("%1 %2").arg(speed, 0, 'f', 1).arg(speedUnit);
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
        return;
    }
    const int numReaders = static_cast<int>(videoReaders_.size());
    QString text = QString("Marks: %1/%2 — adjust each camera, then Align")
        .arg(defectMarks_.size()).arg(numReaders);
    QString named = text;

    // Alignment verdict: when every marked camera's defect displays at the same
    // master frame (mark - offset), all cameras are in sync.
    QMap<int, int> markMasterFrames;  // camera index -> master frame of its defect
    for (auto it = defectMarks_.begin(); it != defectMarks_.end(); ++it) {
        const int camIndex = it.key().mid(3).toInt() - 1;
        if (camIndex < 0 || camIndex >= static_cast<int>(cameraFrameOffsets_.size())) {
            continue;
        }
        markMasterFrames[camIndex] = static_cast<int>(it.value().toDouble())
            - cameraFrameOffsets_[camIndex];
    }
    if (markMasterFrames.size() >= 2) {
        int minFrame = markMasterFrames.constBegin().value();
        int maxFrame = minFrame;
        for (auto it = markMasterFrames.constBegin(); it != markMasterFrames.constEnd(); ++it) {
            minFrame = qMin(minFrame, it.value());
            maxFrame = qMax(maxFrame, it.value());
        }
        if (minFrame == maxFrame) {
            const double seconds = relativeSecondsForFrameIndex(minFrame);
            text += QString(" · all defects @ %1 (%2%3 s)")
                .arg(minFrame)
                .arg(seconds >= 0.0 ? "+" : "")
                .arg(seconds, 0, 'f', 3);
            named += QString(" · all defects @ frame %1 (%2%3 s)")
                .arg(minFrame)
                .arg(seconds >= 0.0 ? "+" : "")
                .arg(seconds, 0, 'f', 3);
        } else {
            text += QString(" · marks differ by %1 f — re-check marks").arg(maxFrame - minFrame);
            named += QString(" · marks differ by %1 frames — re-check marks").arg(maxFrame - minFrame);
        }
    }
    alignStatusLabel_->setText(text);
    alignStatusLabel_->setToolTip(named);
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
