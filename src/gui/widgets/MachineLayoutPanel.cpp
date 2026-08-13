#include "MachineLayoutPanel.h"

#include "../../config/CameraConfig.h"
#include "../../core/EventDatabase.h"
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
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
    headerLayout->addStretch(1);
    layout->addWidget(header);

    canvas_ = new Canvas(this);
    layout->addWidget(canvas_, 1);

    connect(eventCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        applyEventFilter();
        canvas_->update();
    });
}

void MachineLayoutPanel::setCameras(const std::vector<CameraInfo>& cameras) {
    cardCameras_ = cameras;
    refresh();
}

void MachineLayoutPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refresh();  // the visual always mirrors the current configuration
}

void MachineLayoutPanel::refresh() {
    rebuildData();
    populateEventCombo();
    applyEventFilter();
    canvas_->update();
}

void MachineLayoutPanel::rebuildData() {
    cameras_.clear();
    eventGroups_.clear();
    defects_.clear();
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

        // Projecting marked frames onto machine mm needs the event speed.
        const double speed = ev.speedValue;
        const bool speedOk = std::isfinite(speed) && speed > 0.0;
        if (!speedOk) {
            ++skippedNoSpeedEvents_;
            continue;
        }
        double fps = ev.fps > 0.0 ? ev.fps : CameraConfig::getFps();
        if (fps <= 0.0) {
            fps = 10.0;
        }
        const double framesPerMm = fps * 60.0 / (speed * 1000.0);
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

        if (!group.defects.isEmpty()) {
            eventGroups_.append(group);
        }
    }
}

