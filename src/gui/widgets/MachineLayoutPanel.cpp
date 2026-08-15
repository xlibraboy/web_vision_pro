#include "MachineLayoutPanel.h"

#include "../../config/CameraConfig.h"
#include "../../core/EventDatabase.h"
#include "../../core/SpeedProfile.h"
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWheelEvent>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QLocale>
#include <QToolTip>
#include <QFontMetrics>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDateTime>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
const int kMaxMarkedEvents = 25;  // most recent marked events offered in the combo

// Display-row helpers: the machine's floors are shown bottom-up, so the top
// lane (display row 0) is the 3rd floor and the bottom lane is the 1st floor.
inline int displayRowForLane(int lane) { return CameraFloor::kCount - 1 - lane; }
inline int floorForDisplayRow(int row) { return CameraFloor::kThird - row; }
}

// ─────────────────────────────────────────────────────────────────────────────
// Canvas: painted via focused draw* helpers. Defined in MachineLayoutPanel.h
// with member definitions here.
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// MachineLayoutPanel
// ─────────────────────────────────────────────────────────────────────────────
MachineLayoutPanel::MachineLayoutPanel(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(520);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    const ThemeColors tc = CameraConfig::getThemeColors();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // Header: defect event picker
    auto* header = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(10, 8, 10, 0);
    headerLayout->setSpacing(8);
    auto* label = new QLabel(QStringLiteral("DEFECTS FROM EVENT:"), header);
    label->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: 600;").arg(tc.text));
    eventCombo_ = new QComboBox(header);
    eventCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: 6px; padding: 4px 8px; font-size: 12px; min-width: 220px; }"
        "QComboBox:hover { border-color: %4; }"
        "QComboBox::drop-down { border: none; width: 22px; }"
        "QComboBox QAbstractItemView { background-color: %1; color: %2;"
        " selection-background-color: %4; }"
    ).arg(tc.btnBg, tc.text, tc.border, tc.primary));
    headerLayout->addWidget(label);
    headerLayout->addWidget(eventCombo_);

    // Reference data overlay: a read-only reference set (not wired to the
    // system) the operator can overlay to compare the live configuration
    // against a known machine layout (see buildReferenceSet).
    auto* refLabel = new QLabel(QStringLiteral("REFERENCE DATA:"), header);
    refLabel->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: 600;").arg(tc.text));
    headerLayout->addWidget(refLabel);
    refCombo_ = new QComboBox(header);
    refCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: 6px; padding: 4px 8px; font-size: 12px; min-width: 120px; }"
        "QComboBox:hover { border-color: %4; }"
        "QComboBox::drop-down { border: none; width: 22px; }"
        "QComboBox QAbstractItemView { background-color: %1; color: %2;"
        " selection-background-color: %4; }"
    ).arg(tc.btnBg, tc.text, tc.border, tc.primary));
    refCombo_->addItem(QStringLiteral("OFF"), 0);
    refCombo_->addItem(QStringLiteral("Reference 1"), 1);
    refCombo_->addItem(QStringLiteral("Reference 2"), 2);
    refSet_ = buildReferenceSet(QStringLiteral("Reference 1"));
    refEnabled_ = false;
    headerLayout->addWidget(refCombo_);

    // Zoom controls: − / + step the shared mm scale around the view center
    // (same 2x-per-notch step as the mouse wheel), Reset view restores the
    // full fit. − / + are always enabled; Reset view only while zoomed.
    const QString zoomBtnStyle = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: 6px; padding: 0; font-size: 14px; font-weight: 700; }"
        "QPushButton:hover { border-color: %4; }"
        "QPushButton:disabled { color: %5; border-color: %3; }"
    ).arg(tc.btnBg, tc.text, tc.border, tc.primary, QColor(tc.text).lighter(155).name());

    zoomOutBtn_ = new QPushButton(tr("−"), header);
    zoomOutBtn_->setToolTip(tr("Zoom out (mouse wheel out)"));
    zoomOutBtn_->setFixedSize(28, 28);
    zoomOutBtn_->setStyleSheet(zoomBtnStyle);
    connect(zoomOutBtn_, &QPushButton::clicked, this, [this] {
        zoomAt((minMm_ + maxMm_) / 2.0, 0.5);
    });
    headerLayout->addWidget(zoomOutBtn_);

    zoomInBtn_ = new QPushButton(tr("+"), header);
    zoomInBtn_->setToolTip(tr("Zoom in (mouse wheel in)"));
    zoomInBtn_->setFixedSize(28, 28);
    zoomInBtn_->setStyleSheet(zoomBtnStyle);
    connect(zoomInBtn_, &QPushButton::clicked, this, [this] {
        zoomAt((minMm_ + maxMm_) / 2.0, 2.0);
    });
    headerLayout->addWidget(zoomInBtn_);

    // Reset view (enabled only while zoomed in)
    resetZoomBtn_ = new QPushButton(tr("Reset view"), header);
    resetZoomBtn_->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: 6px; padding: 4px 10px; font-size: 12px; }"
        "QPushButton:hover { border-color: %4; }"
        "QPushButton:disabled { color: %5; border-color: %3; }"
    ).arg(tc.btnBg, tc.text, tc.border, tc.primary, QColor(tc.text).lighter(155).name()));
    resetZoomBtn_->setEnabled(false);  // always visible, enabled only while zoomed
    connect(resetZoomBtn_, &QPushButton::clicked, this, [this] { resetZoom(); });
    headerLayout->addWidget(resetZoomBtn_);
    headerLayout->addStretch(1);
    layout->addWidget(header);

    canvas_ = new Canvas(this);
    layout->addWidget(canvas_, 1);

    connect(eventCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        applyEventFilter();
        canvas_->update();
    });

    connect(refCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        const int sel = refCombo_->currentData().toInt();
        refEnabled_ = sel > 0;
        refSet_ = buildReferenceSet(sel >= 2 ? QStringLiteral("Reference 2")
                                             : QStringLiteral("Reference 1"));
        // The fit range changes with the overlay, so reset any zoom to show
        // the full picture (reference markers can sit far beyond the live
        // camera span).
        resetZoom();
        refresh();
    });
}

void MachineLayoutPanel::setCameras(const std::vector<CameraInfo>& cameras) {
    // Skip the rebuild when the camera configuration is unchanged. The config
    // page re-feeds the same card values on every network refresh tick; a full
    // rebuild would clear the user's marker selection (the glow) and can reset
    // an active zoom, so only rebuild when something actually changed.
    bool same = cardCameras_.size() == cameras.size();
    if (same) {
        for (size_t i = 0; i < cameras.size(); ++i) {
            const CameraInfo& a = cardCameras_[i];
            const CameraInfo& b = cameras[i];
            // Compare only the configuration fields the layout renders (id,
            // name, side, position, group, floor). Runtime-only fields
            // (temperature, model, imageSize, ...) never trigger a rebuild.
            if (a.id != b.id || a.name != b.name || a.location != b.location ||
                a.side != b.side || a.machinePosition != b.machinePosition ||
                a.group != b.group || a.floor != b.floor) {
                same = false;
                break;
            }
        }
    }
    if (same) {
        return;  // no change → keep selection, zoom, and view as-is
    }
    cardCameras_ = cameras;
    refresh();
}

void MachineLayoutPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refresh();  // the visual always mirrors the current configuration
}

void MachineLayoutPanel::setReferenceOverlayEnabled(bool on) {
    if (refCombo_) {
        // OFF = index 0, Reference 1 = index 1 (the default reference set).
        const int idx = on ? 1 : 0;
        if (refCombo_->currentIndex() != idx) {
            refCombo_->setCurrentIndex(idx);  // triggers the connected refresh
        }
    }
    if (refEnabled_ != on) {
        refEnabled_ = on;
        resetZoom();
        refresh();
    }
}

void MachineLayoutPanel::refresh() {
    rebuildData();
    populateEventCombo();
    applyEventFilter();
    canvas_->mmStep_ = niceStep();  // shared tick step (lanes + cursor snap)
    updateZoomUi();
    canvas_->update();
}

void MachineLayoutPanel::rebuildData() {
    cameras_.clear();
    eventGroups_.clear();
    defects_.clear();
    triggers_.clear();
    skippedNoSpeedEvents_ = 0;

    // Cameras: live camera-card values when supplied (they may hold unsaved
    // edits), otherwise the saved CameraConfig.
    const bool useCards = !cardCameras_.empty();
    const int count = useCards ? static_cast<int>(cardCameras_.size())
                               : CameraConfig::getCameraCount();
    int noPositionStack[CameraFloor::kCount] = {0, 0, 0};
    for (int i = 0; i < count; ++i) {
        const CameraInfo info = useCards ? cardCameras_[static_cast<size_t>(i)]
                                         : CameraConfig::getCameraInfo(i);
        CameraMark mark;
        mark.id = info.id > 0 ? info.id : i + 1;
        mark.name = info.name;
        mark.ip = info.ipAddress;
        mark.side = info.side;
        mark.group = info.group;
        mark.floor = (info.floor >= CameraFloor::kFirst && info.floor <= CameraFloor::kThird)
            ? info.floor : CameraFloor::kFirst;
        mark.lane = CameraFloor::laneIndex(mark.floor);
        mark.mm = info.machinePosition;
        mark.hasPosition = info.machinePosition > 0;
        mark.stackIndex = mark.hasPosition ? -1 : noPositionStack[mark.lane]++;
        cameras_.append(mark);
    }

    loadDefects();
    rebuildScale();
    rebuildSectionRanges();

    // The data just changed. If the current zoom window no longer shows any
    // camera or defect, the zoomed view is useless (e.g. the clicked defect
    // disappeared from the event set), so fall back to the full fit instead of
    // leaving the user staring at an empty mm slice.
    if (zoomActive_) {
        bool anyVisible = false;
        for (const CameraMark& c : cameras_) {
            if (c.hasPosition && c.mm >= minMm_ && c.mm <= maxMm_) {
                anyVisible = true;
                break;
            }
        }
        if (!anyVisible) {
            for (const DefectMark& d : defects_) {
                if (d.mm >= minMm_ && d.mm <= maxMm_) {
                    anyVisible = true;
                    break;
                }
            }
        }
        if (!anyVisible) {
            for (const TriggerMark& t : triggers_) {
                if (t.mm >= minMm_ && t.mm <= maxMm_) {
                    anyVisible = true;
                    break;
                }
            }
        }
        if (!anyVisible) {
            zoomActive_ = false;
            applyViewRange();
        }
    }
}

void MachineLayoutPanel::rebuildSectionRanges() {
    sectionRanges_.clear();
    for (int g = CameraGroup::kWire; g < CameraGroup::kCount; ++g) {
        SectionRange r;
        r.group = g;
        r.minMm = std::numeric_limits<double>::max();
        r.maxMm = std::numeric_limits<double>::lowest();
        for (const CameraMark& c : cameras_) {
            if (c.group != g || !c.hasPosition) continue;
            r.minMm = std::min(r.minMm, static_cast<double>(c.mm));
            r.maxMm = std::max(r.maxMm, static_cast<double>(c.mm));
            ++r.camCount;
        }
        if (r.camCount > 0) {
            // Store both bounds; drawSectionBar handles empty groups via camCount==0.
            sectionRanges_.append(r);
        } else {
            // Keep an entry so the bar still reserves a divider slot.
            SectionRange empty;
            empty.group = g;
            sectionRanges_.append(empty);
        }
    }
}

