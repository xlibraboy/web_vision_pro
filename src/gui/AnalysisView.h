#pragma once

#include <QWidget>
#include <QSplitter>
#include <QFutureWatcher>
#include <QPushButton>

struct EventSignalData;
#include <QTableWidget>
#include <QTabWidget>
#include <QSlider>
#include <QToolBar>
#include <QProgressBar>
#include <QLabel>
#include <QCheckBox>
#include "widgets/ToggleSwitch.h"
#include <QLineEdit>
#include <QGridLayout>
#include <QGroupBox>
#include <QGroupBox>
#include <QMenu>
#include <QSpinBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QComboBox>
#include <QGroupBox>
#include <QMenu>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QtConcurrent>
#include <QDateTime>
#include <QTimer>
#include <QPropertyAnimation>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QVector>
#include <QPointF>
#include <QStringList>
#include <vector>
#include <deque>
#include <cmath>
#include <cstdint>
#include <map>
#include <opencv2/opencv.hpp>
#include <pylon/PylonIncludes.h>

#include "../core/CameraManager.h"
#include "../core/EventDatabase.h"


class AnalysisVideoWidget;
class QGraphicsOpacityEffect;

/**
 * Analysis View - Video playback and analysis interface
 * Features: Left sidebar with controls, tabbed camera grid, playback controls
 */
class AnalysisView : public QWidget {
    Q_OBJECT

public:
    explicit AnalysisView(int numCameras = 8, QWidget *parent = nullptr);
    ~AnalysisView();

signals:
    void serverToggled(bool running);
    void adminLoginRequested();
    void recordAllToggled(bool recording);
    void manualTriggerRequested(); // Signal to request a manual trigger from MainWindow
    void eventAdded(); // Signal emitted when a new event is added to the log

public slots:
    void addPaperBreakEvent(const std::string& timestamp, int triggerIndex, int totalFrames,
                            int primaryCameraId = 1);
    // Placeholder "Recording…" row shown the instant a trigger is accepted.
    void addPendingEventRow(const QString& timestamp, const QString& reason);
    void setPlaybackPosition(double frame);
    void updateCameraFrame(int cameraId, const QImage& frame);
    
    // Dynamically update camera count
    void setCameraCount(int count);
    
    // Trigger Review (load from disk)
    void startReview(const QString& eventPath, int triggerIndex = -1);
    
    // Trigger Review (from video file - on-demand loading)
    void startReviewFromFile(const QString& videoPath, int triggerIndex);
    
    void setLiveMode();
    void clearData(); // Explicitly clear memory
    void setDeleteEnabled(bool enabled); // Toggle delete mode
    void setAdminMode(bool isAdmin); // Expose to MainWindow
    void updateTheme(); // Dynamically update widget theme colors
    void setCameraManager(CameraManager* manager); // Wire live camera data for Diagnostic tab
    void reloadEventStorage();
    // Push the real vision-system state (from MainWindow's camera lifecycle)
    // into the Control Panel button so it reflects reality, not just clicks.
    void setServerRunning(bool running);
    // Show the intermediate "Connecting…" state while camera startup runs
    // (instead of flipping straight from Offline to Online).
    void setServerConnecting(bool connecting);
    
private slots:
    void onServerButtonClicked();
    void onAdminButtonClicked();
    void onDeleteClicked();
    void onTogglePermanentClicked();
    void onInstantClearClicked();
    
    void onLogSelected(int row, int col);
    void onTiffLoadingFinished();
    
