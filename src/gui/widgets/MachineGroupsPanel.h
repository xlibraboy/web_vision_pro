#pragma once

#include <QWidget>
#include "../CameraInfo.h"

class QTableWidget;
class QLabel;

/**
 * @brief MachineGroupsPanel - read-only registry of the 4 fixed camera groups.
 *
 * Shows every configured camera as a row (Group | ID | Camera | Location) and
 * a per-group count summary (Press-Part: N, Pre-Dryer: N, ...). The group a
 * camera belongs to is assigned on its Camera Card; this panel is a pure
 * display mirror refreshed by ConfigDialog whenever the camera set changes.
 *
 * Triggers in the OPC UA config can be wired to a group (or All); when a
 * trigger fires, only the cameras of that group are recorded.
 */
class MachineGroupsPanel : public QWidget {
    Q_OBJECT

public:
    explicit MachineGroupsPanel(QWidget* parent = nullptr);

    // Repopulate the table from the current camera set (one row per camera).
    void setCameras(const std::vector<CameraInfo>& cameras);

private:
    QTableWidget* table_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
};