void MachineLayoutPanel::loadDefects() {
    eventGroups_.clear();
    skippedNoSpeedEvents_ = 0;

    const std::vector<EventDatabase::EventInfo> events =
        EventDatabase::instance().getAllEvents();  // newest first

    static const QColor kPalette[] = {
        QColor(0, 229, 255), QColor(255, 153, 0), QColor(0, 204, 68),
        QColor(178, 51, 255), QColor(255, 90, 90), QColor(255, 215, 0),
        QColor(10, 132, 255), QColor(255, 128, 200)
    };
    const int paletteSize = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));

    int eventIdx = 0;
    for (const auto& ev : events) {
        if (eventIdx >= kMaxMarkedEvents) {
            break;
        }

        // Sidecar annotations live next to the event metadata:
        // <datadir>/event_<timestamp>_annotations.json
        QString dirPath;
        if (!ev.metadataPath.isEmpty()) {
            dirPath = QFileInfo(ev.metadataPath).dir().path();
        }
        if (dirPath.isEmpty() && !ev.videoPath.isEmpty()) {
            dirPath = QFileInfo(ev.videoPath).dir().path();
        }
        if (dirPath.isEmpty()) {
            continue;
        }
        const QString sidecar = QDir(dirPath).filePath(
            QString("event_%1_annotations.json").arg(ev.timestamp));

        QFile file(sidecar);
        if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject()) {
            continue;
        }
        const QJsonObject root = doc.object();
        if (!root.contains("defectMarks") || !root["defectMarks"].isObject()) {
            continue;
        }
        const QJsonObject marks = root["defectMarks"].toObject();
        const QJsonObject offsets = root.contains("cameraOffsets") && root["cameraOffsets"].isObject()
            ? root["cameraOffsets"].toObject() : QJsonObject();
        if (marks.isEmpty()) {
            continue;
        }

        ++eventIdx;

        // Projecting marked frames onto machine mm needs the event speed. A
        // recorded speed profile (per-drive anchors) is preferred; legacy
        // events fall back to the single speedValue.
        const double fallbackSpeed = ev.speedValue;
        const bool speedOk = SpeedProfile::hasValidAnchors(ev.speedAnchors)
            || (std::isfinite(fallbackSpeed) && fallbackSpeed > 0.0);
        if (!speedOk) {
            ++skippedNoSpeedEvents_;
            continue;
        }
        double fps = ev.fps > 0.0 ? ev.fps : CameraConfig::getFps();
        if (fps <= 0.0) {
            fps = 10.0;
        }
        const int sign = ev.positionDirectionSign >= 0 ? 1 : -1;

        EventGroup group;
        group.label = formatEventTime(ev.timestamp);
        group.color = kPalette[eventIdx % paletteSize];

        for (auto it = marks.begin(); it != marks.end(); ++it) {
            const int camId = it.key().mid(3).toInt();  // 1-based
            if (camId <= 0) {
                continue;
            }
            const int idx = camId - 1;
            const int offset = offsets.value(it.key()).toInt(0);

            QVector<int> frames;
            if (it.value().isArray()) {
                const QJsonArray arr = it.value().toArray();
                for (const QJsonValue& v : arr) {
                    if (v.isDouble()) {
                        frames.append(static_cast<int>(v.toDouble()));
                    }
                }
            } else if (it.value().isDouble()) {
                frames.append(static_cast<int>(it.value().toDouble()));
            }

            const double basePos =
                (idx >= 0 && idx < static_cast<int>(ev.cameraPositionsMm.size()))
                    ? ev.cameraPositionsMm[static_cast<size_t>(idx)]
                    : (idx >= 0 && idx < CameraConfig::getCameraCount()
                        ? CameraConfig::getCameraInfo(idx).machinePosition : 0.0);

            // Local speed at this camera's position (interpolated between the
            // recorded speed anchors), so the draw between drive groups is
            // reflected on the mm ruler.
            const double localSpeed = SpeedProfile::speedAt(
                static_cast<int>(basePos), ev.speedAnchors, fallbackSpeed);
            const double framesPerMm = fps * 60.0 / (localSpeed * 1000.0);
            for (int frame : frames) {
                const int master = frame - offset;  // alignment applied
                const double rel = master - ev.triggerIndex;
                const double deltaMm = rel / framesPerMm * sign;
                const double mm = basePos + deltaMm;

                DefectMark def;
                def.eventText = group.label;
                def.color = group.color;
                def.camLabel = QString("CAM-%1").arg(camId, 2, 10, QChar('0'));
                def.camId = camId;
                def.mm = mm;
                def.eventIndex = static_cast<int>(eventGroups_.size());
                def.detail = QString("Event: %1\nCamera: %2 (marked frame %3, aligned master %4)\nMachine position: %5 mm")
                    .arg(def.eventText, def.camLabel)
                    .arg(frame).arg(master)
                    .arg(mm, 0, 'f', 1);
                group.defects.append(def);
            }
        }

        // Recorded trigger position (e.g. the sheetbreak sensor): the machine
        // location where this event's trigger fired. Only events with a valid
        // position (> 0) get a marker on the TRIGGER lane.
        if (ev.triggerPositionMm > 0) {
            TriggerMark trig;
            trig.eventText = group.label;
            trig.color = group.color;
            trig.mm = ev.triggerPositionMm;
            trig.source = ev.triggerSource.trimmed().isEmpty()
                ? ev.triggerReason : ev.triggerSource;
            trig.eventIndex = static_cast<int>(eventGroups_.size());
            QString pos = QStringLiteral("%1 mm").arg(trig.mm);
            trig.detail = QString("Trigger: %1\nEvent: %2\nTrigger position: %3\nSource: %4")
                .arg(ev.triggerReason.isEmpty() ? QStringLiteral("Triggered") : ev.triggerReason,
                     trig.eventText, pos, trig.source);
            group.triggers.append(trig);
        }
        // Keep the group when it has defect marks OR a recorded trigger, so a
        // sheetbreak trigger with no marked defects still shows on the TRIGGER
        // lane and in the event picker.
        if (!group.defects.isEmpty() || !group.triggers.isEmpty()) {
            eventGroups_.append(group);
        }
    }
}

void MachineLayoutPanel::rebuildScale() {
    // Auto-fit range around the visible data; this is the zoom baseline.
    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();
    for (const CameraMark& c : cameras_) {
        if (!c.hasPosition) {
            continue;
        }
        lo = std::min(lo, static_cast<double>(c.mm));
        hi = std::max(hi, static_cast<double>(c.mm));
    }
    for (const DefectMark& d : defects_) {
        lo = std::min(lo, d.mm);
        hi = std::max(hi, d.mm);
    }
    for (const TriggerMark& t : triggers_) {
        lo = std::min(lo, static_cast<double>(t.mm));
        hi = std::max(hi, static_cast<double>(t.mm));
    }
    // Reference overlay: include its positions so enabling it shows the full
    // machine span (reference data can sit far beyond the live camera range).
    if (refEnabled_) {
        for (const RefCamera& r : refSet_.cameras) {
            lo = std::min(lo, static_cast<double>(r.mm));
            hi = std::max(hi, static_cast<double>(r.mm));
        }
        for (const RefPoint& r : refSet_.speedInputs) {
            lo = std::min(lo, static_cast<double>(r.mm));
            hi = std::max(hi, static_cast<double>(r.mm));
        }
        for (const RefPoint& r : refSet_.webBreaks) {
            lo = std::min(lo, static_cast<double>(r.mm));
            hi = std::max(hi, static_cast<double>(r.mm));
        }
        for (const RefPoint& r : refSet_.triggers) {
            lo = std::min(lo, static_cast<double>(r.mm));
            hi = std::max(hi, static_cast<double>(r.mm));
        }
    }
    if (lo > hi) {
        lo = 0.0;
        hi = 1000.0;
    }
    const double pad = (hi - lo) * 0.06;
    fitMinMm_ = lo - pad;
    fitMaxMm_ = hi + pad;
    if (fitMaxMm_ - fitMinMm_ < 1.0) {
        fitMinMm_ -= 50.0;
        fitMaxMm_ += 50.0;
    }
    applyViewRange();
}

void MachineLayoutPanel::applyViewRange() {
    // Effective visible range: the zoom window when active (clamped to the fit
    // range), otherwise the auto-fit range.
    if (zoomActive_) {
        minMm_ = std::max(fitMinMm_, zoomMinMm_);
        maxMm_ = std::min(fitMaxMm_, zoomMaxMm_);
        if (maxMm_ - minMm_ < 1.0) {
            // The zoom window no longer intersects the data range (e.g. the
            // defect/event set changed under the user). Clamp the window back
            // into the fit range instead of silently collapsing the zoom, so
            // clicking a marker to zoom never resets the view on its own.
            const double win = zoomMaxMm_ - zoomMinMm_;
            if (win >= 1.0) {
                double lo = zoomMinMm_;
                double hi = zoomMaxMm_;
                if (hi < fitMinMm_) {
                    lo = fitMinMm_;
                    hi = lo + win;
                } else if (lo > fitMaxMm_) {
                    hi = fitMaxMm_;
                    lo = hi - win;
                } else {
                    lo = std::max(fitMinMm_, lo);
                    hi = std::min(fitMaxMm_, hi);
                }
                lo = std::max(fitMinMm_, lo);
                hi = std::min(fitMaxMm_, hi);
                if (hi - lo < 1.0) {
                    // Still unresolvable — give up and show the fit range.
                    zoomActive_ = false;
                    minMm_ = fitMinMm_;
                    maxMm_ = fitMaxMm_;
                } else {
                    zoomMinMm_ = lo;
                    zoomMaxMm_ = hi;
                    minMm_ = lo;
                    maxMm_ = hi;
                }
            } else {
                zoomActive_ = false;
                minMm_ = fitMinMm_;
                maxMm_ = fitMaxMm_;
            }
        }
    } else {
        minMm_ = fitMinMm_;
        maxMm_ = fitMaxMm_;
    }
}

void MachineLayoutPanel::zoomAt(double centerMm, double factor) {
    if (!canvas_ || factor <= 0.0 || std::abs(factor - 1.0) < 1e-6) {
        return;
    }
    const double span = maxMm_ - minMm_;
    const double fitSpan = fitMaxMm_ - fitMinMm_;
    const double minSpan = std::max(1.0, fitSpan * 0.001);

    // Keep the mm under the cursor at the same screen position.
    double newLo = centerMm - (centerMm - minMm_) / factor;
    double newHi = newLo + span / factor;
    // The min-window guard only applies while zooming IN. Clicking a camera
    // or defect zooms to a tight 1 mm window (zoomToMm), which can be far
    // below minSpan; zooming OUT from there must always be allowed, otherwise
    // the user is stuck at the marker and can never zoom back out.
    if (factor > 1.0 && newHi - newLo < minSpan) {
        return;  // already at maximum zoom
    }
    newLo = std::max(fitMinMm_, newLo);
    newHi = std::min(fitMaxMm_, newHi);
    if (newLo <= fitMinMm_ && newHi >= fitMaxMm_) {
        resetZoom();  // zoomed out to (or beyond) the full fit view
        return;
    }
    if (factor > 1.0 && newHi - newLo < minSpan) {
        // Zoom-in collapsed the window while clamping — pin it around the
        // cursor. (Zooming out can't collapse the window: it only grows.)
        newLo = std::max(fitMinMm_, centerMm - minSpan / 2.0);
        newHi = std::min(fitMaxMm_, centerMm + minSpan / 2.0);
    }
    zoomActive_ = true;
    zoomMinMm_ = newLo;
    zoomMaxMm_ = newHi;
    applyViewRange();
    canvas_->mmStep_ = niceStep();
    updateZoomUi();
    canvas_->update();
}

void MachineLayoutPanel::zoomToMm(double mm) {
    // Tight 1 mm window centered on a marker's actual position (bypasses the
    // normal min-zoom guard) so its exact position can be read on the ruler.
    if (!canvas_) {
        return;
    }
    constexpr double kWindowMm = 1.0;
    double newLo = mm - kWindowMm / 2.0;
    double newHi = newLo + kWindowMm;
    if (newLo < fitMinMm_) {
        newLo = fitMinMm_;
        newHi = newLo + kWindowMm;
    }
    if (newHi > fitMaxMm_) {
        newHi = fitMaxMm_;
        newLo = newHi - kWindowMm;
    }
    newLo = std::max(fitMinMm_, newLo);
    zoomActive_ = true;
    zoomMinMm_ = newLo;
    zoomMaxMm_ = newHi;
    applyViewRange();
    canvas_->mmStep_ = niceStep();
    updateZoomUi();
    canvas_->update();
}

