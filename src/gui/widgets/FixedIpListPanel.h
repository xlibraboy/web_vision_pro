#pragma once

#include <QWidget>
#include <QVector>
#include <QHash>
#include "../CameraInfo.h"

class QTableWidget;
class QPushButton;
class QLabel;

/**
 * @brief FixedIpListPanel - Fixed IP registry for all cameras.
 *
 * Shows every configured camera as a row (ID | Name | Fixed IP | MAC) and is
 * the master list the Camera Cards tab derives its card count from:
 *   - Add Camera  -> ConfigDialog creates a matching camera card.
 *   - Delete      -> ConfigDialog removes the matching camera card.
 *   - Edit Name/IP -> ConfigDialog updates the matching camera card.
 *
 * The panel is editable only in admin mode; the fixed IPs are meant to be set
 * at initial install and stay stable afterwards (the live "Detected IP" on the
 * cards reflects the real camera state and is not managed here).
 */
class FixedIpListPanel : public QWidget {
    Q_OBJECT

public:
    explicit FixedIpListPanel(QWidget* parent = nullptr);

    // Repopulate the table from the current camera set (one row per camera).
    void setCameras(const std::vector<CameraInfo>& cameras);

    void setAdminMode(bool isAdmin);

signals:
    void addCameraRequested();
    void deleteCameraRequested(int cameraId);
    void ipEdited(int cameraId, const QString& ip);
    void nameEdited(int cameraId, const QString& name);

private slots:
    void onAddClicked();
    void onDeleteClicked();
    void onCellChanged(int row, int column);

private:
    int cameraIdAtRow(int row) const;
    QString cameraIpAtRow(int row) const;

    QTableWidget* table_ = nullptr;
    QHash<int, QString> lastValidIps_;  // cameraId -> last accepted fixed IP
    QPushButton* addBtn_ = nullptr;
    QPushButton* deleteBtn_ = nullptr;
    QLabel* countLabel_ = nullptr;
    bool populating_ = false;
    bool adminMode_ = false;
};