    void onCameraClicked(int cameraId);
    void onSelectedCameraDoubleClicked(int cameraId);
    void onTabChanged(int index);
    void onSliderMoved(int value);
    void onSliderValueChanged(int value); // Handle click-to-seek
    void onPlayPauseClicked();
    void onBeginClicked();
    void onPreviousPressed();
    void onPreviousReleased();
    void onResetClicked();
    void onNextPressed();
    void onNextReleased();
    void onEndClicked();
    void onFrameInputChanged();
    void onSpeedChanged(QAction* action);
    void onPlaybackTick();  // Timer-based playback update
    void setupEventDashboards();
    void refreshDashboardForCamera(int camIdx);
    void generateThumbnails(int camIdx);
    void refreshDashboardThumbnails();
    void updateDashboardLoadingState();
    // Show the dashboard only when its data (signals + thumbnails) is fully
    // loaded and the TOOLS toggle allows it; progress bar otherwise.
    void updateDashboardVisibility(bool loading);
    void updateTracksEdgeTabVisibility();
    void restyleTracksEdgeTab();
    void startNextSignalScan();
    void onDashboardSeekRequested(int frame);
    void onSignalScanFinished(const QString& binPath, const EventSignalData& data);
    void onDetailWindowFinished(const QString& binPath, int startFrame, int endFrame,
                                const EventSignalData& data);
    void onDetailWindowFailed(const QString& binPath, int startFrame, int endFrame,
                              const QString& reason);
    // Fire a lazy stride-1 sub-scan when the detail strip's snapped window
    // changes on a >600-frame event (no-op otherwise).
    void maybeScanDetailWindow(int frameIndex);
    void onSignalScanFailed(const QString& binPath, const QString& reason);
    // How many frames one step / scrub advances at the current speed selector.
    int playbackStepSize() const;

    // Diagnostic tab refresh slots
    void refreshDiagTable();
    void onDiagAutoRefreshToggled(bool enabled);

    // Server-button Connecting… animation
    void onServerConnectingTick();
    // New-event row highlight pulse animation
    void onNewEventPulseTick();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUI();
    void applyAnalysisViewStyle();
    void setupLeftSidebar();
    void setupMainArea();
    void setupPlaybackControls();
    void setupCameraGrid(QWidget* container);
    void setupDiagnosticTab();   // Build the all-camera diagnostics table
    void updateDynamicTab(int cameraId);
    void updatePlaybackControlsState(); // Enable/disable controls based on data availability
    void updatePlaybackInfoLabel();
    void updateSliderZeroMarker();  // Position the zero marker on the slider
    void updateAnnotationSliderMarkers();
    void setPlaybackPlaying(bool playing);
    // Timer interval for one playback/scrub tick at the current speed:
    // real-time (1000/reviewFps_) when the event fps is known, legacy 33 ms
    // assumption otherwise.
    int playbackTickIntervalMs() const;
    void seekToRelativeFrame(double relativeFrame);
    // Camera-selection shortcuts: digit keys 1..N open that camera by its
    // visible id (multi-digit: keys are buffered until the delay elapses, with
    // a live id+name banner so the target is known before the switch), 0
    // returns to the All Cameras grid, Up/Down step to the previous/next
    // camera. Returns true when the key was consumed. No-ops while the TOOLS
    // layer panel is open so digits/arrows can't be hijacked away from its
    // inputs.
    bool handlePlayerCameraKey(int key);
    // Pending multi-digit camera entry: cameraKeyBuffer_ accumulates digits
    // and the preview banner shows until the timer fires, then the camera
    // opens. Non-digit media keys and camera clicks cancel a pending entry.
    QString cameraKeyBuffer_;
    QTimer*  cameraKeyTimer_ = nullptr;
    QLabel*  cameraKeyBanner_ = nullptr;
    static constexpr int kCameraKeyEntryDelayMs = 700;
    void handleCameraDigit(int digit);
    void resolveCameraKeyEntry();  // timer fired: open the buffered camera
    void cancelCameraKeyEntry();   // drop buffer, stop timer, hide banner
    void refreshCameraKeyPreview(); // banner text follows the buffer digits
    void positionCameraKeyBanner(); // center over the video area, top zone