void MachineLayoutPanel::panBy(double deltaMm) {
    if (!canvas_ || !zoomActive_) {
        return;
    }
    const double span = zoomMaxMm_ - zoomMinMm_;
    double newLo = zoomMinMm_ - deltaMm;
    double newHi = zoomMaxMm_ - deltaMm;
    if (newLo < fitMinMm_) {
        newLo = fitMinMm_;
        newHi = newLo + span;
    }
    if (newHi > fitMaxMm_) {
        newHi = fitMaxMm_;
        newLo = newHi - span;
    }
    if (newLo <= fitMinMm_ && newHi >= fitMaxMm_) {
        resetZoom();
        return;
    }
    zoomMinMm_ = newLo;
    zoomMaxMm_ = newHi;
    applyViewRange();
    canvas_->mmStep_ = niceStep();
    updateZoomUi();
    canvas_->update();
}

void MachineLayoutPanel::resetZoom() {
    zoomActive_ = false;
    applyViewRange();
    if (canvas_) {
        canvas_->mmStep_ = niceStep();
        updateZoomUi();
        canvas_->update();
    }
}

void MachineLayoutPanel::updateZoomUi() {
    if (resetZoomBtn_) {
        resetZoomBtn_->setEnabled(zoomActive_);
    }
}

void MachineLayoutPanel::populateEventCombo() {
    if (!eventCombo_) {
        return;
    }
    const int previous = eventCombo_->currentData().toInt();
    eventCombo_->blockSignals(true);
    eventCombo_->clear();

    int total = 0;
    for (const EventGroup& g : eventGroups_) {
        total += g.defects.size();
    }
    eventCombo_->addItem(QString("All marked events  (%1 defect%2)")
        .arg(total).arg(total == 1 ? QString() : QStringLiteral("s")), -1);
    for (int i = 0; i < eventGroups_.size(); ++i) {
        const EventGroup& g = eventGroups_[i];
        eventCombo_->addItem(QString("%1  (%2 defect%3)")
            .arg(g.label).arg(g.defects.size())
            .arg(g.defects.size() == 1 ? QString() : QStringLiteral("s")), i);
    }

    const int idx = eventCombo_->findData(previous);
    eventCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
    eventCombo_->blockSignals(false);
}

void MachineLayoutPanel::applyEventFilter() {
    // A combo change invalidates the marker set, so any selection must go.
    if (canvas_) {
        canvas_->selectedCamera_ = -1;
        canvas_->selectedDefect_ = -1;
        canvas_->selectedTrigger_ = -1;
        canvas_->cursorHitCamera_ = -1;
        canvas_->cursorHitDefect_ = -1;
        canvas_->cursorHitTrigger_ = -1;
        canvas_->cursorHitRefCam_ = -1;
        canvas_->cursorHitRefTrigger_ = -1;
    }
    defects_.clear();
    triggers_.clear();
    const int sel = eventCombo_ ? eventCombo_->currentData().toInt() : -1;
    if (sel < 0) {
        for (const EventGroup& g : eventGroups_) {
            defects_ += g.defects;
            triggers_ += g.triggers;
        }
    } else if (sel >= 0 && sel < eventGroups_.size()) {
        defects_ = eventGroups_[sel].defects;
        triggers_ = eventGroups_[sel].triggers;
    }
    rebuildScale();
}

double MachineLayoutPanel::niceStep() const {
    // 1-2-5 ladder over the visible span: the tick step the MM POSITION ruler
    // lane draws and the position cursor snaps to.
    const double span = maxMm_ - minMm_;
    const double raw = span / 8.0;
    const double mag = std::pow(10.0, std::floor(std::log10(raw > 0.0 ? raw : 1.0)));
    const double norm = raw / mag;
    double step = 1.0 * mag;
    if (norm > 5.0) step = 10.0 * mag;
    else if (norm > 2.0) step = 5.0 * mag;
    else if (norm > 1.0) step = 2.0 * mag;
    return step;
}

double MachineLayoutPanel::mmToX(double mm) const {
    const double span = maxMm_ - minMm_;
    const double t = span > 0.0 ? (mm - minMm_) / span : 0.5;
    return 12.0 + t * static_cast<double>(qMax(0, width() - 24));
}

double MachineLayoutPanel::Canvas::mmAt(int x) const {
    // Exact mm under a screen x (the inverse of mmToX); no stepping so the
    // position cursor always shows the real pointer position.
    const double t = (static_cast<double>(x) - 12.0) / std::max(1, width() - 24);
    return owner_->minMm_ + t * (owner_->maxMm_ - owner_->minMm_);
}

void MachineLayoutPanel::Canvas::updateCursorHits() {
    // Glow only when the cursor line's mm actually matches a marker's position.
    // Tolerance is half a pixel converted to mm — the finest the cursor can
    // resolve, so the glow never fires for a position that visibly misses.
    cursorHitCamera_ = -1;
    cursorHitDefect_ = -1;
    cursorHitTrigger_ = -1;
    if (!cursorVisible_ && !panning_) {
        return;
    }
    const double span = owner_->maxMm_ - owner_->minMm_;
    const double mmPerPixel = span / std::max(1, width() - 24);
    const double tolMm = 0.5 * mmPerPixel;
    for (int i = 0; i < owner_->cameras_.size(); ++i) {
        const CameraMark& c = owner_->cameras_[i];
        if (c.hasPosition && std::abs(c.mm - cursorMm_) <= tolMm) {
            cursorHitCamera_ = i;
            break;
        }
    }
    if (cursorHitCamera_ < 0 && owner_->refEnabled_) {
        for (int i = 0; i < owner_->refSet_.cameras.size(); ++i) {
            if (std::abs(owner_->refSet_.cameras[i].mm - cursorMm_) <= tolMm) {
                cursorHitRefCam_ = i;
                break;
            }
        }
    }
    if (cursorHitCamera_ < 0 && cursorHitRefCam_ < 0) {
        for (int i = 0; i < owner_->defects_.size(); ++i) {
            if (std::abs(owner_->defects_[i].mm - cursorMm_) <= tolMm) {
                cursorHitDefect_ = i;
                break;
            }
        }
    }
    if (cursorHitCamera_ < 0 && cursorHitRefCam_ < 0 && cursorHitDefect_ < 0) {
        for (int i = 0; i < owner_->triggers_.size(); ++i) {
            if (std::abs(owner_->triggers_[i].mm - cursorMm_) <= tolMm) {
                cursorHitTrigger_ = i;
                break;
            }
        }
    }
    if (cursorHitCamera_ < 0 && cursorHitRefCam_ < 0 && cursorHitDefect_ < 0
        && cursorHitTrigger_ < 0 && owner_->refEnabled_) {
        for (int i = 0; i < owner_->refSet_.triggers.size(); ++i) {
            if (std::abs(owner_->refSet_.triggers[i].mm - cursorMm_) <= tolMm) {
                cursorHitRefTrigger_ = i;
                break;
            }
        }
    }
}

QColor MachineLayoutPanel::groupColor(int group) {
    switch (group) {
    case CameraGroup::kWire: return QColor(230, 200, 70);
    case CameraGroup::kPressPart: return QColor(255, 153, 0);
    case CameraGroup::kPreDryer: return QColor(10, 132, 255);
    case CameraGroup::kAfterDryer: return QColor(0, 204, 68);
    case CameraGroup::kCalenderReel: return QColor(178, 51, 255);
    default: return QColor(138, 138, 138);
    }
}

QString MachineLayoutPanel::formatEventTime(const QString& timestamp) {
    QDateTime dt = QDateTime::fromString(timestamp, "yyyyMMdd_HHmmss");
    if (!dt.isValid()) {
        dt = QDateTime::fromString(timestamp, "yyyyMMdd_HHmmss_zzz");
    }
    return dt.isValid() ? dt.toString("yyyy-MM-dd HH:mm:ss") : timestamp;
}

MachineLayoutPanel::RefSet MachineLayoutPanel::buildReferenceSet(const QString& name) {
    // Hardcoded read-only reference snapshot of a known machine layout. Not
    // wired to the system — the operator overlays it to compare positions.
    RefSet set;
    set.name = name;
    if (name == QLatin1String("Reference 1")) {
        set.cameras = {
            { QStringLiteral("Trim DS"), QStringLiteral("Trim"), 16600, false },
            { QStringLiteral("Trim OS"), QStringLiteral("Trim"), 16600, true },
            { QStringLiteral("Pickup DS"), QStringLiteral("Pick UP"), 18816, false },
            { QStringLiteral("Pickup OS"), QStringLiteral("Pick UP"), 18816, true },
            { QStringLiteral("After Press DS"), QStringLiteral("After Press"), 25195, false },
            { QStringLiteral("After Press OS"), QStringLiteral("After Press"), 25195, true },
            { QStringLiteral("Sizer DS"), QStringLiteral("Size Press"), 229258, false },
            { QStringLiteral("Calender DS"), QStringLiteral("Calender"), 240076, false },
        };
        set.speedInputs = {
            { QStringLiteral("Press speed"), QStringLiteral("Press"), 21100 },
            { QStringLiteral("Before Calendar speed"), QStringLiteral("5D Before Calender"), 240076 },
        };
        set.webBreaks = {
            { QStringLiteral("First Dryer"), QStringLiteral("First Dryer"), 30140 },
            { QStringLiteral("First Dryer"), QStringLiteral("Before Calender"), 240076 },
            { QStringLiteral("Reel Change Input"), QStringLiteral("Reel"), 250076 },
        };
        set.triggers = {
            { QStringLiteral("Operator/Manual Trigger"), QStringLiteral("—"), 16600 },
            { QStringLiteral("WIS (Web Inspection System)"), QStringLiteral("—"), 210000 },
        };
    } else if (name == QLatin1String("Reference 2")) {
        // Second reference machine: a different (mostly portable) camera
        // layout on the same physical machine. Speed inputs, web breaks and
        // trigger records are unchanged from Reference 1 — only the camera
        // list differs (FPS is carried where known, e.g. Pickup OS at 50 fps).
        set.cameras = {
            { QStringLiteral("Trim DS"), QStringLiteral("Trim"), 16600, false },
            { QStringLiteral("Trim OS"), QStringLiteral("Trim"), 16600, true },
            { QStringLiteral("Pickup DS"), QStringLiteral("Pick UP"), 18816, false },
            { QStringLiteral("Pickup OS"), QStringLiteral("Pick UP"), 18816, true, 50.0 },
            { QStringLiteral("Center Roll DS"), QStringLiteral("Press"), 32092, false },
            { QStringLiteral("Pre-Dryer DS"), QStringLiteral("Pre-Dryer"), 74201, false },
            { QStringLiteral("Sizer DS"), QStringLiteral("Sizer"), 154981, false },
            { QStringLiteral("4P DS"), QStringLiteral("Press"), 38351, false },
            { QStringLiteral("After-Size Press OS (Portable CAM)"), QStringLiteral("Size Press"), 136302, true },
            { QStringLiteral("After-Dryer DS"), QStringLiteral("After-Dryer"), 190100, false },
            { QStringLiteral("Pre-Dryer OS (Portable CAM)"), QStringLiteral("Pre-Dryer"), 200000, true },
        };
        set.speedInputs = {
            { QStringLiteral("Press speed"), QStringLiteral("Press"), 21100 },
            { QStringLiteral("Before Calendar speed"), QStringLiteral("5D Before Calender"), 240076 },
        };
        set.webBreaks = {
            { QStringLiteral("First Dryer"), QStringLiteral("First Dryer"), 30140 },
            { QStringLiteral("First Dryer"), QStringLiteral("Before Calender"), 240076 },
            { QStringLiteral("Reel Change Input"), QStringLiteral("Reel"), 250076 },
        };
        set.triggers = {
            { QStringLiteral("Operator/Manual Trigger"), QStringLiteral("—"), 16600 },
            { QStringLiteral("WIS (Web Inspection System)"), QStringLiteral("—"), 210000 },
        };
    }
    return set;
}

