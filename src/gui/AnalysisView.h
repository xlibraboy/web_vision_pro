#pragma once

#include <QWidget>
#include <QSplitter>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QSlider>
#include <QToolBar>
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
#include <vector>
#include <deque>
#include <opencv2/opencv.hpp>
#include <pylon/PylonIncludes.h>

class AnalysisVideoWidget;

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
    void addPaperBreakEvent(const std::string& timestamp, int triggerIndex, int totalFrames);
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
    
private slots:
    void onServerButtonClicked();
    void onAdminButtonClicked();
    void onLinkCamerasToggled(bool linked);
    void onDeleteClicked();
    void onTogglePermanentClicked();
    
    void onLogSelected(int row, int col);
    void onTiffLoadingFinished();
    
    void onCameraClicked(int cameraId);
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

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void setupUI();
    void setupLeftSidebar();
    void setupMainArea();
    void setupPlaybackControls();
    void setupCameraGrid(QWidget* container);
    void updateDynamicTab(int cameraId);
    void updatePlaybackControlsState(); // Enable/disable controls based on data availability
    void updateSliderZeroMarker();  // Position the zero marker on the slider
    
    // Main layout
    
    // Main layout
    QSplitter* mainSplitter_;
    
    // Left sidebar
    QWidget* leftSidebar_;
    QPushButton* serverButton_;
    QPushButton* adminButton_;
    ToggleSwitch* enableDeleteCheck_;
    QTableWidget* paperBreakTable_;
    QTableWidget* permanentPaperBreakTable_;
    QWidget* permanentSectionWidget_;
    QPushButton* togglePermanentTableButton_;
    
    // Main area
    QWidget* mainArea_;
    QTabWidget* tabWidget_;
    QWidget* allCameraTab_;
    QWidget* singleCameraTab_;
    QWidget* diagnosticTab_;


    
    // Camera widgets
    std::vector<AnalysisVideoWidget*> cameraWidgets_;
    AnalysisVideoWidget* selectedCameraWidget_;
    int numCameras_;
    int selectedCameraId_;
    
    // Playback controls
    QWidget* playbackPanel_;
    QSlider* playbackSlider_;
    QLabel* sliderZeroMarker_;  // Visual marker at the 0 frame position
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
    
    // Export controls
    QPushButton* saveAviButton_;
    
    // Settings actions
    // Settings actions
    // QAction* rawModeAction_; // Removed
    // QPushButton* deleteButton_; // Moved to Main Window Menu
    QPushButton* deleteButton_; // For deleting events
    QPushButton* permanentButton_;

    
    // State
    bool serverRunning_;
    bool isRecording_;
    
    // Theme State
    QString activeThemePrimaryColor_;
    QString activeThemeHoverColor_;
    bool isPlaying_;
    bool isReviewMode_;  // True when reviewing a triggered event
    double currentFrame_;
    double totalFrames_;
    double playbackSpeed_;
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
    
    // Helper to load raw binary
    void loadRawSequence(const QString& binPath);
    QString formatTimestamp(const QString& rawTs);
    QString getMetadataOverlayText(int frameIndex, double relativeFrame);
    QString getMetadataTooltip(int frameIndex, double relativeFrame);
    void addEventRow(const QString& timestamp, const QString& reason, bool permanent, bool selectRow);
    void reloadEventTables();
    void updatePermanentButtonLabel();
    QTableWidget* createLogTable(QWidget* parent, bool deleteMode);
    void configureLogTable(QTableWidget* table, bool deleteMode);
    void connectLogTable(QTableWidget* table);
    void sortLogTable(QTableWidget* table);
    void selectLatestEvent();
    void moveSelectedRowsToTable(QTableWidget* sourceTable, QTableWidget* targetTable, bool permanent);
    
    // On-demand video loading (per active camera)
    std::map<int, std::unique_ptr<class VideoStreamReader>> videoReaders_;
    bool isStreamingMode_;  // True when loading from file instead of RAM
    
    QTimer* stepTimer_;  // For hold-click stepping
    QTimer* playbackTimer_;  // For automatic playback
    
    // Async TIFF loading
    QFutureWatcher<QVector<QImage>>* tiffLoaderWatcher_;
    QProgressDialog* loadingDialog_;
    int pendingTriggerIndex_;  // Store trigger index during async load
    
    // triggerButton_ removed
};
