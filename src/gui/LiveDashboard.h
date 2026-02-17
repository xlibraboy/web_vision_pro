#pragma once

#include <QWidget>
#include <QLabel>
#include <QGridLayout>
#include "widgets/CameraWidget.h"
#include <opencv2/opencv.hpp>
#include <vector>

class LiveDashboard : public QWidget {
    Q_OBJECT

public:
    explicit LiveDashboard(int numCameras = 8, QWidget *parent = nullptr);
    ~LiveDashboard();

    // Layout Config
    void setGridDimensions(int rows, int cols);

    // Update a specific camera's frame
    void updateFrame(int cameraId, const cv::Mat& frame);
    
    // Update status labels
    void updateStatus(double fps, bool recording);
    
    // Get current grid dimensions
    int getCurrentRows() const { return currentRows_; }
    int getCurrentCols() const { return currentCols_; }

signals:
    void cameraSelected(int cameraId);

private:
    void setupGrid(int rows, int cols);
    
    QGridLayout* gridLayout_;
    QWidget* gridContainer_;
    std::vector<CameraWidget*> cameraWidgets_;
    std::vector<QWidget*> cameraCells_;  // Container for camera + label
    std::vector<QWidget*> emptyCells_;   // Empty placeholder cells
    QLabel* statusLabel_;
    QLabel* infoLabel_;
    int numCameras_;
    int currentRows_ = 3;
    int currentCols_ = 3;
};