    // Per-camera playback alignment (review-time defect sync)
    int displayedFrameIndexForCamera(int camIdx, int masterFrameIndex) const;
    // Maps a camera's own frame index to the equivalent timeline (longest
    // recording) frame index via hardware timestamps; -1 when the event has
    // no cross-comparable timestamps (legacy shared-index behavior).
    int tlIndexOfOwnFrame(int camIdx, int ownFrame) const;
    // Inverse of displayedFrameIndexForCamera: the master (timeline) frame at
    // which camIdx displays ownFrame. Legacy events fall back to the raw index.
    int masterFrameForCameraFrame(int camIdx, int ownFrame) const;
    // fps of the timeline camera (offsets/seconds live in its frame domain);
    // falls back to the first reader, then CameraConfig, then 10.
    double timelineFps() const;
    void markDefectForSelectedCamera();
    void applyCameraAlignment();
    // Align cameras using placed defect marks as ground truth (needs >= 2 marked
    // cameras in this event). Returns false to let applyCameraAlignment fall back
    // to the machine-speed/position formula.
    bool tryAlignToMarks();
    void clearCameraOffsets();
    void updateAlignmentStatus();
    void onCameraOffsetChanged(int value);
    // Defect-mark helpers (a camera may carry several marks).
    // Parses a sidecar value: legacy single int or new array of ints.
    QVector<int> defectMarkFrames(const QJsonValue& value) const;
    QVector<int> defectMarksForCamera(int camIndex) const;
    // Sync crosshair: show on every camera view while viewing a synced frame.
    void applySyncIndicators();
    QPointF syncIndicatorPosForCamera(int camIndex) const;

    // Right tools panel
    void onToolsLockToggled();
    void onToolsHoverTick();
    void positionToolsPanel();
    void restyleToolsEdgeTab();
    void applyToolsPanelTheme();
    // Show/hide transition (fade + slide).
    void animateToolsPanelShow();
    void animateToolsPanelHide();
    void onToolsPanelHideFinished();
    bool toolsPanelActuallyVisible() const;
    // Attach/remove the fade effect around a transition only, so the panel
    // renders natively (no compositing artifacts) while idle or hidden.
    void ensureToolsPanelOpacityEffect();
    void clearToolsPanelOpacityEffect();
    
    // Main layout
    
    // Main layout
    QSplitter* mainSplitter_;
    
    // Left sidebar
    QWidget* leftSidebar_;
    QPushButton* serverButton_;
    QPushButton* adminButton_;
    ToggleSwitch* enableDeleteCheck_;
    QLabel* recentRecordsLabel_ = nullptr;
    QLabel* permanentRecordsLabel_ = nullptr;
    QTableWidget* paperBreakTable_;
    QTableWidget* permanentPaperBreakTable_;
    QWidget* permanentSectionWidget_;
    QPushButton* togglePermanentTableButton_;
    QPushButton* instantClearButton_ = nullptr;
    QSpinBox* instantClearKeepSpin_ = nullptr;
    
    // Main area
    QWidget* mainArea_;
    QTabWidget* tabWidget_;
    QWidget* allCameraTab_;
    QWidget* singleCameraTab_;
    QWidget* diagnosticTab_;
    QGridLayout* cameraGridLayout_ = nullptr;
    QWidget* metadataHeaderWidget_ = nullptr;
    QComboBox* metadataDisplayCombo_ = nullptr;
    QWidget* detailToolsWidget_ = nullptr;
    QFrame* headerToolsSeparator_ = nullptr;
    QCheckBox* markerToolCheck_ = nullptr;
    QComboBox* markerShapeCombo_ = nullptr;
    QSlider* zoomSlider_ = nullptr;
    QSlider* brightnessSlider_ = nullptr;
    QLabel* zoomValueLabel_ = nullptr;
    QLabel* brightnessValueLabel_ = nullptr;

    // Diagnostic tab — all-camera live data table
    QTableWidget* diagTable_         = nullptr;
    QTimer*       diagRefreshTimer_  = nullptr;
    QPushButton*  diagRefreshBtn_    = nullptr;
    QCheckBox*    diagAutoRefreshChk_= nullptr;

    // Per-camera drop-rate tracking: previous cumulative drop count and the
    // wall-clock time it was sampled, indexed by camera row.
    std::vector<uint64_t> diagPrevDropCount_;
    std::vector<qint64>   diagPrevDropSampleMs_;

    // Per-camera Drops/s sample history for the trend sparkline (last N
    // samples, indexed by camera row). Reset when a camera goes offline.
    std::vector<std::vector<double>> diagDropRateHistory_;
    static const int kDiagDropRateHistoryMax = 60; // ~3 min at 3 s refresh

    // CameraManager pointer (set from MainWindow after construction)
    CameraManager* cameraManager_ = nullptr;


    
    // Camera widgets
    std::vector<AnalysisVideoWidget*> cameraWidgets_;
    AnalysisVideoWidget* selectedCameraWidget_;
    int numCameras_;
    int selectedCameraId_;
    