void MachineLayoutPanel::rebuildScale() {
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
    if (lo > hi) {
        lo = 0.0;
        hi = 1000.0;
    }
    const double pad = (hi - lo) * 0.06;
    minMm_ = lo - pad;
    maxMm_ = hi + pad;
    if (maxMm_ - minMm_ < 1.0) {
        minMm_ -= 50.0;
        maxMm_ += 50.0;
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
    defects_.clear();
    const int sel = eventCombo_ ? eventCombo_->currentData().toInt() : -1;
    if (sel < 0) {
        for (const EventGroup& g : eventGroups_) {
            defects_ += g.defects;
        }
    } else if (sel >= 0 && sel < eventGroups_.size()) {
        defects_ = eventGroups_[sel].defects;
    }
    rebuildScale();
}

double MachineLayoutPanel::mmToX(double mm) const {
    const double span = maxMm_ - minMm_;
    const double t = span > 0.0 ? (mm - minMm_) / span : 0.5;
    return 12.0 + t * static_cast<double>(qMax(0, width() - 24));
}

QColor MachineLayoutPanel::groupColor(int group) {
    switch (group) {
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

// ─────────────────────────────────────────────────────────────────────────────
// Canvas painting
// ─────────────────────────────────────────────────────────────────────────────
MachineLayoutPanel::Canvas::Canvas(MachineLayoutPanel* owner)
    : QWidget(owner), owner_(owner) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);  // hover events (tooltips) need tracking on
}

QRect MachineLayoutPanel::Canvas::cameraMarkerRect(const CameraMark& cam) const {
    const int x = cam.hasPosition ? static_cast<int>(owner_->mmToX(cam.mm))
                                  : 12 + cam.stackIndex * 16;
    return QRect(x - 10, floorAxisY_[cam.lane] - 24, 20, 26);
}

QRect MachineLayoutPanel::Canvas::defectMarkerRect(const DefectMark& def) const {
    const int x = static_cast<int>(owner_->mmToX(def.mm));
    return QRect(x - 10, defectLaneAxisY_ - 14, 20, 14);
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
    const int contentBottom = 460;
    const int dy = qMax(0, (height() - contentBottom) / 2);

    // Recompute floorAxisY_ and defectLaneAxisY_ once per paint so the helpers
    // (and the cursor) can reference them.
    const int lane1AxisY = floorAxisY_[0] = 56 + dy;
    const int lane2AxisY = floorAxisY_[1] = lane1AxisY + 88;
    const int lane3AxisY = floorAxisY_[2] = lane2AxisY + 88;
    defectLaneAxisY_     = lane3AxisY + 88;
    Q_UNUSED(leftMargin); // each helper computes its own leftMargin

    drawSectionBar(painter, tc);
    drawPaperWeb(painter, tc);
    drawFloorLanes(painter, tc);
    drawCameraMarkers(painter, tc);
    drawDefectStrip(painter, tc);
    drawPositionCursor(painter, tc);
    drawLegends(painter, tc);
    drawSummary(painter, tc);
}

// Stubs for helpers whose visual content arrives in later tasks. They must
// exist today so the dispatch above compiles and the visual stays identical.
void MachineLayoutPanel::Canvas::drawSectionBar(QPainter&, const ThemeColors&) {}
void MachineLayoutPanel::Canvas::drawPaperWeb(QPainter&, const ThemeColors&) {}
void MachineLayoutPanel::Canvas::drawPositionCursor(QPainter&, const ThemeColors&) {}

void MachineLayoutPanel::Canvas::drawFloorLanes(QPainter& painter, const ThemeColors& tc) {
    const int leftMargin = 12;

    // Lane geometry (shared mm scale across all lanes). One lane per machine
    // floor so every camera position is visible; the defect lane sits below the
    // camera lanes. The whole block is vertically centered in the canvas so
    // there's no dead space at the bottom.
    constexpr int kLanePitch = 88;  // title row + axis + markers + ticks
    const int contentBottom = 460;  // approx bottom of the summary/hint text
    const int dy = qMax(0, (height() - contentBottom) / 2);
    const int lane1TitleY = 8 + dy;
    const int lane1AxisY = floorAxisY_[0];
    const int lane2TitleY = lane1TitleY + kLanePitch;
    const int lane2AxisY = floorAxisY_[1];
    const int lane3TitleY = lane2TitleY + kLanePitch;
    const int lane3AxisY = floorAxisY_[2];
    const int defectTitleY = lane3TitleY + kLanePitch;
    const int defectLaneAxisY = defectLaneAxisY_;

    // Titles
    QFont titleFont = painter.font();
    titleFont.setPixelSize(13);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(QColor(tc.text));
    painter.drawText(leftMargin, lane1TitleY + 14, QString("1ST FLOOR — CAMERAS  (mm)"));
    painter.drawText(leftMargin, lane2TitleY + 14, QString("2ND FLOOR — CAMERAS  (mm)"));
    painter.drawText(leftMargin, lane3TitleY + 14, QString("3RD FLOOR — CAMERAS  (mm)"));
    painter.drawText(leftMargin, defectTitleY + 14, QString("MARKED & ALIGNED DEFECTS  (mm)"));

    // Nice tick step for the mm axis
    const double span = owner_->maxMm_ - owner_->minMm_;
    const double raw = span / 8.0;
    const double mag = std::pow(10.0, std::floor(std::log10(raw > 0.0 ? raw : 1.0)));
    const double norm = raw / mag;
    double step = 1.0 * mag;
    if (norm > 5.0) step = 10.0 * mag;
    else if (norm > 2.0) step = 5.0 * mag;
    else if (norm > 1.0) step = 2.0 * mag;

    // Per-lane vertical bounds: split each floor lane into an upper (operator)
    // and lower (drive) sub-row. Axis line is centered; 32px above and 32px below.
    struct SubRow { int opTop, opBottom, driveTop, driveBottom; };
    SubRow rows[CameraFloor::kCount];
    constexpr int kSubRowHeight = 32;
    for (int f = 0; f < CameraFloor::kCount; ++f) {
        const int axisY = floorAxisY_[f];
        rows[f].opTop       = axisY - kSubRowHeight;
        rows[f].opBottom    = axisY;
        rows[f].driveTop    = axisY;
        rows[f].driveBottom = axisY + kSubRowHeight;
    }

    // Sub-row background fills — must precede the axis-line draw so the line
    // sits cleanly on top of the tints.
    const QColor opTint(0x2A, 0x32, 0x39);     // upper sub-row (operator side)
    const QColor driveTint(0x1F, 0x24, 0x29);  // lower sub-row (drive side)
    for (int f = 0; f < CameraFloor::kCount; ++f) {
        painter.fillRect(leftMargin, rows[f].opTop,
                         width() - 2 * leftMargin,
                         rows[f].opBottom - rows[f].opTop, opTint);
        painter.fillRect(leftMargin, rows[f].driveTop,
                         width() - 2 * leftMargin,
                         rows[f].driveBottom - rows[f].driveTop, driveTint);
    }

    // Axis lines + mm ticks (labels on the 3rd-floor lane, shared scale)
    QFont tickFont = painter.font();
    tickFont.setPixelSize(9);
    painter.setFont(tickFont);
    const int laneAxes[CameraFloor::kCount] = {lane1AxisY, lane2AxisY, lane3AxisY};
    for (int f = 0; f < CameraFloor::kCount; ++f) {
        const int axisY = laneAxes[f];
        painter.setPen(QPen(QColor(tc.border), 1));
        painter.drawLine(leftMargin, axisY, width() - leftMargin, axisY);
        const bool drawLabels = (f == CameraFloor::kCount - 1);
        if (drawLabels) {
            const double tickStart = std::ceil(owner_->minMm_ / step) * step;
            for (double mm = tickStart; mm <= owner_->maxMm_ + step * 0.5; mm += step) {
                const int x = static_cast<int>(owner_->mmToX(mm));
                painter.setPen(QPen(QColor(tc.border), 1));
                painter.drawLine(x, axisY - 4, x, axisY + 4);
                painter.setPen(QColor(tc.text));
                painter.drawText(QRect(x - 40, axisY + 6, 80, 14),
                                 Qt::AlignHCenter | Qt::AlignTop, QString::number(mm, 'f', 0));
            }
        }
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
        const QString prefix = CameraFloor::name(CameraFloor::kFirst + f).section(' ', 0, 0).toUpper();
        const QString opLabel    = QString("%1 · OPERATOR").arg(prefix);
        const QString driveLabel = QString("%1 · DRIVE").arg(prefix);
        painter.drawText(leftMargin, rows[f].opTop + 11, opLabel);
        painter.drawText(leftMargin, rows[f].driveTop + 11, driveLabel);
    }

    painter.setPen(QPen(QColor(tc.border), 1));
    painter.drawLine(leftMargin, defectLaneAxisY, width() - leftMargin, defectLaneAxisY);
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
        const int axisY = floorAxisY_[cam.lane];
        const int x = cam.hasPosition ? static_cast<int>(owner_->mmToX(cam.mm))
                                      : 12 + cam.stackIndex * 16;
        const QColor col = owner_->groupColor(cam.group);
        const bool isOperator = cam.side.compare("OPERATOR SIDE", Qt::CaseInsensitive) == 0;
        painter.setPen(QPen(i == hoveredCamera_ ? Qt::white : QColor(tc.border), 1.5));
        painter.setBrush(cam.hasPosition ? col : QColor(col.red(), col.green(), col.blue(), 90));
        if (isOperator) {
            // Upward triangle = OPERATOR SIDE
            QPainterPath tri;
            tri.moveTo(x, axisY - 24);
            tri.lineTo(x + 10, axisY);
            tri.lineTo(x - 10, axisY);
            tri.closeSubpath();
            painter.drawPath(tri);
        } else {
            // Rounded rect = DRIVE SIDE (and default)
            painter.drawRoundedRect(QRect(x - 9, axisY - 22, 18, 24), 4, 4);
        }

        // Vertical camera label (reads bottom-to-top) so tightly spaced
        // cameras never overlap on the mm line.
        painter.save();
        painter.translate(x, axisY - 26);
        painter.rotate(-90);
        painter.setPen(i == hoveredCamera_ ? Qt::white : QColor(tc.text));
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
        painter.drawText(QRect(leftMargin, defectLaneAxisY + 26, width() - 2 * leftMargin, 14),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         singleEvent
                             ? QStringLiteral("This event has no positionable defects.")
                             : QStringLiteral("No marked & aligned defects yet — mark the same defect on at "
                                              "least two cameras in the Analysis view and press Align."));
    }
    for (int i = 0; i < defects.size(); ++i) {
        const DefectMark& def = defects[i];
        const int x = static_cast<int>(owner_->mmToX(def.mm));
        painter.setPen(QPen(i == hoveredDefect_ ? Qt::white : QColor(tc.border), 1.5));
        painter.setBrush(def.color);
        QPainterPath diamond;
        diamond.moveTo(x, defectLaneAxisY - 12);
        diamond.lineTo(x + 8, defectLaneAxisY - 6);
        diamond.lineTo(x, defectLaneAxisY);
        diamond.lineTo(x - 8, defectLaneAxisY - 6);
        diamond.closeSubpath();
        painter.drawPath(diamond);
    }
}

