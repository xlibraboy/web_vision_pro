#include "LiveDashboard.h"
#include "../config/CameraConfig.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFont>

namespace {
QFont liveViewGridTitleFont() {
    const LiveViewCardStyle style = CameraConfig::getLiveViewCardStyle();
    QFont font(style.gridTitleFontFamily);
    font.setPixelSize(style.gridTitleFontSize);
    font.setBold(true);
    return font;
}

int cameraIndexForGridSlot(const std::vector<CameraInfo>& cameras, int gridSlot) {
    const int displayId = gridSlot + 1;
    for (int i = 0; i < static_cast<int>(cameras.size()); ++i) {
        if (cameras[i].id == displayId) {
            return i;
        }
    }
    return -1;
}
}

LiveDashboard::LiveDashboard(int numCameras, QWidget *parent) 
    : QWidget(parent), numCameras_(numCameras) {
    
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);  // Remove margins for more space

    // Grid Container
    gridContainer_ = new QWidget(this);
    gridLayout_ = new QGridLayout(gridContainer_);
    gridLayout_->setSpacing(4);
    gridLayout_->setContentsMargins(2, 2, 2, 2);

    // Create Camera Cells (Widget + Label)
    for (int i = 0; i < numCameras_; ++i) {
        // Container widget for camera + label
        QWidget* cellContainer = new QWidget(this);
        QVBoxLayout* cellLayout = new QVBoxLayout(cellContainer);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(2);
        
        // Camera Name Label (OUTSIDE frame, on top) - from centralized config
        QString labelText = CameraConfig::getCameraLabel(i);

        // Camera Widget
        CameraWidget* cam = new CameraWidget(this);
        cam->setCameraId(i);
        cam->setOverlayText(labelText); // Set overlay text locally
        cam->setOverlayFont(liveViewGridTitleFont());
        
        // Connect DOUBLE click signal for detail view
        connect(cam, &CameraWidget::doubleClicked, this, &LiveDashboard::cameraSelected);
        
        // cellLayout->addWidget(nameLabel); // REMOVED top label
        cellLayout->addWidget(cam, 1); // Stretch camera widget
        
        cameraWidgets_.push_back(cam);
        cameraCells_.push_back(cellContainer);
    }
    
    // Add grid container to main layout with full stretch
    mainLayout->addWidget(gridContainer_, 1);
    
    // Set default grid dimensions
    setGridDimensions(3, 3);
    
    // Create status labels but don't add them to layout (keep for updateStatus compatibility)
    statusLabel_ = new QLabel("System Ready", this);
    infoLabel_ = new QLabel("Total FPS: 0.0", this);
    statusLabel_->hide();
    infoLabel_->hide();
}

LiveDashboard::~LiveDashboard() {}

void LiveDashboard::setGridDimensions(int rows, int cols) {
    currentRows_ = rows;
    currentCols_ = cols;
    setupGrid(rows, cols);
}

void LiveDashboard::setCameraCount(int count) {
    if (count == numCameras_) {
        refreshCameraLabels();
        return;
    }
    
    if (count > numCameras_) {
        // Add new camera widgets
        for (int i = numCameras_; i < count; ++i) {
            QWidget* cellContainer = new QWidget(this);
            QVBoxLayout* cellLayout = new QVBoxLayout(cellContainer);
            cellLayout->setContentsMargins(0, 0, 0, 0);
            cellLayout->setSpacing(2);
            
            QString labelText = CameraConfig::getCameraLabel(i);
            CameraWidget* cam = new CameraWidget(this);
            cam->setCameraId(i);
            cam->setOverlayText(labelText);
            cam->setOverlayFont(liveViewGridTitleFont());
            
            connect(cam, &CameraWidget::doubleClicked, this, &LiveDashboard::cameraSelected);
            cellLayout->addWidget(cam, 1);
            
            cameraWidgets_.push_back(cam);
            cameraCells_.push_back(cellContainer);
        }
    } else {
        // Remove excess camera widgets
        for (int i = numCameras_ - 1; i >= count; --i) {
            gridLayout_->removeWidget(cameraCells_[i]);
            cameraCells_[i]->deleteLater(); // Deletes container and its children (CameraWidget)
            cameraCells_.pop_back();
            cameraWidgets_.pop_back();
        }
    }
    
    numCameras_ = count;
    refreshCameraLabels();
    setupGrid(currentRows_, currentCols_);
}

