#pragma once

#include <QWidget>
#include <QVector>
#include <QColor>
#include <vector>
#include "../CameraInfo.h"

class QComboBox;

/**
 * @brief MachineLayoutPanel - visual machine layout for System Configuration.
 *
 * Shared mm scale across all lanes:
 *  - One camera lane per machine floor (1st / 2nd / 3rd): every camera at the
 *    machine position assigned on its Camera Card (live card values when
 *    supplied, CameraConfig otherwise), colored by camera group, with vertical
 *    labels and side-based marker shapes (rounded rect = DRIVE SIDE,
 *    triangle = OPERATOR SIDE).
 *  - Defects lane: defects marked (and aligned) in the Analysis view, read from
 *    each event's sidecar annotations and projected onto the machine using the
 *    event speed. A header combo isolates a single event's defects or shows all
 *    recent marked events at once.
 *
 * Everything is rebuilt on refresh() / show, so the visual always mirrors the
 * actual configuration and the most recent marked events.
 */
class MachineLayoutPanel : public QWidget {
    Q_OBJECT

public:
    explicit MachineLayoutPanel(QWidget* parent = nullptr);

    // Live camera set as edited on the Camera Cards. Empty = read from
    // CameraConfig instead.
    void setCameras(const std::vector<CameraInfo>& cameras);
    void refresh();

protected:
    void showEvent(QShowEvent* event) override;

private:
    class Canvas;  // defined in MachineLayoutPanel.cpp

    struct CameraMark {
        int id = 0;               // 1-based camera id
        QString name;
        QString ip;
        QString side;             // "DRIVE SIDE" / "OPERATOR SIDE"
        int group = CameraGroup::kUnassigned;
        int floor = CameraFloor::kFirst;
        int lane = 0;             // floor lane index (0..2) for hit-testing
        int mm = 0;
        bool hasPosition = false;
        int stackIndex = -1;  // position in the left-edge "no position" stack
    };

    struct DefectMark {
        QString eventText;        // human-readable event time
        QColor color;
        QString camLabel;
        int camId = 0;
        double mm = 0.0;
        QString detail;           // full tooltip text
        int eventIndex = -1;      // index into eventGroups_
    };

    struct EventGroup {
        QString label;            // "2026-02-08 15:30:00"
        QColor color;
        QVector<DefectMark> defects;
    };

    void rebuildData();
    void loadDefects();
    void rebuildScale();
    void applyEventFilter();
    void populateEventCombo();
    double mmToX(double mm) const;
    static QColor groupColor(int group);
    static QString formatEventTime(const QString& timestamp);

    std::vector<CameraInfo> cardCameras_;  // live override from camera cards
    QVector<CameraMark> cameras_;
    QVector<EventGroup> eventGroups_;
    QVector<DefectMark> defects_;          // filtered by the event combo
    double minMm_ = 0.0;
    double maxMm_ = 1.0;
    int skippedNoSpeedEvents_ = 0;

    Canvas* canvas_ = nullptr;
    QComboBox* eventCombo_ = nullptr;
};