int MachineLayoutPanel::floorForRefCamera(const RefCamera& r) const {
    // Reference cameras carry no floor — draw them on the floor lane of the
    // live camera nearest in position, so reference markers sit beside their
    // live counterparts for a direct comparison.
    int bestFloor = CameraFloor::kFirst;
    int bestDist = std::numeric_limits<int>::max();
    for (const CameraMark& c : cameras_) {
        if (!c.hasPosition) {
            continue;
        }
        const int d = std::abs(c.mm - r.mm);
        if (d < bestDist) {
            bestDist = d;
            bestFloor = c.floor;
        }
    }
    return bestFloor;
}

// ─────────────────────────────────────────────────────────────────────────────
// Canvas painting
// ─────────────────────────────────────────────────────────────────────────────
MachineLayoutPanel::Canvas::Canvas(MachineLayoutPanel* owner)
    : QWidget(owner), owner_(owner) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);  // hover events (tooltips) need tracking on
    setFocusPolicy(Qt::StrongFocus);  // so Esc reaches keyPressEvent
}

QRect MachineLayoutPanel::Canvas::cameraMarkerRect(const CameraMark& cam) const {
    const int x = cam.hasPosition ? static_cast<int>(owner_->mmToX(cam.mm))
                                  : 12 + cam.stackIndex * 16;
    // Side-aware y-center mirroring drawCameraMarkers: DRIVE sits in the upper
    // sub-row (axisY - 18), OPERATOR in the lower (axisY + 18). Rect is
    // centered on the marker so hover/click hit-testing overlaps the drawn
    // shape (triangle apex yCenter-10..base yCenter+6; rect yCenter±8).
    const int axisY = floorAxisY_[displayRowForLane(cam.lane)];
    const bool isOperator = cam.side.compare("OPERATOR SIDE", Qt::CaseInsensitive) == 0;
    const int yCenter = isOperator ? axisY + 18 : axisY - 18;
    return QRect(x - 10, yCenter - 10, 20, 20);
}

QRect MachineLayoutPanel::Canvas::defectMarkerRect(const DefectMark& def) const {
    const int x = static_cast<int>(owner_->mmToX(def.mm));
    return QRect(x - 10, defectLaneAxisY_ - 14, 20, 14);
}

QRect MachineLayoutPanel::Canvas::triggerMarkerRect(const TriggerMark& trig) const {
    const int x = static_cast<int>(owner_->mmToX(trig.mm));
    // Mirrors the sensor-pin shape (stem below the axis, head above).
    return QRect(x - 8, triggerLaneAxisY_ - 12, 16, 20);
}

QRect MachineLayoutPanel::Canvas::refCamMarkerRect(const RefCamera& r, int floorLane) const {
    const int x = static_cast<int>(owner_->mmToX(r.mm));
    const int axisY = floorAxisY_[displayRowForLane(floorLane)];
    const int yCenter = r.operatorSide ? axisY + 18 : axisY - 18;
    return QRect(x - 10, yCenter - 10, 20, 20);
}

QRect MachineLayoutPanel::Canvas::refSpeedMarkerRect(const RefPoint& r) const {
    const int x = static_cast<int>(owner_->mmToX(r.mm));
    return QRect(x - 6, refLaneAxisY_ - 14, 12, 12);
}

QRect MachineLayoutPanel::Canvas::refBreakMarkerRect(const RefPoint& r) const {
    const int x = static_cast<int>(owner_->mmToX(r.mm));
    return QRect(x - 6, refLaneAxisY_ + 2, 12, 14);
}

QRect MachineLayoutPanel::Canvas::refTriggerMarkerRect(const RefPoint& r) const {
    const int x = static_cast<int>(owner_->mmToX(r.mm));
    return QRect(x - 6, triggerLaneAxisY_ - 12, 12, 20);
}

void MachineLayoutPanel::Canvas::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemeColors tc = CameraConfig::getThemeColors();

    painter.fillRect(rect(), QColor(tc.bg));
    painter.setPen(QColor(tc.border));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    // Shared layout numbers (lane geometry), hoisted to member accessors later.
    const int leftMargin = 12;
    const int contentBottom = owner_->refEnabled_ ? 680 : 600;
    const int dy = qMax(0, (height() - contentBottom) / 2);

    // Recompute floorAxisY_, defectLaneAxisY_, triggerLaneAxisY_, refLaneAxisY_
    // and mmRulerAxisY_ once per paint so the helpers (and the cursor) can
    // reference them. The reference data lane is only reserved when the
    // overlay is enabled; otherwise the ruler sits directly under the trigger
    // lane as before.
    const int lane1AxisY = floorAxisY_[0] = 56 + dy;
    const int lane2AxisY = floorAxisY_[1] = lane1AxisY + 88;
    const int lane3AxisY = floorAxisY_[2] = lane2AxisY + 88;
    defectLaneAxisY_     = lane3AxisY + 88;
    triggerLaneAxisY_    = defectLaneAxisY_ + 56;
    if (owner_->refEnabled_) {
        refLaneAxisY_    = triggerLaneAxisY_ + 44;
        mmRulerAxisY_    = refLaneAxisY_ + 56;
    } else {
        refLaneAxisY_    = -1;
        mmRulerAxisY_    = triggerLaneAxisY_ + 56;
    }
    Q_UNUSED(leftMargin); // each helper computes its own leftMargin

    drawSectionBar(painter, tc);
    drawPaperWeb(painter, tc);
    drawFloorLanes(painter, tc);
    drawCameraMarkers(painter, tc);
    drawReferenceCameras(painter, tc);
    drawDefectStrip(painter, tc);
    drawTriggerStrip(painter, tc);
    drawReferenceTriggers(painter, tc);
    drawReferenceStrip(painter, tc);
    drawMmRuler(painter, tc);
    drawPositionCursor(painter, tc);
    drawLegends(painter, tc);
    drawSummary(painter, tc);
    drawZoomIndicator(painter, tc);
}

// Stubs for helpers whose visual content arrives in later tasks. They must
// exist today so the dispatch above compiles and the visual stays identical.
void MachineLayoutPanel::Canvas::drawSectionBar(QPainter& p, const ThemeColors& tc) {
    Q_UNUSED(tc);
    const int leftMargin = 12;
    const int barTop = 12;
    const int barHeight = 24;
    const int fullLeft = leftMargin;
    const int fullRight = width() - leftMargin;
    const int fullWidth = fullRight - fullLeft;

    // Local cache of slot geometry. NB: not named `slots` — that identifier
    // is a Qt macro that expands to nothing (Q_SLOTS), which would break the
    // declaration.
    QVector<SectionBarSlot> barSlots;
    double totalRange = 0.0;
    for (const SectionRange& r : owner_->sectionRanges_) {
        if (r.camCount > 0) totalRange += (r.maxMm - r.minMm);
    }
    const bool useEqualWidths = (totalRange <= 0.0);
    const int minSegWidth = 36;
    const int nGroups = CameraGroup::kCount;

    int cursor = fullLeft;
    for (int i = 0; i < nGroups; ++i) {
        const SectionRange& r = owner_->sectionRanges_.value(i);
        const double span = (r.camCount > 0) ? (r.maxMm - r.minMm) : 0.0;
        int segW = useEqualWidths
            ? (fullWidth / nGroups)
            : static_cast<int>(std::lround((span / totalRange) * fullWidth));
        segW = std::max(minSegWidth, segW);
        if (cursor + segW > fullRight) segW = fullRight - cursor;
        if (segW <= 0) break;

        const QColor col = owner_->groupColor(r.group);
        if (r.camCount == 0) {
            // Empty section: 2 px divider in the group color, no fill.
            p.setPen(QPen(col, 2));
            p.drawLine(cursor, barTop, cursor, barTop + barHeight);
        } else {
            p.fillRect(cursor, barTop, segW, barHeight, col);
            if (segW >= minSegWidth) {
                p.setPen(QColor("#0E1116"));
                QFont f = p.font(); f.setPixelSize(10); f.setBold(true); p.setFont(f);
                p.drawText(QRect(cursor, barTop, segW, barHeight),
                           Qt::AlignCenter, CameraGroup::name(r.group).toUpper());
            }
        }
        barSlots.append({ r.group, cursor, segW, r.camCount });
        cursor += segW;
    }
    p.setPen(QColor(tc.border));
    p.drawRect(QRect(fullLeft, barTop, fullWidth, barHeight));
    sectionBarSlots_ = barSlots;
}

void MachineLayoutPanel::Canvas::drawPaperWeb(QPainter& p, const ThemeColors& tc) {
    Q_UNUSED(tc);
    const int leftMargin = 12;
    const int stripTop = 40;   // directly below the section bar (bar bottom = 36)
    const int stripHeight = 8;

    // Paper-tan fill
    p.fillRect(leftMargin, stripTop, width() - 2 * leftMargin, stripHeight,
               QColor("#F5F1E6"));
    p.setPen(QColor("#D6CFB7"));
    p.drawLine(leftMargin, stripTop, width() - leftMargin, stripTop);
    p.drawLine(leftMargin, stripTop + stripHeight,
               width() - leftMargin, stripTop + stripHeight);

    // Direction arrow + label, right-aligned
    QFont f = p.font();
    f.setPixelSize(9);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor("#8A8267"));
    const QString arrow = QStringLiteral("→ DRIVE → OPERATOR →");
    const int textW = p.fontMetrics().horizontalAdvance(arrow);
    p.drawText(width() - leftMargin - textW - 4,
               stripTop + stripHeight - 1,
               arrow);
}
void MachineLayoutPanel::Canvas::drawPositionCursor(QPainter& p, const ThemeColors& tc) {
    if (!cursorVisible_ && !panning_) {
        return;
    }
    const int leftMargin = 12;
    const int x = static_cast<int>(owner_->mmToX(cursorMm_));

    // Vertical guide line: section bar (y=12) down to just above the summary.
    p.setOpacity(0.75);
    p.setPen(QPen(QColor(tc.primary), 1));
    p.drawLine(x, 12, x, height() - 60);
    p.setOpacity(1.0);

    // Drag handle: 6x6 square sitting on the section bar.
    p.fillRect(QRect(x - 3, 22, 6, 6), QColor(tc.primary));

    // Live "<mm> mm · <section>" pill above the section bar.
    QString section = QStringLiteral("—");
    for (const SectionRange& r : owner_->sectionRanges_) {
        if (r.camCount > 0 && cursorMm_ >= r.minMm && cursorMm_ <= r.maxMm) {
            section = CameraGroup::name(r.group);
            break;
        }
    }
    const QString text = QString("%1 mm · %2").arg(QLocale().toString(qRound(cursorMm_))).arg(section);

    QFont f = p.font();
    f.setPixelSize(10);
    f.setBold(true);
    p.setFont(f);
    const int textW = p.fontMetrics().horizontalAdvance(text);
    const int pillW = textW + 12;
    const int pillH = 14;
    int pillX = x - pillW / 2;
    pillX = std::max(leftMargin, std::min(pillX, width() - leftMargin - pillW));

    p.setPen(QPen(QColor(tc.primary), 1));
    p.setBrush(QColor("#1C2128"));
    p.drawRoundedRect(QRect(pillX, 0, pillW, pillH), 3, 3);
    p.setPen(QColor(tc.primary));
    p.drawText(QRect(pillX, 0, pillW, pillH), Qt::AlignCenter, text);
}

