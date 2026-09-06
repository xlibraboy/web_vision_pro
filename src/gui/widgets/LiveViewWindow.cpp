#include "LiveViewWindow.h"
#include "CameraWidget.h"
#include "../LiveDashboard.h"
#include "../../config/CameraConfig.h"
#include <QFont>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace {

QFont liveViewTitleFont() {
    const LiveViewCardStyle style = CameraConfig::getLiveViewCardStyle();
    QFont font(style.gridTitleFontFamily);
    font.setPixelSize(style.gridTitleFontSize);
    font.setBold(true);
    return font;
}

} // namespace

LiveViewWindow::LiveViewWindow(QWidget* parent)
    : QMainWindow(parent) {
    setAttribute(Qt::WA_DeleteOnClose);

    // Grid page: full multi-camera grid, purely observational (no per-tile
    // pop-out buttons, no configuration surface).
    grid_ = new LiveDashboard(CameraConfig::getCameraCount(), this);
    connect(grid_, &LiveDashboard::cameraSelected,
            this, &LiveViewWindow::showSingleCamera);

    // Single-camera page: a compact back row on top of the bare camera feed.
    QWidget* singlePage = new QWidget(this);
    QVBoxLayout* singleLayout = new QVBoxLayout(singlePage);
    singleLayout->setContentsMargins(0, 0, 0, 0);
    singleLayout->setSpacing(0);

    QHBoxLayout* backRow = new QHBoxLayout();
    backRow->setContentsMargins(4, 4, 4, 2);
    backRow->setSpacing(4);
    backButton_ = new QToolButton(this);
    backButton_->setText(QStringLiteral("\u2190 Back to Grid"));
    backButton_->setAutoRaise(true);
    backButton_->setCursor(Qt::PointingHandCursor);
    connect(backButton_, &QToolButton::clicked, this, &LiveViewWindow::showGrid);
    backRow->addWidget(backButton_);
    backRow->addStretch();
    singleLayout->addLayout(backRow);

    single_ = new CameraWidget(this);
    singleLayout->addWidget(single_, 1);

    stack_ = new QStackedWidget(this);
    stack_->addWidget(grid_);        // index 0: grid
    stack_->addWidget(singlePage);   // index 1: single camera
    setCentralWidget(stack_);

    setWindowTitle(QStringLiteral("Live View"));
    resize(1280, 720);
    showGrid();
}

int LiveViewWindow::currentCameraId() const {
    return stack_ && stack_->currentIndex() == 1 ? single_->cameraId() : -1;
}

bool LiveViewWindow::isShowingGrid() const {
    return !stack_ || stack_->currentIndex() == 0;
}

void LiveViewWindow::showGrid() {
    if (stack_) {
        stack_->setCurrentIndex(0);
    }
    setWindowTitle(QStringLiteral("Live View \u2014 Grid"));
}

void LiveViewWindow::showSingleCamera(int cameraId) {
    if (cameraId < 0 || cameraId >= CameraConfig::getCameraCount()) {
        return;
    }

    single_->setCameraId(cameraId);
    single_->setOverlayFont(liveViewTitleFont());
    single_->setOverlayText(CameraConfig::getCameraLabel(cameraId));
    setWindowTitle(QStringLiteral("Live View \u2014 %1")
                       .arg(CameraConfig::getCameraLabel(cameraId)));
    if (stack_) {
        stack_->setCurrentIndex(1);
    }
}

void LiveViewWindow::updateFrame(int cameraId, const cv::Mat& frame) {
    // Keep the hidden grid current so its tiles are fresh when the user
    // returns to it.
    grid_->updateFrame(cameraId, frame);
    if (stack_ && stack_->currentIndex() == 1 && single_->cameraId() == cameraId) {
        single_->updateFrame(frame);
    }
}

void LiveViewWindow::clearCamera(int cameraId) {
    grid_->clearCameraWidget(cameraId);
    if (single_->cameraId() == cameraId) {
        single_->clearFrame();
    }
}

void LiveViewWindow::setCameraCount(int count) {
    grid_->setCameraCount(count);
}

void LiveViewWindow::setGridDimensions(int rows, int cols) {
    grid_->setGridDimensions(rows, cols);
}

void LiveViewWindow::refreshCameraLabels() {
    grid_->refreshCameraLabels();

    // If the single page shows a camera that no longer exists, fall back to
    // the grid rather than showing a stale label.
    const int id = single_->cameraId();
    if (stack_ && stack_->currentIndex() == 1
            && (id < 0 || id >= CameraConfig::getCameraCount())) {
        showGrid();
    }
}

void LiveViewWindow::updateTheme() {
    grid_->updateTheme();
    single_->update();
}