    // Playback controls
    QWidget* playbackPanel_;
    QSlider* playbackSlider_;
    QLabel* sliderZeroMarker_;  // Visual marker at the 0 frame position
    QVector<QLabel*> annotationSliderMarkers_;
    QToolBar* playbackToolbar_;
    QPushButton* speedButton_;
    QMenu* speedMenu_;
    QPushButton* playPauseButton_;
    QPushButton* beginButton_;
    QPushButton* prevButton_;
    QPushButton* resetButton_;
    QLineEdit* frameInput_;
    QPushButton* nextButton_;
    QPushButton* endButton_;
    // Playback info (relative frame + time from trigger + optional speed
    // summary) composed in updatePlaybackInfoLabel and drawn in-frame by
    // AnalysisVideoWidget::setPlaybackInfo.
    QString playbackInfoText_;
    
    // Export controls
    QPushButton* saveAviButton_;
    
    // Settings actions
    // Settings actions
    // QAction* rawModeAction_; // Removed
    // QPushButton* deleteButton_; // Moved to Main Window Menu
    QPushButton* deleteButton_; // For deleting events
    QPushButton* permanentButton_;
    // Segmented "Mark Permanent | Delete Selected" row (one compact control).
    QWidget* splitActionRow_ = nullptr;
    QFrame* deleteActionDivider_ = nullptr;

    
    // State
    bool serverRunning_;
    bool serverConnecting_ = false; // True while camera startup is in progress
    // Animated-ellipsis timer for the Connecting… button state.
    QTimer* serverConnectingTimer_ = nullptr;
    int serverConnectingDots_ = 0;
    bool adminMode_ = false; // Tracks admin login state for the Login/Logout button
    bool isRecording_;
    
    // Theme State
    QString activeThemePrimaryColor_;
    QString activeThemeHoverColor_;
    bool isPlaying_;
    bool isReviewMode_;  // True when reviewing a triggered event
    double currentFrame_;
    double totalFrames_;
    double playbackSpeed_;
    // Real fps of the event's primary camera (from its RAW header). Drives
    // playback/scrub timing so 1.0x means true real-time; 0 = unknown
    // (non-streaming modes) and the legacy 30 fps assumption applies.
    double reviewFps_ = 0.0;
    int triggerFrameIndex_; // Index of the trigger point (t=0)
    int baseWidth_;
    int baseHeight_;
    QDateTime eventBaseTime_; // Real-world time reference from filename
    
    // Recorded sequence for review (pre/post trigger)
    std::vector<QImage> recordedSequence_;
    
    // Metadata storage
    struct FrameMetadata {
        int64_t timestamp;
        int64_t frameCounter;
        QString displayTime; // Pre-formatted time string
    };
    std::vector<FrameMetadata> frameMetadata_;
    // Trigger frame index inside the reader that provided frameMetadata_ (its
    // own raw-header trigger flag). -1 = unknown: fall back to the primary
    // camera's triggerFrameIndex_. The metadata source may be a different
    // camera than the event's primary when its recording is longer
    // (per-camera acquisition fps), so the relative-time zero must be that
    // reader's own trigger instant.
    int metadataTriggerIndex_ = -1;
    // Camera (videoReaders_ key) whose reader provided frameMetadata_ — the
    // LONGEST recording, which defines the scrub timeline length.
    int timelineCameraIdx_ = -1;
    // Per-camera per-frame hardware timestamps (ns, epoch) captured at event
    // load. Used to map a timeline frame to each camera's nearest own frame so
    // mixed-fps recordings stay wall-clock aligned while scrubbing. Cameras
    // without RAW metadata (legacy video events) are absent and fall back to
    // the shared raw index.
    std::map<int, std::vector<int64_t>> cameraTimestamps_;
    
    // Helper to load raw binary
    void loadRawSequence(const QString& binPath);
    QString formatTimestamp(const QString& rawTs);
    QString getMetadataOverlayText(int frameIndex, double relativeFrame);
    QString getMetadataTooltip(int frameIndex, double relativeFrame);
    QString currentEventCameraLabel(int cameraId) const;
    int currentEventCameraPositionMm(int cameraId) const;
    QString currentSpeedSummary(double relativeSeconds) const;