void MachineLayoutPanel::Canvas::drawFloorLanes(QPainter& painter, const ThemeColors& tc) {
    const int leftMargin = 12;

    // Lane geometry (shared mm scale across all lanes). One lane per machine
    // floor so every camera position is visible; the defect lane sits below the
    // camera lanes. The whole block is vertically centered in the canvas so
    // there's no dead space at the bottom.
    constexpr int kLanePitch = 88;  // title row + axis + markers
    const int contentBottom = owner_->refEnabled_ ? 680 : 600;  // matches paintEvent
    const int dy = qMax(0, (height() - contentBottom) / 2);
    const int lane1TitleY = 8 + dy;
    const int lane1AxisY = floorAxisY_[0];
    const int lane2TitleY = lane1TitleY + kLanePitch;
    const int lane2AxisY = floorAxisY_[1];
    const int lane3TitleY = lane2TitleY + kLanePitch;
    const int lane3AxisY = floorAxisY_[2];
    const int defectTitleY = lane3TitleY + kLanePitch;
    const int defectLaneAxisY = defectLaneAxisY_;

    // Titles (floor name only; floors display bottom-up so the top lane is
    // the 3rd floor)
    QFont titleFont = painter.font();
    titleFont.setPixelSize(13);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(QColor(tc.text));
    painter.drawText(leftMargin, lane1TitleY + 14, CameraFloor::name(floorForDisplayRow(0)).toUpper());
    painter.drawText(leftMargin, lane2TitleY + 14, CameraFloor::name(floorForDisplayRow(1)).toUpper());
    painter.drawText(leftMargin, lane3TitleY + 14, CameraFloor::name(floorForDisplayRow(2)).toUpper());
    painter.drawText(leftMargin, defectTitleY + 14, QString("MARKED & ALIGNED DEFECTS"));

    // Per-lane vertical bounds: split each floor lane into an upper (drive)
    // and lower (operator) sub-row. Axis line is centered; 32px above and 32px below.
    struct SubRow { int upperTop, upperBottom, lowerTop, lowerBottom; };
    SubRow rows[CameraFloor::kCount];
    constexpr int kSubRowHeight = 32;
    for (int f = 0; f < CameraFloor::kCount; ++f) {
        const int axisY = floorAxisY_[f];
        rows[f].upperTop       = axisY - kSubRowHeight;
        rows[f].upperBottom    = axisY;
        rows[f].lowerTop       = axisY;
        rows[f].lowerBottom    = axisY + kSubRowHeight;
    }

    // Sub-row background fills — must precede the axis-line draw so the line
    // sits cleanly on top of the tints.
    const QColor opTint(0x2A, 0x32, 0x39);     // lower sub-row (operator side)
    const QColor driveTint(0x1F, 0x24, 0x29);  // upper sub-row (drive side)
    for (int f = 0; f < CameraFloor::kCount; ++f) {
        painter.fillRect(leftMargin, rows[f].upperTop,
                         width() - 2 * leftMargin,
                         rows[f].upperBottom - rows[f].upperTop, driveTint);
        painter.fillRect(leftMargin, rows[f].lowerTop,
                         width() - 2 * leftMargin,
                         rows[f].lowerBottom - rows[f].lowerTop, opTint);
    }

    // Plain axis lines on every floor lane (markers sit on them). The mm
    // tick scale now lives on the dedicated MM POSITION lane below the defect
    // lane (drawMmRuler), so the floor lanes stay clean and uncluttered.
    const int laneAxes[CameraFloor::kCount] = {lane1AxisY, lane2AxisY, lane3AxisY};
    for (int f = 0; f < CameraFloor::kCount; ++f) {
        painter.setPen(QPen(QColor(tc.border), 1));
        painter.drawLine(leftMargin, laneAxes[f], width() - leftMargin, laneAxes[f]);
    }

    // Per-sub-row side labels (left edge). Bold 9px, muted color.
    // The muted text token is project-wide (spec: MachineLayout polish
    // design, "Color tokens" section); kept inline because ThemeColors
    // has no `muted` field and adding one is out of scope here.
    QFont sideLabelFont = painter.font();
    sideLabelFont.setPixelSize(9);
    sideLabelFont.setBold(true);
    painter.setFont(sideLabelFont);
    painter.setPen(QColor(QStringLiteral("#8B949E")));
    for (int f = 0; f < CameraFloor::kCount; ++f) {
        painter.drawText(leftMargin, rows[f].upperTop + 11, QStringLiteral("DRIVE SIDE"));     // upper sub-row
        painter.drawText(leftMargin, rows[f].lowerTop + 11, QStringLiteral("OPERATOR SIDE"));  // lower sub-row
    }

    painter.setPen(QPen(QColor(tc.border), 1));
    painter.drawLine(leftMargin, defectLaneAxisY, width() - leftMargin, defectLaneAxisY);
}

void MachineLayoutPanel::Canvas::drawMmRuler(QPainter& painter, const ThemeColors& tc) {
    const int leftMargin = 12;
    const int axisY = mmRulerAxisY_;

    // Dedicated MM POSITION lane: a single full-scale ruler (ticks + labels)
    // shared by every floor and defect lane above it, so the exact mm of any
    // camera or defect can be read in one place.
    QFont titleFont = painter.font();
    titleFont.setPixelSize(13);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(QColor(tc.text));
    painter.drawText(leftMargin, axisY - 24, QStringLiteral("MM POSITION"));

    // Axis line
    painter.setPen(QPen(QColor(tc.border), 1));
    painter.drawLine(leftMargin, axisY, width() - leftMargin, axisY);

    // Full-scale ticks + labels (nice step, shared with the cursor snap;
    // guarded in case refresh() has not run yet).
    const double step = mmStep_ > 0.0 ? mmStep_ : 100.0;
    const double pxPerTick = (width() - 2 * leftMargin) * step
                           / std::max(1.0, owner_->maxMm_ - owner_->minMm_);
    const int labelEvery = std::max(1, static_cast<int>(std::ceil(70.0 / std::max(1.0, pxPerTick))));

    QFont tickFont = painter.font();
    tickFont.setPixelSize(9);
    painter.setFont(tickFont);
    const double tickStart = std::ceil(owner_->minMm_ / step) * step;
    int tickCount = 0;
    for (double mm = tickStart; mm <= owner_->maxMm_ + step * 0.5; mm += step) {
        const int x = static_cast<int>(owner_->mmToX(mm));
        painter.setPen(QPen(QColor(tc.border), 1));
        painter.drawLine(x, axisY - 4, x, axisY + 4);
        if (tickCount++ % labelEvery == 0) {
            painter.setPen(QColor(tc.text));
            const QString label = (step < 1.0)
                ? QString::number(mm, 'f', 1)
                : QString::number(mm, 'f', 0);
            painter.drawText(QRect(x - 40, axisY + 6, 80, 14),
                             Qt::AlignHCenter | Qt::AlignTop, label);
        }
    }
}

void MachineLayoutPanel::Canvas::drawCameraMarkers(QPainter& painter, const ThemeColors& tc) {
    const int leftMargin = 12;

    // ── Camera markers, one lane per floor ──
    QFont labelFont = painter.font();
    labelFont.setPixelSize(10);
    painter.setFont(labelFont);
    const QVector<CameraMark>& cameras = owner_->cameras_;
    for (int i = 0; i < cameras.size(); ++i) {
        const CameraMark& cam = cameras[i];
        const int axisY = floorAxisY_[displayRowForLane(cam.lane)];
        const int x = cam.hasPosition ? static_cast<int>(owner_->mmToX(cam.mm))
                                      : 12 + cam.stackIndex * 16;
        const QColor col = owner_->groupColor(cam.group);
        const bool isOperator = cam.side.compare("OPERATOR SIDE", Qt::CaseInsensitive) == 0;
        // Side-aware sub-row: DRIVE sits in the upper sub-row, OPERATOR in the
        // lower one (aligned with the sub-row tints in drawFloorLanes).
        const int yCenter = isOperator ? axisY + 18 : axisY - 18;

        // Faint dashed guide from the section-bar bottom (36) down to the marker.
        if (cam.hasPosition) {
            const QColor guide(col.red(), col.green(), col.blue(), 76);
            painter.setPen(QPen(guide, 1, Qt::DashLine));
            painter.drawLine(x, 36, x, yCenter);
        }

        // Selection state: dim every marker but the selected one; the selected
        // marker gets an accent border. Hover highlight only applies when no
        // selection is active.
        const bool glow = (i == hoveredCamera_ || i == cursorHitCamera_);
        QColor brushCol = cam.hasPosition ? col : QColor(col.red(), col.green(), col.blue(), 90);
        QPen markerPen = QPen(glow ? Qt::white : QColor(tc.border), 1.5);
        if (selectedCamera_ >= 0) {
            if (i == selectedCamera_) {
                markerPen = QPen(QColor(tc.primary), 2);  // accent border
            } else {
                brushCol = QColor(col.red(), col.green(), col.blue(), 89);  // 35% dim
                markerPen = QPen(QColor(tc.border), 1.5);
            }
        }
        painter.setPen(markerPen);
        painter.setBrush(brushCol);
        if (isOperator) {
            // Upward triangle = OPERATOR SIDE
            QPainterPath tri;
            tri.moveTo(x, yCenter - 10);
            tri.lineTo(x + 9, yCenter + 6);
            tri.lineTo(x - 9, yCenter + 6);
            tri.closeSubpath();
            painter.drawPath(tri);
        } else {
            // Rounded rect = DRIVE SIDE (and default)
            painter.drawRoundedRect(QRect(x - 9, yCenter - 8, 18, 18), 4, 4);
        }

        // Vertical camera label (reads bottom-to-top) so tightly spaced
        // cameras never overlap on the mm line.
        painter.save();
        painter.translate(x, yCenter - 12);
        painter.rotate(-90);
        painter.setPen(glow ? Qt::white : QColor(tc.text));
        painter.drawText(QRect(0, -8, 50, 16), Qt::AlignLeft | Qt::AlignVCenter,
                         QString("CAM-%1").arg(cam.id, 2, 10, QChar('0')));
        painter.restore();
    }
    const int lane3AxisY = floorAxisY_[CameraFloor::kCount - 1];
    for (const CameraMark& c : cameras) {
        if (!c.hasPosition) {
            painter.setPen(QColor(255, 90, 90));
            painter.drawText(QRect(leftMargin, lane3AxisY + 26, width() - 2 * leftMargin, 14),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             "Hollow left-edge markers = cameras without a machine position "
                             "(set it on the Camera Card).");
            break;
        }
    }
}

void MachineLayoutPanel::Canvas::drawDefectStrip(QPainter& painter, const ThemeColors& tc) {
    const int leftMargin = 12;
    const int defectLaneAxisY = defectLaneAxisY_;

    // ── Defect markers ──
    const QVector<DefectMark>& defects = owner_->defects_;
    if (defects.isEmpty()) {
        QFont noteFont = painter.font();
        noteFont.setPixelSize(11);
        painter.setFont(noteFont);
        painter.setPen(QColor(tc.text));
        const bool singleEvent = owner_->eventCombo_ && owner_->eventCombo_->currentData().toInt() >= 0;
        // Note sits above the defect axis (between the lane title and the
        // axis) so it never collides with the MM POSITION ruler below.
        painter.drawText(QRect(leftMargin, defectLaneAxisY - 24, width() - 2 * leftMargin, 14),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         singleEvent
                             ? QStringLiteral("This event has no positionable defects.")
                             : QStringLiteral("No marked & aligned defects yet — mark the same defect on at "
                                              "least two cameras in the Analysis view and press Align."));
    }
    for (int i = 0; i < defects.size(); ++i) {
        const DefectMark& def = defects[i];
        const int x = static_cast<int>(owner_->mmToX(def.mm));
        // Selection state: dim every defect but the selected one; the selected
        // defect gets an accent border. Hover highlight only applies when no
        // selection is active.
        QColor fill = def.color;
        QPen diamondPen = QPen((i == hoveredDefect_ || i == cursorHitDefect_)
                                   ? Qt::white : QColor(tc.border), 1.5);
        if (selectedDefect_ >= 0) {
            if (i == selectedDefect_) {
                diamondPen = QPen(QColor(tc.primary), 2);  // accent border
            } else {
                fill = QColor(def.color.red(), def.color.green(), def.color.blue(), 89);  // 35% dim
                diamondPen = QPen(QColor(tc.border), 1.5);
            }
        }
        painter.setPen(diamondPen);
        painter.setBrush(fill);
        QPainterPath diamond;
        diamond.moveTo(x, defectLaneAxisY - 12);
        diamond.lineTo(x + 8, defectLaneAxisY - 6);
        diamond.lineTo(x, defectLaneAxisY);
        diamond.lineTo(x - 8, defectLaneAxisY - 6);
        diamond.closeSubpath();
        painter.drawPath(diamond);
    }
}