void MachineLayoutPanel::Canvas::drawLegends(QPainter& painter, const ThemeColors& tc) {
    const int leftMargin = 12;
    const int defectLaneAxisY = defectLaneAxisY_;

    // ── Legend ──
    int ly = defectLaneAxisY + 50;
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
}

void MachineLayoutPanel::Canvas::drawSummary(QPainter& painter, const ThemeColors& tc) {
    const int leftMargin = 12;
    const int defectLaneAxisY = defectLaneAxisY_;
    const QVector<CameraMark>& cameras = owner_->cameras_;
    const QVector<DefectMark>& defects = owner_->defects_;

    int ly = defectLaneAxisY + 50;

    // ── Summary ──
    QFont sumFont = painter.font();
    sumFont.setPixelSize(11);
    sumFont.setBold(false);
    painter.setFont(sumFont);
    painter.setPen(QColor(tc.text));
    int sy = ly + 28;
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
        "Rounded = DRIVE SIDE · triangle = OPERATOR SIDE. Each defect diamond is colored per event; "
        "hover a camera or defect for details.");
}

void MachineLayoutPanel::Canvas::mouseMoveEvent(QMouseEvent* event) {
    hoveredCamera_ = -1;
    hoveredDefect_ = -1;
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
    } else {
        QToolTip::hideText();
    }
    update();
    QWidget::mouseMoveEvent(event);
}

void MachineLayoutPanel::Canvas::leaveEvent(QEvent* event) {
    hoveredCamera_ = -1;
    hoveredDefect_ = -1;
    QToolTip::hideText();
    update();
    QWidget::leaveEvent(event);
}