    int currentReviewFrameIndex() const;
    bool hasRelativeTimeAxis() const;
    double relativeSecondsForFrameIndex(int frameIndex) const;
    int sliderValueForFrameIndex(int frameIndex) const;
    int frameIndexForSliderValue(int value) const;
    void configureReviewSliderRange();
    void renderCurrentReviewFrame(bool updateSlider);
    void seekToFrameIndex(int frameIndex, bool updateSlider = true);
    QString annotationKey(int cameraId, int frameIndex) const;
    QMap<QString, int> loadEventAnnotations(const QString& videoPath);
    void saveEventAnnotations();
    void applyAnnotationToSelectedFrame();
    void applyAnnotationToWidget(AnalysisVideoWidget* widget, int cameraId, int frameIndex);
    // Returns the final row index of the added row (after the table's
    // timestamp sort), so callers can locate it without assuming where the
    // sort left it.
    int addEventRow(const QString& timestamp, const QString& reason, bool permanent,
                    bool selectRow, int group = CameraGroup::kUnassigned,
                    int defectFrame = -1);
    void reloadEventTables();
    // Re-append the pending placeholder row after a table rebuild (and retire
    // it once stale or once the real event is in the database).
    void insertPendingEventRow();
    void updateRecordCountLabel();
    void updatePermanentButtonLabel();
    QTableWidget* createLogTable(QWidget* parent, bool deleteMode);
    void configureLogTable(QTableWidget* table, bool deleteMode);
    // Size the Reason column so Trigger Time + Reason exactly fill the visible
    // table width (proportional), leaving Group + Defect Frame behind the
    // horizontal scroll.
    void updateLogTableReasonWidths();
    void connectLogTable(QTableWidget* table);
    void sortLogTable(QTableWidget* table);
    void selectLatestEvent();
    void moveSelectedRowsToTable(QTableWidget* sourceTable, QTableWidget* targetTable, bool permanent);
    // Animate the newest-event row highlight: a brief bright pulse that decays
    // onto the static tint. Called from addEventRow after insert + sort; every
    // tick re-locates the flagged row (and re-validates), so the animation
    // survives re-sorts and stops cleanly when the row is rebuilt, moved, or
    // the highlight is acknowledged by a click.
    void startNewEventPulse(QTableWidget* table);
    void stopNewEventPulse();
    void applyNewEventPulseAlpha(QTableWidget* table, int row, int alpha);
    // Row (or -1) whose column-0 item still carries the new-event flag.
    int locateNewEventRow(QTableWidget* table) const;
    static int newEventPulseAlphaForStep(int step);
    QString latestAddedEventTimestamp_;
    // Pending trigger placeholder: timestamp + wall-clock start (ms) for the
    // stale guard (an event that never saves would otherwise linger forever).
    QString pendingEventTimestamp_;
    qint64 pendingEventStartMs_ = 0;
    bool suppressNewEventIndicatorClear_ = false;
    // New-event row pulse animation state.
    QTimer* newEventPulseTimer_ = nullptr;
    QTableWidget* newEventPulseTable_ = nullptr;
    int newEventPulseRow_ = -1;
    int newEventPulseSteps_ = 0;
    static constexpr int kNewEventPulseTickMs = 40;
    static constexpr int kNewEventPulseDurationMs = 1200;
    static constexpr int kNewEventPulseBaseAlpha = 36;   // static tint
    static constexpr int kNewEventPulseAmplitude = 190;  // peak flash above base
    QString currentAnnotationPath_;
    QStringList currentEventCameraLabels_;
    QJsonObject eventAnnotations_;
    EventDatabase::EventInfo currentEventInfo_;

    // Per-camera playback frame offset (slot 0-based) applied on top of the shared
    // review timeline in renderCurrentReviewFrame(). Index = camera slot.
    std::vector<int> cameraFrameOffsets_;
    // Manual defect marks: key "cam{N}" (N 1-based) -> absolute frame index
    // (legacy sidecars) or array of frame indices (multiple marks per camera).
    QJsonObject defectMarks_;
    // Master-frame positions (k-th marks of every marked camera) that are in
    // sync. Empty when marks don't fully agree; drives the on-frame crosshair.
    QVector<int> syncedMasterFrames_;