void MachineLayoutPanel::Canvas::drawTriggerStrip(QPainter& painter, const ThemeColors& tc) {
    const int leftMargin = 12;
    const int axisY = triggerLaneAxisY_;

    // ── Trigger record position ──
    QFont titleFont = painter.font();
    titleFont.setPixelSize(13);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(QColor(tc.text));
    painter.drawText(leftMargin, axisY - 24, QStringLiteral("TRIGGER RECORD POSITION"));

    // Axis line (the sheetbreak sensor's position reads off the MM POSITION
    // ruler below, exactly like the camera and defect lanes).
    painter.setPen(QPen(QColor(tc.border), 1));
    painter.drawLine(leftMargin, axisY, width() - leftMargin, axisY);

    // Sensor-pin markers: a short stem below the axis + a filled head above,
    // colored per event (same palette as its defect diamonds).
    const QVector<TriggerMark>& triggers = owner_->triggers_;
    if (triggers.isEmpty()) {
        QFont noteFont = painter.font();
        noteFont.setPixelSize(11);
        painter.setFont(noteFont);
        painter.setPen(QColor(tc.text));
        const bool singleEvent = owner_->eventCombo_ && owner_->eventCombo_->currentData().toInt() >= 0;
        painter.drawText(QRect(leftMargin, axisY - 24, width() - 2 * leftMargin, 14),
                         Qt::AlignRight | Qt::AlignVCenter,
                         singleEvent
                             ? QStringLiteral("No recorded trigger position for this event.")
                             : QStringLiteral("No trigger positions recorded — set the sensor position on the trigger tag."));
    }
    for (int i = 0; i < triggers.size(); ++i) {
        const TriggerMark& trig = triggers[i];
        const int x = static_cast<int>(owner_->mmToX(trig.mm));
        // Selection dims every trigger but the selected one; hover highlights.
        QColor fill = trig.color;
        QPen pinPen = QPen((i == hoveredTrigger_ || i == cursorHitTrigger_)
                               ? Qt::white : QColor(tc.border), 1.5);
        if (selectedTrigger_ >= 0) {
            if (i == selectedTrigger_) {
                pinPen = QPen(QColor(tc.primary), 2);  // accent border
            } else {
                fill = QColor(trig.color.red(), trig.color.green(), trig.color.blue(), 89);
                pinPen = QPen(QColor(tc.border), 1.5);
            }
        }
        painter.setPen(pinPen);
        painter.drawLine(x, axisY, x, axisY + 7);  // stem below the axis
        painter.setBrush(fill);
        painter.drawEllipse(QRect(x - 4, axisY - 10, 8, 8));  // sensor head
    }
}

void MachineLayoutPanel::Canvas::drawReferenceCameras(QPainter& painter, const ThemeColors& tc) {
    if (!owner_->refEnabled_ || owner_->refSet_.cameras.isEmpty()) {
        return;
    }
    // Hollow (dashed outline, no fill) markers on the floor lane of the
    // nearest live camera, side-aware like the live markers (DRIVE upper
    // sub-row, OPERATOR lower). Clearly distinct from the solid live cameras.
    const RefSet& ref = owner_->refSet_;
    for (int i = 0; i < ref.cameras.size(); ++i) {
        const RefCamera& r = ref.cameras[i];
        const int lane = owner_->floorForRefCamera(r);
        const int axisY = floorAxisY_[displayRowForLane(lane)];
        const int x = static_cast<int>(owner_->mmToX(r.mm));
        const int yCenter = r.operatorSide ? axisY + 18 : axisY - 18;
        const bool glow = (i == hoveredRefCam_ || i == cursorHitRefCam_);

        // Faint dashed guide from the section-bar bottom down to the marker.
        painter.setPen(QPen(QColor(255, 255, 255, 36), 1, Qt::DashLine));
        painter.drawLine(x, 36, x, yCenter);

        const QColor outlineCol = glow ? Qt::white : QColor("#9AA4AF");
        painter.setPen(QPen(outlineCol, 1.5, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        if (r.operatorSide) {
            QPainterPath tri;
            tri.moveTo(x, yCenter - 10);
            tri.lineTo(x + 9, yCenter + 6);
            tri.lineTo(x - 9, yCenter + 6);
            tri.closeSubpath();
            painter.drawPath(tri);
        } else {
            painter.drawRoundedRect(QRect(x - 9, yCenter - 8, 18, 18), 4, 4);
        }

        // Vertical reference name (rotated), muted gray.
        painter.save();
        painter.translate(x, yCenter - 12);
        painter.rotate(-90);
        painter.setPen(glow ? Qt::white : QColor("#9AA4AF"));
        painter.drawText(QRect(0, -8, 60, 16), Qt::AlignLeft | Qt::AlignVCenter, r.name);
        painter.restore();
    }
}

void MachineLayoutPanel::Canvas::drawReferenceTriggers(QPainter& painter, const ThemeColors& tc) {
    if (!owner_->refEnabled_ || owner_->refSet_.triggers.isEmpty()) {
        return;
    }
    // Hollow dashed pins on the TRIGGER lane, distinct from the solid colored
    // per-event trigger pins.
    const RefSet& ref = owner_->refSet_;
    const int axisY = triggerLaneAxisY_;
    for (int i = 0; i < ref.triggers.size(); ++i) {
        const RefPoint& r = ref.triggers[i];
        const int x = static_cast<int>(owner_->mmToX(r.mm));
        const bool glow = (i == hoveredRefTrigger_ || i == cursorHitRefTrigger_);
        painter.setPen(QPen(glow ? Qt::white : QColor("#9AA4AF"), 1.5, Qt::DashLine));
        painter.drawLine(x, axisY, x, axisY + 7);  // stem below the axis
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRect(x - 4, axisY - 10, 8, 8));  // hollow sensor head
    }
}

void MachineLayoutPanel::Canvas::drawReferenceStrip(QPainter& painter, const ThemeColors& tc) {
    if (!owner_->refEnabled_ || refLaneAxisY_ < 0) {
        return;
    }
    const int leftMargin = 12;
    const int axisY = refLaneAxisY_;
    const RefSet& ref = owner_->refSet_;

    // Compact lane between TRIGGER and MM POSITION: reference speed inputs
    // (cyan dots above the axis) and web break sensors (red × below the axis).
    QFont titleFont = painter.font();
    titleFont.setPixelSize(13);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(QColor(tc.text));
    painter.drawText(leftMargin, axisY - 20, QStringLiteral("REFERENCE DATA"));

    painter.setPen(QPen(QColor(tc.border), 1));
    painter.drawLine(leftMargin, axisY, width() - leftMargin, axisY);

    for (int i = 0; i < ref.speedInputs.size(); ++i) {
        const RefPoint& r = ref.speedInputs[i];
        const int x = static_cast<int>(owner_->mmToX(r.mm));
        const bool glow = (i == hoveredRefSpeed_);
        painter.setPen(QPen(glow ? Qt::white : QColor("#00E5FF"), 1.5));
        painter.setBrush(QColor(0, 229, 255, 70));
        painter.drawEllipse(QRect(x - 4, axisY - 13, 8, 8));
        painter.setBrush(Qt::NoBrush);
    }
    for (int i = 0; i < ref.webBreaks.size(); ++i) {
        const RefPoint& r = ref.webBreaks[i];
        const int x = static_cast<int>(owner_->mmToX(r.mm));
        const bool glow = (i == hoveredRefBreak_);
        painter.setPen(QPen(glow ? Qt::white : QColor("#FF5A5A"), 1.6));
        painter.drawLine(x - 5, axisY + 4, x + 5, axisY + 14);
        painter.drawLine(x - 5, axisY + 14, x + 5, axisY + 4);
    }
}

void MachineLayoutPanel::Canvas::drawLegends(QPainter& painter, const ThemeColors& tc) {
    const int leftMargin = 12;

    // ── Legend ── (below the dedicated MM POSITION ruler lane)
    int ly = mmRulerAxisY_ + 48;
    QFont legendFont = painter.font();
    legendFont.setPixelSize(10);
    legendFont.setBold(true);
    painter.setFont(legendFont);
    painter.setPen(QColor(tc.text));
    painter.drawText(leftMargin, ly, QStringLiteral("CAMERA GROUPS:"));
    int lx = leftMargin + 110;
    for (int g = CameraGroup::kUnassigned; g < CameraGroup::kCount; ++g) {
        const QColor col = owner_->groupColor(g);
        painter.setPen(Qt::NoPen);
        painter.setBrush(col);
        painter.drawRect(lx, ly - 9, 12, 12);
        painter.setPen(QColor(tc.text));
        const QString name = CameraGroup::name(g);
        painter.drawText(lx + 16, ly, name);
        lx += 16 + painter.fontMetrics().horizontalAdvance(name) + 22;
    }

    // ── Side split legend ──
    const int sideY = ly + 22;  // one line below the camera-groups legend
    QFont sideFont = painter.font();
    sideFont.setPixelSize(10);
    sideFont.setBold(true);
    painter.setFont(sideFont);
    painter.setPen(QColor(tc.text));
    painter.drawText(leftMargin, sideY, QStringLiteral("SIDE SPLIT:"));

    // Rounded rect swatch = DRIVE
    const int swX = leftMargin + 110;
    painter.setPen(QPen(QColor(tc.border), 1));
    painter.setBrush(QColor("#8B949E"));
    painter.drawRoundedRect(QRect(swX, sideY - 9, 12, 12), 3, 3);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QColor(tc.text));
    painter.drawText(swX + 16, sideY, QStringLiteral("DRIVE SIDE  (upper sub-row)"));
    const int driveW = painter.fontMetrics().horizontalAdvance("DRIVE SIDE  (upper sub-row)");

    // Triangle swatch = OPERATOR
    const int opX = swX + driveW + 28;
    painter.setPen(QPen(QColor(tc.border), 1));
    painter.setBrush(QColor("#8B949E"));
    QPainterPath tri;
    tri.moveTo(opX + 6, sideY - 9);
    tri.lineTo(opX + 12, sideY + 3);
    tri.lineTo(opX, sideY + 3);
    tri.closeSubpath();
    painter.drawPath(tri);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QColor(tc.text));
    painter.drawText(opX + 18, sideY, QStringLiteral("OPERATOR SIDE  (lower sub-row)"));

    // ── Trigger marker legend ──
    const int trigY = sideY + 22;
    painter.setPen(QColor(tc.text));
    painter.drawText(leftMargin, trigY, QStringLiteral("TRIGGER:"));
    const int trigX = leftMargin + 110;
    painter.setPen(QPen(QColor(tc.border), 1));
    painter.drawLine(trigX + 6, trigY, trigX + 6, trigY + 6);
    painter.setBrush(QColor("#8B949E"));
    painter.drawEllipse(QRect(trigX + 2, trigY - 9, 8, 8));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QColor(tc.text));
    painter.drawText(trigX + 16, trigY,
        QStringLiteral("SENSOR PIN  (sheetbreak trigger position, colored per event)"));

    // ── Reference overlay legend (only while a reference set is enabled) ──
    if (owner_->refEnabled_) {
        const int refY = trigY + 22;
        painter.setPen(QColor(tc.text));
        painter.drawText(leftMargin, refY, QStringLiteral("REFERENCE (%1):").arg(owner_->refSet_.name.toUpper()));
        const int refX = leftMargin + 110;
        // hollow square = reference camera
        painter.setPen(QPen(QColor("#9AA4AF"), 1.5, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRect(refX, refY - 9, 12, 12), 3, 3);
        painter.setPen(QColor(tc.text));
        painter.drawText(refX + 18, refY, QStringLiteral("CAM"));
        int rx = refX + 18 + painter.fontMetrics().horizontalAdvance(QStringLiteral("CAM")) + 26;
        // cyan dot = speed input
        painter.setPen(QPen(QColor("#00E5FF"), 1.5));
        painter.setBrush(QColor(0, 229, 255, 70));
        painter.drawEllipse(QRect(rx, refY - 8, 8, 8));
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QColor(tc.text));
        painter.drawText(rx + 14, refY, QStringLiteral("SPEED IN"));
        rx += 14 + painter.fontMetrics().horizontalAdvance(QStringLiteral("SPEED IN")) + 26;
        // red × = web break
        painter.setPen(QPen(QColor("#FF5A5A"), 1.6));
        painter.drawLine(rx + 4, refY - 8, rx + 12, refY);
        painter.drawLine(rx + 4, refY, rx + 12, refY - 8);
        painter.setPen(QColor(tc.text));
        painter.drawText(rx + 18, refY, QStringLiteral("WEB BREAK"));
        rx += 18 + painter.fontMetrics().horizontalAdvance(QStringLiteral("WEB BREAK")) + 26;
        // dashed pin = trigger record
        painter.setPen(QPen(QColor("#9AA4AF"), 1.5, Qt::DashLine));
        painter.drawLine(rx + 4, refY, rx + 4, refY + 6);
        painter.drawEllipse(QRect(rx + 1, refY - 9, 8, 8));
        painter.setPen(QColor(tc.text));
        painter.drawText(rx + 14, refY,
            QStringLiteral("TRIGGER RECORD  (read-only overlay, not wired to the system)"));
    }
}

