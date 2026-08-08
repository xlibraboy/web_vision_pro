#pragma once

#include <QWidget>
#include "../CameraInfo.h"

class QTableWidget;
class QLabel;

/**
 * @brief FixedIpListPanel - read-only registry of fixed IPs per camera ID.
 *
 * Shows every configured camera as a row (ID | Name | Fixed IP | Detected IP |
 * MAC) and is a pure display of the total camera registry, including the
 * running count (N / 16 cameras). The Detected IP column reflects the live
 * camera state (green when the camera is reachable). It is intentionally
 * read-only:
 *   - The fixed IPs are meant to be set once at initial install and stay
 *     stable afterwards.
 *   - Adding/removing cameras and editing the configured IP happens on the
 *     Camera Cards tab (each card keeps its own configured base IP, which is a
 *     separate function from this registry).
 *
 * The panel is refreshed by ConfigDialog whenever the camera set changes.
 */
class FixedIpListPanel : public QWidget {
    Q_OBJECT

public:
    explicit FixedIpListPanel(QWidget* parent = nullptr);

    // Repopulate the table from the current camera set (one row per camera).
    // detectedIps must be parallel to cameras and holds each camera's live
    // detected IP (or "Offline" / "Emulated").
    void setCameras(const std::vector<CameraInfo>& cameras,
                    const std::vector<QString>& detectedIps);

private:
    QTableWidget* table_ = nullptr;
    QLabel* countLabel_ = nullptr;
};
