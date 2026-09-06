#pragma once

#include <QMainWindow>
#include <opencv2/opencv.hpp>

class CameraWidget;
class LiveDashboard;
class QToolButton;
class QStackedWidget;

/**
 * A single detached top-level window showing ONLY live video — no settings,
 * parameters, or status widgets. It holds two pages:
 *   - Grid page:        the same multi-camera tile grid as the main Live View.
 *   - Single-camera page: one camera's live feed filling the window, with a
 *                         single "Back to Grid" button (no other controls).
 * Double-clicking a tile in the grid page switches to that camera's live feed.
 *
 * Video frames are the only live data pushed in; they arrive from MainWindow
 * via updateFrame(). The window never reads or edits configuration and owns no
 * status/parameter UI.
 */
class LiveViewWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit LiveViewWindow(QWidget* parent = nullptr);

    // -1 while the grid page is active; the camera shown on the single page
    // otherwise.
    int currentCameraId() const;
    bool isShowingGrid() const;

    // Video fan-out (the only live data this window consumes).
    void updateFrame(int cameraId, const cv::Mat& frame);
    void clearCamera(int cameraId);

    // Configuration-driven refreshes (camera count / layout / theme).
    void setCameraCount(int count);
    void setGridDimensions(int rows, int cols);
    void refreshCameraLabels();
    void updateTheme();

public slots:
    void showGrid();
    void showSingleCamera(int cameraId);

private:
    LiveDashboard* grid_ = nullptr;
    CameraWidget* single_ = nullptr;
    QToolButton* backButton_ = nullptr;
    QStackedWidget* stack_ = nullptr;
};