void MachineLayoutPanel::Canvas::drawSummary(QPainter& painter, const ThemeColors& tc) {
    const int leftMargin = 12;
    const QVector<CameraMark>& cameras = owner_->cameras_;
    const QVector<DefectMark>& defects = owner_->defects_;

    int ly = mmRulerAxisY_ + 48;

    // ── Summary ── (starts below the legend rows so the two blocks never
    // overlap; an extra row is reserved when the reference overlay is on.)
    QFont sumFont = painter.font();
    sumFont.setPixelSize(11);
    sumFont.setBold(false);
    painter.setFont(sumFont);
    painter.setPen(QColor(tc.text));
    int sy = ly + (owner_->refEnabled_ ? 90 : 66);
    int camerasWithPosition = 0;
    int floorCounts[CameraFloor::kCount] = {0, 0, 0};
    for (const CameraMark& c : cameras) {
        if (c.hasPosition) {
            ++camerasWithPosition;
        }
        floorCounts[c.lane]++;
    }
    painter.drawText(leftMargin, sy, QString("Cameras: %1 (%2 with a machine position) — 1st: %3 · 2nd: %4 · 3rd: %5")
        .arg(cameras.size()).arg(camerasWithPosition)
        .arg(floorCounts[0]).arg(floorCounts[1]).arg(floorCounts[2]));
    sy += 16;

    const bool singleEvent = owner_->eventCombo_ && owner_->eventCombo_->currentData().toInt() >= 0;
    QString defectLine;
    if (singleEvent) {
        const int sel = owner_->eventCombo_->currentData().toInt();
        const QString label = (sel >= 0 && sel < owner_->eventGroups_.size())
            ? owner_->eventGroups_[sel].label : QString();
        defectLine = QString("Defects: %1 mark(s) from %2")
            .arg(defects.size()).arg(label);
    } else {
        defectLine = QString("Defects: %1 mark(s) from %2 marked event(s)")
            .arg(defects.size()).arg(owner_->eventGroups_.size());
        if (owner_->skippedNoSpeedEvents_ > 0) {
            defectLine += QString(" · %1 event(s) skipped (no machine speed captured)")
                .arg(owner_->skippedNoSpeedEvents_);
        }
    }
    painter.drawText(leftMargin, sy, defectLine);
    sy += 16;
    painter.drawText(leftMargin, sy,
        QString("Triggers: %1 record%2 from %3 event%4")
            .arg(owner_->triggers_.size())
            .arg(owner_->triggers_.size() == 1 ? QString() : QStringLiteral("s"))
            .arg(singleEvent ? 1 : owner_->eventGroups_.size())
            .arg(singleEvent || owner_->eventGroups_.size() == 1 ? QString() : QStringLiteral("s")));
    sy += 16;
    painter.drawText(leftMargin, sy,
        "Rounded = DRIVE SIDE · triangle = OPERATOR SIDE · diamond = defect · sensor pin = trigger. "
        "Each marker is colored per event; hover a camera, defect or trigger for details.");
    if (owner_->refEnabled_) {
        sy += 16;
        painter.drawText(leftMargin, sy,
            QString("%1 overlay: %2 reference camera%3 · %4 speed input%5 · %6 web break%7 · %8 trigger record%9 "
                     "(read-only, not wired to the system)")
                .arg(owner_->refSet_.name)
                .arg(owner_->refSet_.cameras.size())
                .arg(owner_->refSet_.cameras.size() == 1 ? QString() : QStringLiteral("s"))
                .arg(owner_->refSet_.speedInputs.size())
                .arg(owner_->refSet_.speedInputs.size() == 1 ? QString() : QStringLiteral("s"))
                .arg(owner_->refSet_.webBreaks.size())
                .arg(owner_->refSet_.webBreaks.size() == 1 ? QString() : QStringLiteral("s"))
                .arg(owner_->refSet_.triggers.size())
                .arg(owner_->refSet_.triggers.size() == 1 ? QString() : QStringLiteral("s")));
    }
}

void MachineLayoutPanel::Canvas::drawZoomIndicator(QPainter& p, const ThemeColors& tc) {
    if (!owner_->zoomActive_) {
        return;
    }
    // "×N.N" pill in the top-right corner while zoomed in.
    const double factor = (owner_->fitMaxMm_ - owner_->fitMinMm_)
                        / (owner_->maxMm_ - owner_->minMm_);
    QFont f = p.font();
    f.setPixelSize(10);
    f.setBold(true);
    p.setFont(f);
    const QString text = QStringLiteral("×%1").arg(factor, 0, 'f', 2);
    const int tw = p.fontMetrics().horizontalAdvance(text);
    const int pw = tw + 12;
    const int ph = 16;
    const int x = width() - pw - 12;
    p.setPen(QPen(QColor(tc.primary), 1));
    p.setBrush(QColor("#1C2128"));
    p.drawRoundedRect(QRect(x, 8, pw, ph), 3, 3);
    p.setPen(QColor(tc.primary));
    p.drawText(QRect(x, 8, pw, ph), Qt::AlignCenter, text);
}

void MachineLayoutPanel::Canvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const QPoint p = event->pos();

    // 1. Camera hit-test first (same expanded rect as the hover test).
    const QVector<CameraMark>& cameras = owner_->cameras_;
    for (int i = 0; i < cameras.size(); ++i) {
        if (cameraMarkerRect(cameras[i]).adjusted(-2, -2, 2, 2).contains(p)) {
            selectedCamera_ = i;
            selectedDefect_ = -1;
            selectedTrigger_ = -1;
            const CameraMark& c = cameras[i];
            QToolTip::showText(event->globalPos(),
                QString("Camera %1 — %2\nFloor: %3\nSide: %4\nGroup: %5\nIP: %6\nMachine position: %7")
                    .arg(c.id)
                    .arg(c.name.isEmpty() ? QStringLiteral("(no name)") : c.name)
                    .arg(CameraFloor::name(c.floor))
                    .arg(c.side.isEmpty() ? QStringLiteral("—") : c.side)
                    .arg(CameraGroup::name(c.group))
                    .arg(c.ip.isEmpty() ? QStringLiteral("—") : c.ip)
                    .arg(c.hasPosition ? QString("%1 mm").arg(c.mm)
                                       : QStringLiteral("not set — set it on the Camera Card")),
                this);
            if (c.hasPosition) {
                owner_->zoomToMm(c.mm);  // auto-zoom to the actual position
            }
            update();
            return;
        }
    }

    // 2. Defect hit-test second (same expanded rect as the hover test).
    const QVector<DefectMark>& defects = owner_->defects_;
    for (int i = 0; i < defects.size(); ++i) {
        if (defectMarkerRect(defects[i]).adjusted(-3, -3, 3, 3).contains(p)) {
            selectedDefect_ = i;
            selectedCamera_ = -1;
            selectedTrigger_ = -1;
            QToolTip::showText(event->globalPos(), defects[i].detail, this);
            owner_->zoomToMm(defects[i].mm);  // auto-zoom to the actual position
            update();
            return;
        }
    }

    // 2b. Trigger hit-test (sheetbreak sensor positions).
    const QVector<TriggerMark>& triggers = owner_->triggers_;
    for (int i = 0; i < triggers.size(); ++i) {
        if (triggerMarkerRect(triggers[i]).adjusted(-3, -3, 3, 3).contains(p)) {
            selectedTrigger_ = i;
            selectedCamera_ = -1;
            selectedDefect_ = -1;
            QToolTip::showText(event->globalPos(), triggers[i].detail, this);
            owner_->zoomToMm(triggers[i].mm);  // auto-zoom to the trigger position
            update();
            return;
        }
    }

    // 2c. Reference camera hit-test (read-only overlay).
    if (owner_->refEnabled_) {
        const RefSet& ref = owner_->refSet_;
        for (int i = 0; i < ref.cameras.size(); ++i) {
            const int lane = owner_->floorForRefCamera(ref.cameras[i]);
            if (refCamMarkerRect(ref.cameras[i], lane).adjusted(-2, -2, 2, 2).contains(p)) {
                const RefCamera& r = ref.cameras[i];
                QToolTip::showText(event->globalPos(),
                    QString("Reference camera — %1 (%2)\nMachine position: %3 mm\nSide: %4\n"
                            "(%5 overlay — read-only, not wired to the system)")
                        .arg(r.name, r.location)
                        .arg(r.mm)
                        .arg(r.operatorSide ? QStringLiteral("OPERATOR SIDE")
                                             : QStringLiteral("DRIVE SIDE"),
                             owner_->refSet_.name),
                    this);
                owner_->zoomToMm(r.mm);
                update();
                return;
            }
        }
        // 2d. Reference trigger hit-test (hollow pins on the TRIGGER lane).
        for (int i = 0; i < ref.triggers.size(); ++i) {
            if (refTriggerMarkerRect(ref.triggers[i]).adjusted(-3, -3, 3, 3).contains(p)) {
                const RefPoint& r = ref.triggers[i];
                QToolTip::showText(event->globalPos(),
                    QString("Trigger record — %1\nMachine position: %2 mm\n"
                            "(%3 overlay — read-only, not wired to the system)")
                        .arg(r.name).arg(r.mm).arg(owner_->refSet_.name),
                    this);
                owner_->zoomToMm(r.mm);
                update();
                return;
            }
        }
        // 2e. Reference speed input hit-test.
        for (int i = 0; i < ref.speedInputs.size(); ++i) {
            if (refSpeedMarkerRect(ref.speedInputs[i]).adjusted(-2, -2, 2, 2).contains(p)) {
                const RefPoint& r = ref.speedInputs[i];
                QToolTip::showText(event->globalPos(),
                    QString("Speed input — %1\nLocation: %2\nMachine position: %3 mm\n"
                            "(%4 overlay — read-only, not wired to the system)")
                        .arg(r.name, r.location).arg(r.mm).arg(owner_->refSet_.name),
                    this);
                owner_->zoomToMm(r.mm);
                update();
                return;
            }
        }
        // 2f. Reference web break hit-test.
        for (int i = 0; i < ref.webBreaks.size(); ++i) {
            if (refBreakMarkerRect(ref.webBreaks[i]).adjusted(-2, -2, 2, 2).contains(p)) {
                const RefPoint& r = ref.webBreaks[i];
                QToolTip::showText(event->globalPos(),
                    QString("Web break sensor — %1\nLocation: %2\nMachine position: %3 mm\n"
                            "(%4 overlay — read-only, not wired to the system)")
                        .arg(r.name, r.location).arg(r.mm).arg(owner_->refSet_.name),
                    this);
                owner_->zoomToMm(r.mm);
                update();
                return;
            }
        }
    }

    // 3. Empty canvas → clear selection and start panning (the view moves
    // only while zoomed in; the position cursor still follows the pointer).
    selectedCamera_ = -1;
    selectedDefect_ = -1;
    selectedTrigger_ = -1;
    QToolTip::hideText();
    panning_ = true;
    lastPanX_ = event->pos().x();
    cursorMm_ = mmAt(event->pos().x());  // exact position, no snapping
    updateCursorHits();
    update();
}

