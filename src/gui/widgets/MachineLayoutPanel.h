#pragma once

#include <QWidget>
#include <QVector>
#include <QColor>
#include <QPainter>
#include <vector>
#include "../CameraInfo.h"

class QComboBox;
class QPushButton;
struct ThemeColors;  // full definition in config/CameraConfig.h

/**
 * @brief MachineLayoutPanel - visual machine layout for System Configuration.
 *
 * Shared mm scale across all lanes:
 *  - One camera lane per machine floor (1st / 2nd / 3rd, displayed bottom-up
 *    so the top lane is the 3rd floor): every camera at the machine position
 *    assigned on its Camera Card (live card values when supplied, CameraConfig
 *    otherwise), colored by camera group, with vertical labels and side-based
 *    marker shapes (rounded rect = DRIVE SIDE in the upper sub-row,
 *    triangle = OPERATOR SIDE in the lower sub-row).
 *  - Defects lane: defects marked (and aligned) in the Analysis view, read from
 *    each event's sidecar annotations and projected onto the machine using the
 *    event speed. A header combo isolates a single event's defects or shows all
 *    recent marked events at once.
 *  - Trigger lane: the recorded trigger position (mm) of each event — the
 *    machine location where the trigger fired, e.g. the sheetbreak sensor.
 *  - MM POSITION lane: a single dedicated full-scale mm ruler (ticks + labels)
 *    below the trigger lane, so the exact position of any camera, defect or
 *    trigger can be read in one place (the floor lanes themselves stay clean).
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

    // Enable/disable the read-only reference overlay (REFERENCE DATA combo).
    void setReferenceOverlayEnabled(bool on);

    // Zoom/pan the shared mm scale (centered on the given mm / shifted by mm).
    void zoomAt(double centerMm, double factor);
    void zoomToMm(double mm);  // 1 mm window centered on a marker position
    void panBy(double deltaMm);
    void resetZoom();

protected:
    void showEvent(QShowEvent* event) override;

private:
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

    struct TriggerMark {
        QString eventText;        // human-readable event time
        QColor color;
        int mm = 0;               // recorded trigger position (sheetbreak sensor)
        QString source;           // trigger source/reason (e.g. "OPC UA")
        QString detail;           // full tooltip text
        int eventIndex = -1;      // index into eventGroups_
    };

    // ── Read-only reference overlay ────────────────────────────────────────
    // A reference set is a hardcoded snapshot of a known machine layout. It is
    // NOT wired to the system: enabling it in the header overlays its cameras,
    // speed inputs, web breaks and trigger records onto the live layout so the
    // operator can compare the configured machine against the reference.
    struct RefCamera {
        QString name;
        QString location;
        int mm = 0;
        bool operatorSide = false;  // false = DRIVE side
    };
    struct RefPoint {
        QString name;
        QString location;
        int mm = 0;
    };
    struct RefSet {
        QString name;                // e.g. "Reference 1"
        QVector<RefCamera> cameras;
        QVector<RefPoint> speedInputs;
        QVector<RefPoint> webBreaks;
        QVector<RefPoint> triggers;
    };

    struct EventGroup {
        QString label;            // "2026-02-08 15:30:00"
        QColor color;
        QVector<DefectMark> defects;
        QVector<TriggerMark> triggers;  // recorded trigger positions of the event
    };

    struct SectionRange {
        int group = CameraGroup::kUnassigned;
        double minMm = 0.0;
        double maxMm = 0.0;
        int camCount = 0;
    };

    class Canvas : public QWidget {
        friend class MachineLayoutPanel;  // panel clears selection on combo change
    public:
        explicit Canvas(MachineLayoutPanel* owner);

    protected:
        void paintEvent(QPaintEvent* event) override;
        void wheelEvent(QWheelEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        void keyPressEvent(QKeyEvent* event) override;
        void enterEvent(QEvent* event) override;
        void leaveEvent(QEvent* event) override;
        void timerEvent(QTimerEvent* event) override;

    private:
        double mmAt(int x) const;  // exact mm under a screen x (no snapping)
        void updateCursorHits();   // glow markers aligned with the cursor line
        void cancelPendingReset(); // abort a pending auto-reset (pointer back)

        QRect cameraMarkerRect(const CameraMark& cam) const;
        QRect defectMarkerRect(const DefectMark& def) const;
        QRect triggerMarkerRect(const TriggerMark& trig) const;
        QRect refCamMarkerRect(const RefCamera& r, int floorLane) const;
        QRect refSpeedMarkerRect(const RefPoint& r) const;
        QRect refBreakMarkerRect(const RefPoint& r) const;
        QRect refTriggerMarkerRect(const RefPoint& r) const;

        void drawSectionBar(QPainter& p, const ThemeColors& tc);
        void drawPaperWeb(QPainter& p, const ThemeColors& tc);
        void drawFloorLanes(QPainter& p, const ThemeColors& tc);
        void drawCameraMarkers(QPainter& p, const ThemeColors& tc);
        void drawDefectStrip(QPainter& p, const ThemeColors& tc);
        void drawTriggerStrip(QPainter& p, const ThemeColors& tc);
        void drawReferenceCameras(QPainter& p, const ThemeColors& tc);  // hollow markers on floor lanes
        void drawReferenceTriggers(QPainter& p, const ThemeColors& tc);  // hollow pins on TRIGGER lane
        void drawReferenceStrip(QPainter& p, const ThemeColors& tc);     // speed inputs + web breaks lane
        void drawMmRuler(QPainter& p, const ThemeColors& tc);
        void drawPositionCursor(QPainter& p, const ThemeColors& tc);
        void drawLegends(QPainter& p, const ThemeColors& tc);
        void drawSummary(QPainter& p, const ThemeColors& tc);
        void drawZoomIndicator(QPainter& p, const ThemeColors& tc);

        struct SectionBarSlot { int group; int x; int width; int camCount; };
        QVector<SectionBarSlot> sectionBarSlots_;

        MachineLayoutPanel* owner_;
        int hoveredCamera_ = -1;
        int hoveredDefect_ = -1;
        int hoveredTrigger_ = -1;
        int hoveredRefCam_ = -1;
        int hoveredRefSpeed_ = -1;
        int hoveredRefBreak_ = -1;
        int hoveredRefTrigger_ = -1;
        int cursorHitCamera_ = -1;  // marker aligned with the cursor line
        int cursorHitDefect_ = -1;
        int cursorHitTrigger_ = -1;
        int cursorHitRefCam_ = -1;
        int cursorHitRefTrigger_ = -1;
        int selectedCamera_ = -1;   // -1 = none
        int selectedDefect_ = -1;   // -1 = none
        int selectedTrigger_ = -1;  // -1 = none
        int floorAxisY_[CameraFloor::kCount] = {0, 0, 0};  // per-floor lane axes
        int defectLaneAxisY_ = 0;
        int triggerLaneAxisY_ = 0;  // trigger record position lane
        int refLaneAxisY_ = 0;      // reference data lane (speed inputs + web breaks)
        int mmRulerAxisY_ = 0;  // dedicated full-scale mm ruler lane

        // Vertical position cursor (tracks the exact pointer mm) + drag pan.
        bool   cursorVisible_  = false;   // pointer is over the canvas
        bool   panning_        = false;   // user is dragging empty canvas (pan)
        int    lastPanX_       = 0;       // last pointer x while panning
        double cursorMm_       = 0.0;     // current cursor position in mm
        double mmStep_         = 100.0;   // mm tick step, computed in refresh()

        // Delayed auto-reset when the pointer leaves the panel. The delay lets
        // transient synthetic leaves (tooltips, layout shifts while the Reset
        // view button appears) cancel themselves when the pointer returns,
        // so clicking a marker to zoom never auto-resets while the mouse
        // stays inside the panel.
        int  resetTimerId_ = -1;   // active delay timer, -1 = none
        bool resetPending_ = false;
    };

    void rebuildData();
    void rebuildSectionRanges();
    void loadDefects();
    void rebuildScale();
    void applyEventFilter();
    void populateEventCombo();
    void applyViewRange();   // effective minMm_/maxMm_ from fit or zoom window
    void updateZoomUi();     // Reset view button visibility
    double niceStep() const;  // mm tick step shared by lanes and the cursor
    double mmToX(double mm) const;
    static QColor groupColor(int group);
    static QString formatEventTime(const QString& timestamp);
    static RefSet buildReferenceSet(const QString& name);  // hardcoded reference data
    int floorForRefCamera(const RefCamera& r) const;  // floor lane of the nearest live camera

    std::vector<CameraInfo> cardCameras_;  // live override from camera cards
    QVector<CameraMark> cameras_;
    RefSet refSet_;                 // active reference set (hardcoded, read-only)
    bool refEnabled_ = false;       // reference overlay shown (header combo)
    QComboBox* refCombo_ = nullptr; // REFERENCE DATA: OFF / Reference 1
    QVector<SectionRange> sectionRanges_;  // per-group mm span for the section bar
    QVector<EventGroup> eventGroups_;
    QVector<DefectMark> defects_;          // filtered by the event combo
    QVector<TriggerMark> triggers_;        // filtered by the event combo
    double fitMinMm_ = 0.0;  // auto-fit range around the data (zoom baseline)
    double fitMaxMm_ = 1.0;
    double minMm_ = 0.0;     // effective visible range (fit or zoom window)
    double maxMm_ = 1.0;
    bool zoomActive_ = false;
    double zoomMinMm_ = 0.0;
    double zoomMaxMm_ = 1.0;
    int skippedNoSpeedEvents_ = 0;

    Canvas* canvas_ = nullptr;
    QComboBox* eventCombo_ = nullptr;
    QPushButton* zoomOutBtn_ = nullptr;
    QPushButton* zoomInBtn_ = nullptr;
    QPushButton* resetZoomBtn_ = nullptr;
};