void LiveDashboard::setupGrid(int rows, int cols) {
    // Clear layout items (this only removes from layout, doesn't delete widgets)
    QLayoutItem *child;
    while ((child = gridLayout_->takeAt(0)) != 0) {
        delete child; 
    }

    // Delete old empty cells
    for (QWidget* emptyCell : emptyCells_) {
        emptyCell->deleteLater();
    }
    emptyCells_.clear();

    // Clear previous stretch factors
    for (int r = 0; r < 20; ++r) gridLayout_->setRowStretch(r, 0);
    for (int c = 0; c < 20; ++c) gridLayout_->setColumnStretch(c, 0);

    // Enforce dimensions using stretch
    for (int r = 0; r < rows; ++r) gridLayout_->setRowStretch(r, 1);
    for (int c = 0; c < cols; ++c) gridLayout_->setColumnStretch(c, 1);

    // Re-add camera cells by configured Camera ID, not compacted array order.
    // Example: if Camera ID 1 is deleted and Camera ID 2 remains, it stays in grid slot 2.
    int row = 0;
    int col = 0;
    int totalSlots = rows * cols;
    const std::vector<CameraInfo> cameras = CameraConfig::getCameras();
    
    for (int i = 0; i < totalSlots; ++i) {
        const int cameraIndex = cameraIndexForGridSlot(cameras, i);
        if (cameraIndex >= 0 && cameraIndex < numCameras_ && cameraIndex < static_cast<int>(cameraCells_.size())) {
            gridLayout_->addWidget(cameraCells_[cameraIndex], row, col);
        } else {
            // Create empty placeholder with border for extra/removed camera slots
            QWidget* emptyCell = new QWidget(gridContainer_);
            ThemeColors tc = CameraConfig::getThemeColors();
            emptyCell->setStyleSheet(QString(
                "background-color: %1; "
                "border: 1px solid %2;"
            ).arg(tc.bg, tc.border));
            emptyCell->setMinimumSize(50, 50); // Ensure minimum size
            gridLayout_->addWidget(emptyCell, row, col);
            emptyCells_.push_back(emptyCell);
        }
        
        col++;
        if (col >= cols) {
            col = 0;
            row++;
        }
    }
}

void LiveDashboard::updateFrame(int cameraId, const cv::Mat& frame) {
    if (cameraId >= 0 && cameraId < numCameras_ && cameraWidgets_[cameraId]) {
        cameraWidgets_[cameraId]->updateFrame(frame);
    }
}

void LiveDashboard::clearCameraWidget(int cameraId) {
    if (cameraId >= 0 && cameraId < numCameras_ && cameraWidgets_[cameraId]) {
        cameraWidgets_[cameraId]->clearFrame();
    }
}

QImage LiveDashboard::getCameraImage(int cameraId) {
    if (cameraId >= 0 && cameraId < numCameras_ && cameraWidgets_[cameraId]) {
        return cameraWidgets_[cameraId]->getImage();
    }
    return QImage();
}

void LiveDashboard::updateCameraTemperature(int cameraId, double temp, TempStatus::Status status) {
    if (cameraId >= 0 && cameraId < numCameras_ && cameraWidgets_[cameraId]) {
        cameraWidgets_[cameraId]->setTemperatureStatus(temp, status);
    }
}

void LiveDashboard::updateStatus(double fps, bool recording) {
    QString status = QString("Status: %1 | FPS: %2").arg(
        recording ? "<font color='red'>RECORDING</font>" : "Monitoring").arg(fps, 0, 'f', 1);
    statusLabel_->setText(status);
}

CameraWidget* LiveDashboard::getCameraWidget(int cameraId) {
    if (cameraId >= 0 && cameraId < numCameras_) {
        return cameraWidgets_[cameraId];
    }
    return nullptr;
}

void LiveDashboard::refreshCameraLabels() {
    const QFont overlayFont = liveViewGridTitleFont();
    for (int i = 0; i < numCameras_ && i < static_cast<int>(cameraWidgets_.size()); ++i) {
        if (!cameraWidgets_[i]) {
            continue;
        }

        cameraWidgets_[i]->setCameraId(i);
        cameraWidgets_[i]->setOverlayText(CameraConfig::getCameraLabel(i));
        cameraWidgets_[i]->setOverlayFont(overlayFont);
        cameraWidgets_[i]->update();
    }
}

void LiveDashboard::updateTheme() {
    ThemeColors tc = CameraConfig::getThemeColors();
    QString emptyStyle = QString(
        "background-color: %1; "
        "border: 1px solid %2;"
    ).arg(tc.bg, tc.border);
    
    for (QWidget* cell : emptyCells_) {
        cell->setStyleSheet(emptyStyle);
    }

    refreshCameraLabels();
}