    QPushButton*    markDefectButton_ = nullptr;
    QSpinBox*       cameraOffsetSpin_ = nullptr;
    QPushButton*    alignButton_ = nullptr;
    QPushButton*    resetOffsetsButton_ = nullptr;
    QLabel*         alignStatusLabel_ = nullptr;

    // Right "Layer" tools panel: all review tools stacked vertically in named
    // groups, slightly transparent, floating over the video frame area.
    // Locked = pinned (always visible); unlocked = hover-driven: the vertical
    // TOOLS tab on the frame's right edge reveals the panel with a fade+slide
    // transition. The panel owns no graphics effect while idle (depth comes
    // from its border); a single QGraphicsOpacityEffect is attached only for
    // the duration of the fade, so nothing is ever nested under another effect
    // (which renders incorrectly on X11).
    QWidget*         rightToolsPanel_ = nullptr;
    QLabel*          toolsEdgeTab_ = nullptr;
    QPushButton*     toolsLockButton_ = nullptr;
    QPushButton*     resetToolsButton_ = nullptr;
    QCheckBox*       dashboardToggleCheck_ = nullptr;  // TOOLS panel: show/hide event dashboard
    bool             toolsLocked_ = false;  // start unpinned (hover-driven)
    bool             toolsTabHovered_ = false;  // cursor is over the edge tab
    QTimer*          toolsHoverTimer_ = nullptr;
    // Show/hide animation state. toolsPanelOpacity_ is non-null only while a
    // transition is in progress.
    QGraphicsOpacityEffect*    toolsPanelOpacity_ = nullptr;
    QPropertyAnimation*        toolsPanelFadeAnim_ = nullptr;
    QPropertyAnimation*        toolsPanelSlideAnim_ = nullptr;
    QRect                      toolsPanelRestingRect_;  // panel rect, mainArea_ coords
    bool                       toolsPanelShown_ = false;  // target visibility state

    
    // On-demand video loading (per active camera)
    std::map<int, std::unique_ptr<class VideoStreamReader>> videoReaders_;
    // Source .bin path per opened camera (for the signal scanner cache).
    std::map<int, QString> videoReaderPaths_;
    // Lazy detail sub-scan state: key of the window in flight or already
    // pushed to the dashboard (prevents per-tick re-requests).
    QString detailWindowKey_;
    // Event dashboard (prototype): single-camera time-series + thumbnails.
    class EventDashboard* detailDashboard_ = nullptr;   // Camera tab, below video
    // Load gate: while the signal scan / thumbnails run after a trigger, the
    // dashboard is hidden and a progress bar occupies its layout slot.
    QProgressBar* dashProgressBar_ = nullptr;
    int dashProgressPercent_ = -1;
    // TRACKS hover edge tab + panel: per-region show/hide for the dashboard's
    // five stacked tracks (same interaction pattern as the TOOLS tab).
    QLabel* tracksEdgeTab_ = nullptr;
    QWidget* tracksPanel_ = nullptr;
    QCheckBox* trackChecks_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    bool tracksTabHovered_ = false;
    class EventSignalScanner* signalScanner_ = nullptr;
    QStringList pendingScanPaths_;
    QFutureWatcher<QVector<QImage>>* thumbWatcher_ = nullptr;
    int thumbCamPending_ = -1;
    int currentDashCam_ = -1;
    struct CameraSignal {
        QVector<int> samples;
        QVector<double> brightness;
        QVector<double> stddev;
        QVector<double> spotPct;
        QVector<int> defects;
        QVector<int> localDefects;
        QVector<int> contrastDefects;
        int totalFrames = 0;
        double fps = 0.0;
    };
    std::map<QString, CameraSignal> signalByCam_;
    bool isStreamingMode_;  // True when loading from file instead of RAM
    
    QTimer* stepTimer_;  // For hold-click stepping
    QTimer* playbackTimer_;  // For automatic playback
    
    // Async TIFF loading
    QFutureWatcher<QVector<QImage>>* tiffLoaderWatcher_ = nullptr;
    QProgressDialog* loadingDialog_ = nullptr;
    int pendingTriggerIndex_;  // Store trigger index during async load
    
    // triggerButton_ removed
};