void MachineLayoutPanel::Canvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        selectedCamera_ = -1;
        selectedDefect_ = -1;
        selectedTrigger_ = -1;
        QToolTip::hideText();
        update();
        return;
    }
    QWidget::keyPressEvent(event);
}

void MachineLayoutPanel::Canvas::mouseMoveEvent(QMouseEvent* event) {
    // 1. Pan while dragging on empty canvas (only when zoomed in): the view
    // shifts by the same mm the pointer travels.
    if (panning_) {
        const double mmPerPixel =
            (owner_->maxMm_ - owner_->minMm_) / std::max(1, width() - 24);
        owner_->panBy((lastPanX_ - event->pos().x()) * mmPerPixel);
        lastPanX_ = event->pos().x();
    }

    // 2. Track the exact mm under the pointer (no stepping). The cursor
    // follows the pointer during both hover and drag; hit-tests below stay
    // untouched.
    cursorVisible_ = true;  // self-heal any synthetic leave (e.g. tooltips)
    cancelPendingReset();   // pointer is back inside → drop any pending reset
    cursorMm_ = mmAt(event->pos().x());
    updateCursorHits();

    // Section bar hover: empty sections get a tooltip.
    {
        const int barTop = 12;
        const int barBottom = barTop + 24;
        const QPoint p = event->pos();
        if (p.y() >= barTop && p.y() <= barBottom) {
            for (const SectionBarSlot& s : sectionBarSlots_) {
                if (s.camCount == 0 && p.x() >= s.x && p.x() <= s.x + 2) {
                    QToolTip::showText(event->globalPos(),
                        QString("%1: no cameras assigned")
                            .arg(CameraGroup::name(s.group)), this);
                    update();
                    QWidget::mouseMoveEvent(event);
                    return;
                }
            }
        }
    }

    hoveredCamera_ = -1;
    hoveredDefect_ = -1;
    hoveredTrigger_ = -1;
    hoveredRefCam_ = -1;
    hoveredRefSpeed_ = -1;
    hoveredRefBreak_ = -1;
    hoveredRefTrigger_ = -1;
    const QPoint p = event->pos();
    const QVector<CameraMark>& cameras = owner_->cameras_;
    for (int i = 0; i < cameras.size(); ++i) {
        if (cameraMarkerRect(cameras[i]).adjusted(-2, -2, 2, 2).contains(p)) {
            hoveredCamera_ = i;
            break;
        }
    }
    if (hoveredCamera_ < 0) {
        const QVector<DefectMark>& defects = owner_->defects_;
        for (int i = 0; i < defects.size(); ++i) {
            if (defectMarkerRect(defects[i]).adjusted(-3, -3, 3, 3).contains(p)) {
                hoveredDefect_ = i;
                break;
            }
        }
    }
    if (hoveredCamera_ < 0 && hoveredDefect_ < 0) {
        const QVector<TriggerMark>& triggers = owner_->triggers_;
        for (int i = 0; i < triggers.size(); ++i) {
            if (triggerMarkerRect(triggers[i]).adjusted(-3, -3, 3, 3).contains(p)) {
                hoveredTrigger_ = i;
                break;
            }
        }
    }
    if (hoveredCamera_ < 0 && hoveredDefect_ < 0 && hoveredTrigger_ < 0 && owner_->refEnabled_) {
        const RefSet& ref = owner_->refSet_;
        for (int i = 0; i < ref.cameras.size(); ++i) {
            const int lane = owner_->floorForRefCamera(ref.cameras[i]);
            if (refCamMarkerRect(ref.cameras[i], lane).adjusted(-2, -2, 2, 2).contains(p)) {
                hoveredRefCam_ = i;
                break;
            }
        }
        if (hoveredRefCam_ < 0) {
            for (int i = 0; i < ref.triggers.size(); ++i) {
                if (refTriggerMarkerRect(ref.triggers[i]).adjusted(-3, -3, 3, 3).contains(p)) {
                    hoveredRefTrigger_ = i;
                    break;
                }
            }
        }
        if (hoveredRefCam_ < 0 && hoveredRefTrigger_ < 0) {
            for (int i = 0; i < ref.speedInputs.size(); ++i) {
                if (refSpeedMarkerRect(ref.speedInputs[i]).adjusted(-2, -2, 2, 2).contains(p)) {
                    hoveredRefSpeed_ = i;
                    break;
                }
            }
        }
        if (hoveredRefCam_ < 0 && hoveredRefTrigger_ < 0 && hoveredRefSpeed_ < 0) {
            for (int i = 0; i < ref.webBreaks.size(); ++i) {
                if (refBreakMarkerRect(ref.webBreaks[i]).adjusted(-2, -2, 2, 2).contains(p)) {
                    hoveredRefBreak_ = i;
                    break;
                }
            }
        }
    }
    if (hoveredCamera_ >= 0) {
        const CameraMark& c = cameras[hoveredCamera_];
        QToolTip::showText(event->globalPos(),
            QString("Camera %1 — %2\nFloor: %3\nSide: %4\nGroup: %5\nIP: %6\nMachine position: %7")
                .arg(c.id)
                .arg(c.name.isEmpty() ? QStringLiteral("(no name)") : c.name)
                .arg(CameraFloor::name(c.floor))
                .arg(c.side.isEmpty() ? QStringLiteral("—") : c.side)
                .arg(CameraGroup::name(c.group))
                .arg(c.ip.isEmpty() ? QStringLiteral("—") : c.ip)
                .arg(c.hasPosition ? QString("%1 mm").arg(c.mm)
                                   : QStringLiteral("not set — set it on the Camera Card")),
            this);
    } else if (hoveredDefect_ >= 0) {
        QToolTip::showText(event->globalPos(), owner_->defects_[hoveredDefect_].detail, this);
    } else if (hoveredTrigger_ >= 0) {
        QToolTip::showText(event->globalPos(), owner_->triggers_[hoveredTrigger_].detail, this);
    } else if (hoveredRefCam_ >= 0) {
        const RefCamera& r = owner_->refSet_.cameras[hoveredRefCam_];
        QString tip = QString("Reference camera — %1 (%2)\nMachine position: %3 mm\nSide: %4\n")
                          .arg(r.name, r.location)
                          .arg(r.mm)
                          .arg(r.operatorSide ? QStringLiteral("OPERATOR SIDE")
                                              : QStringLiteral("DRIVE SIDE"));
        if (r.fps > 0.0) {
            tip += QStringLiteral("Frame rate: %1 fps\n").arg(r.fps, 0, 'f', 1);
        }
        tip += QStringLiteral("(%1 overlay — read-only, not wired to the system)")
                   .arg(owner_->refSet_.name);
        QToolTip::showText(event->globalPos(), tip, this);
    } else if (hoveredRefTrigger_ >= 0) {
        const RefPoint& r = owner_->refSet_.triggers[hoveredRefTrigger_];
        QToolTip::showText(event->globalPos(),
            QString("Trigger record — %1\nMachine position: %2 mm\n"
                    "(%3 overlay — read-only, not wired to the system)")
                .arg(r.name).arg(r.mm).arg(owner_->refSet_.name),
            this);
    } else if (hoveredRefSpeed_ >= 0) {
        const RefPoint& r = owner_->refSet_.speedInputs[hoveredRefSpeed_];
        QToolTip::showText(event->globalPos(),
            QString("Speed input — %1\nLocation: %2\nMachine position: %3 mm\n"
                    "(%4 overlay — read-only, not wired to the system)")
                .arg(r.name, r.location).arg(r.mm).arg(owner_->refSet_.name),
            this);
    } else if (hoveredRefBreak_ >= 0) {
        const RefPoint& r = owner_->refSet_.webBreaks[hoveredRefBreak_];
        QToolTip::showText(event->globalPos(),
            QString("Web break sensor — %1\nLocation: %2\nMachine position: %3 mm\n"
                    "(%4 overlay — read-only, not wired to the system)")
                .arg(r.name, r.location).arg(r.mm).arg(owner_->refSet_.name),
            this);
    } else {
        QToolTip::hideText();
    }
    update();
    QWidget::mouseMoveEvent(event);
}

void MachineLayoutPanel::Canvas::mouseReleaseEvent(QMouseEvent* event) {
    if (panning_) {
        panning_ = false;
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void MachineLayoutPanel::Canvas::wheelEvent(QWheelEvent* event) {
    // Zoom the shared mm scale around the mm under the cursor. 2x per wheel
    // notch (standard 120-unit notch) in either direction, so a few notches
    // restore the full view from a deep marker zoom (and fine wheels with
    // smaller deltas still scale smoothly).
    const double centerMm = mmAt(event->pos().x());
    const double factor = std::pow(2.0, event->angleDelta().y() / 120.0);
    owner_->zoomAt(centerMm, factor);
    event->accept();
}

void MachineLayoutPanel::Canvas::enterEvent(QEvent* event) {
    // A genuine re-entry (or a synthetic one after a tooltip hides) cancels any
    // pending auto-reset: the pointer is back inside the panel.
    cancelPendingReset();
    // Track the entry position so the first painted frame shows the correct
    // mm instead of a stale 0 (same math as mouseMoveEvent). Qt5's
    // QWidget::enterEvent virtual takes QEvent*, but the delivered event is a
    // QEnterEvent carrying the entry position.
    if (const QEnterEvent* enter = dynamic_cast<const QEnterEvent*>(event)) {
        cursorMm_ = mmAt(enter->pos().x());  // exact position, no snapping
    }
    cursorVisible_ = true;
    updateCursorHits();
    update();
    QWidget::enterEvent(event);
}

void MachineLayoutPanel::Canvas::leaveEvent(QEvent* event) {
    hoveredCamera_ = -1;
    hoveredDefect_ = -1;
    hoveredTrigger_ = -1;
    hoveredRefCam_ = -1;
    hoveredRefSpeed_ = -1;
    hoveredRefBreak_ = -1;
    hoveredRefTrigger_ = -1;
    cursorHitCamera_ = -1;
    cursorHitDefect_ = -1;
    cursorHitTrigger_ = -1;
    cursorHitRefCam_ = -1;
    cursorHitRefTrigger_ = -1;
    // Auto-reset the zoom when the pointer genuinely leaves the panel. Only a
    // SPONTANEOUS leave (a real pointer exit delivered by the window system)
    // arms the delayed reset: showing a tooltip (clicking a marker to zoom)
    // and layout reflows deliver synthetic Leave events while the pointer is
    // still over the panel, which must never reset the zoom. The delay plus
    // the panel-position check in timerEvent() make double sure.
    const bool spontaneous = event->spontaneous();
    if (spontaneous) {
        const QPoint gpos = QCursor::pos();
        const QPoint panelLocal = owner_->mapFromGlobal(gpos);
        if (!owner_->rect().contains(panelLocal)) {
            resetPending_ = true;
            if (resetTimerId_ == -1) {
                resetTimerId_ = startTimer(400);
            }
        }
    }
    update();
    QWidget::leaveEvent(event);
}

void MachineLayoutPanel::Canvas::timerEvent(QTimerEvent* event) {
    if (event->timerId() == resetTimerId_) {
        killTimer(resetTimerId_);
        resetTimerId_ = -1;
        if (resetPending_) {
            resetPending_ = false;
            // Only auto-reset when the pointer is genuinely outside the panel
            // (checked in panel coordinates so small canvas shifts while the
            // Reset view button appears can't fake a real exit).
            const QPoint gpos = QCursor::pos();
            const QPoint panelLocal = owner_->mapFromGlobal(gpos);
            if (!owner_->rect().contains(panelLocal)) {
                cursorVisible_ = false;  // mouseReleaseEvent handles panning_
                QToolTip::hideText();
                owner_->resetZoom();
            }
        }
    }
    QWidget::timerEvent(event);
}

void MachineLayoutPanel::Canvas::cancelPendingReset() {
    if (resetTimerId_ != -1) {
        killTimer(resetTimerId_);
        resetTimerId_ = -1;
    }
    resetPending_ = false;
}
