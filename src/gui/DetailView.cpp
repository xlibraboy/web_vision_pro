#include "DetailView.h"
#include "../config/CameraConfig.h"
#include "../core/TemperatureStatus.h"
#include <algorithm>
#include <cmath>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QDebug>
#include <QFont>
#include <QEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainterPath>
#include <QPolygon>
#include <QCheckBox>
#include <QLineF>
#include <functional>

// Convert a hex color like "#RRGGBB" into an rgba() string with the given
// alpha (0-255). Local helper mirroring CameraDeviceSettingsDialog's toRgba.
static QString toRgba(const QString& hex, int alpha) {
    QColor c(hex);
    if (!c.isValid()) {
        return hex;
    }
    return QString("rgba(%1, %2, %3, %4)")
        .arg(c.red()).arg(c.green()).arg(c.blue())
        .arg(static_cast<double>(alpha) / 255.0, 0, 'f', 2);
}

// Sensor-context overlay drawn on the live video frame. The displayed image is
// the AOI crop scaled to fit, so this widget draws the full-sensor outline
// around it (positioned by the offsets) and tints the sensor area outside the
// region. Moving the offsets slides the outline, making the region's position
// within the sensor visible.
class DetailView::AoiRegionOverlay : public QWidget {
public:
    explicit AoiRegionOverlay(QWidget* parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setMouseTracking(true);
    }

    // Fired when the operator finishes drag-drawing a region (sensor coords).
    std::function<void(int width, int height, int offsetX, int offsetY)> onRegionDrawn;

    void setValues(int sensorW, int sensorH, int regionW, int regionH,
                   int offsetX, int offsetY) {
        sensorW_ = sensorW;
        sensorH_ = sensorH;
        regionW_ = regionW;
        regionH_ = regionH;
        offsetX_ = offsetX;
        offsetY_ = offsetY;
        update();
    }

    // Draw mode: the overlay becomes the interactive surface for drag-drawing
    // the AOI rectangle on the frame.
    void setDrawEnabled(bool enabled) {
        drawEnabled_ = enabled;
        setAttribute(Qt::WA_TransparentForMouseEvents, !enabled);
        setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
        dragging_ = false;
        dragRect_ = QRect();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        if (sensorW_ <= 0 || sensorH_ <= 0 || regionW_ <= 0 || regionH_ <= 0) {
            return;
        }
        const QRect content = rect().adjusted(1, 1, -1, -1);

        // Mirror CameraWidget: the region is scaled KeepAspectRatio and centered.
        const double aspect = static_cast<double>(regionW_) / regionH_;
        QSize scaled = content.size();
        if (static_cast<double>(scaled.width()) / scaled.height() > aspect) {
            scaled.setWidth(qMax(1, static_cast<int>(scaled.height() * aspect)));
        } else {
            scaled.setHeight(qMax(1, static_cast<int>(scaled.width() / aspect)));
        }
        const QRect imgRect(content.center().x() - scaled.width() / 2,
                            content.center().y() - scaled.height() / 2,
                            scaled.width(), scaled.height());

        const double sx = static_cast<double>(imgRect.width()) / regionW_;
        const double sy = static_cast<double>(imgRect.height()) / regionH_;
        const QRect sensorRect(imgRect.left() - static_cast<int>(offsetX_ * sx),
                               imgRect.top() - static_cast<int>(offsetY_ * sy),
                               static_cast<int>(sensorW_ * sx),
                               static_cast<int>(sensorH_ * sy));

        const ThemeColors tc = CameraConfig::getThemeColors();

        // Tint the sensor area OUTSIDE the region (the part not captured).
        QPainterPath sensorPath;
        sensorPath.addRect(sensorRect);
        QPainterPath regionPath;
        regionPath.addRect(imgRect);
        QColor tint(tc.primary);
        tint.setAlpha(22);
        p.fillPath(sensorPath.subtracted(regionPath), tint);

        // Sensor outline (dashed) + region border (solid).
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(tc.primary), 1, Qt::DashLine));
        p.drawRect(sensorRect);
        p.setPen(QPen(QColor(tc.primary), 1));
        p.drawRect(imgRect);

        // In-progress drag selection while drawing.
        if (dragging_ && !dragRect_.isEmpty()) {
            QColor fill(tc.primary);
            fill.setAlpha(48);
            p.fillRect(dragRect_, fill);
            p.setPen(QPen(QColor(tc.primary), 1));
            p.drawRect(dragRect_);
        }
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (drawEnabled_ && e->button() == Qt::LeftButton) {
            dragging_ = true;
            dragStart_ = e->pos();
            dragRect_ = QRect(dragStart_, QSize(0, 0));
            update();
            e->accept();
            return;
        }
        QWidget::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (dragging_) {
            dragRect_ = QRect(dragStart_, e->pos()).normalized();
            update();
            e->accept();
            return;
        }
        QWidget::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (dragging_ && e->button() == Qt::LeftButton) {
            dragRect_ = QRect(dragStart_, e->pos()).normalized();
            dragging_ = false;
            emitDrawnRegion();
            update();
            e->accept();
            return;
        }
        QWidget::mouseReleaseEvent(e);
    }

    void mouseDoubleClickEvent(QMouseEvent* e) override {
        // Never navigate back (frame double-click) while draw mode is active.
        if (drawEnabled_) {
            e->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(e);
    }

private:
    // Map the drag rectangle (widget px) into sensor coordinates. The displayed
    // image is the current region scaled to fit, so the drawn rect is expressed
    // relative to the current crop and the current offsets are added back.
    void emitDrawnRegion() {
        if (sensorW_ <= 0 || sensorH_ <= 0 || regionW_ <= 0 || regionH_ <= 0) {
            return;
        }
        const QRect content = rect().adjusted(1, 1, -1, -1);
        const double aspect = static_cast<double>(regionW_) / regionH_;
        QSize scaled = content.size();
        if (static_cast<double>(scaled.width()) / scaled.height() > aspect) {
            scaled.setWidth(qMax(1, static_cast<int>(scaled.height() * aspect)));
        } else {
            scaled.setHeight(qMax(1, static_cast<int>(scaled.width() / aspect)));
        }
        const QRect imgRect(content.center().x() - scaled.width() / 2,
                            content.center().y() - scaled.height() / 2,
                            scaled.width(), scaled.height());

        const QRect sel = dragRect_.intersected(imgRect);
        if (sel.width() < 5 || sel.height() < 5) {
            return;  // ignore accidental tiny drags
        }

        const double sx = static_cast<double>(imgRect.width()) / regionW_;
        const double sy = static_cast<double>(imgRect.height()) / regionH_;

        int newW = qMax(1, static_cast<int>(sel.width() / sx));
        int newH = qMax(1, static_cast<int>(sel.height() / sy));
        int newOX = offsetX_ + qRound((sel.left() - imgRect.left()) / sx);
        int newOY = offsetY_ + qRound((sel.top() - imgRect.top()) / sy);

        // Clamp into the sensor: Offset + Size <= SensorMax.
        newOX = qBound(0, newOX, qMax(0, sensorW_ - newW));
        newOY = qBound(0, newOY, qMax(0, sensorH_ - newH));

        if (onRegionDrawn) {
            onRegionDrawn(newW, newH, newOX, newOY);
        }
    }

    int sensorW_ = 0;
    int sensorH_ = 0;
    int regionW_ = 0;
    int regionH_ = 0;
    int offsetX_ = 0;
    int offsetY_ = 0;
    bool drawEnabled_ = false;
    bool dragging_ = false;
    QPoint dragStart_;
    QRect dragRect_;
};

// Software detection ROI (analysis region) widget. Overlays the live video
// frame with the polygon that limits defect analysis; while drawing, clicks
// add vertices (normalized delivered-frame 0..1) and clicking back on the
// first vertex closes the region. Empty polygon = no region / analysis paused.
class DetailView::RoiRegionOverlay : public QWidget {
public:
    explicit RoiRegionOverlay(QWidget* parent = nullptr) : QWidget(parent) {
        setMouseTracking(true);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::StrongFocus);  // receive Enter/Esc while drawing
        updateMousePassthrough();
    }

    std::function<void(const QVector<QPointF>&)> onRegionClosed;
    // Fired when the operator aborts drawing with Esc (scratch already cleared).
    std::function<void()> onDrawingCancelled;
    // Notified after every scratch-point change (count of committed vertices).
    std::function<void(int)> onScratchChanged;

    // Delivered-frame geometry the polygon is normalized against.
    void setFrameSize(int w, int h) {
        frameW_ = w;
        frameH_ = h;
        update();
    }

    void setPolygon(const QVector<QPointF>& poly) {
        polygon_ = poly;
        update();
    }

    void setDrawing(bool enabled) {
        drawing_ = enabled;
        if (!drawing_) {
            scratch_.clear();
            cursor_ = QPointF();
        } else {
            setFocus(Qt::OtherFocusReason);  // so Enter/Esc land here
        }
        updateMousePassthrough();
        setCursor(drawing_ ? Qt::CrossCursor : Qt::ArrowCursor);
        notifyScratch();
        update();
    }

    bool isDrawing() const { return drawing_; }
    bool hasPolygon() const { return polygon_.size() >= 3; }
    int scratchCount() const { return scratch_.size(); }

    // Close and emit the in-progress polygon (>= 3 points). Returns whether a
    // region was committed; safe to call repeatedly.
    bool finishDrawing() {
        if (!drawing_ || scratch_.size() < 3) {
            return false;
        }
        // A double-click lands as a second click on the same spot — drop the
        // duplicate so the region never gets a zero-length edge.
        if (scratch_.size() >= 2
            && QLineF(scratch_.last(), scratch_[scratch_.size() - 2]).length() < 1e-6) {
            scratch_.removeLast();
        }
        if (scratch_.size() < 3) {
            update();
            return false;
        }
        const QVector<QPointF> done = scratch_;
        scratch_.clear();
        cursor_ = QPointF();
        setDrawing(false);
        if (onRegionClosed) {
            onRegionClosed(done);
        }
        update();
        return true;
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (frameW_ <= 0 || frameH_ <= 0) {
            return;
        }
        const QRect content = rect().adjusted(1, 1, -1, -1);
        if (content.isEmpty()) {
            return;
        }
        const QRect imgRect = imageRect(content);

        const ThemeColors tc = CameraConfig::getThemeColors();
        QColor stroke(tc.primary);
        QColor fill(tc.primary);
        fill.setAlpha(46);
        QColor vertex(tc.primary);
        QColor vertexFill(tc.text);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        auto toWidget = [&](const QPointF& n) {
            return QPointF(imgRect.left() + n.x() * imgRect.width(),
                           imgRect.top() + n.y() * imgRect.height());
        };
        auto drawChain = [&](const QVector<QPointF>& pts, bool closed) {
            if (pts.isEmpty()) {
                return;
            }
            QPolygonF poly;
            poly.reserve(pts.size());
            for (const QPointF& n : pts) {
                poly << toWidget(n);
            }
            QPainterPath path;
            path.addPolygon(poly);
            if (closed && poly.size() >= 3) {
                p.fillPath(path, fill);
            }
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(tc.primary), 1.5));
            p.drawPath(path);
            // Stroke the closing edge explicitly so the end-draw point always
            // links back to the first point (independent of how the path
            // handles its implicit closed subpath).
            if (closed && poly.size() >= 3) {
                p.drawLine(poly.last(), poly.first());
            }
            // Vertex markers.
            p.setPen(QPen(vertex, 1));
            p.setBrush(vertexFill);
            const qreal r = closed ? 2.6 : 3.2;
            for (const QPointF& n : pts) {
                const QPointF w = toWidget(n);
                p.drawEllipse(w, r, r);
            }
        };

        // Existing region under the scratch chain (kept visible while drawing
        // a replacement so the operator sees what they are about to change).
        if (polygon_.size() >= 3) {
            const qreal oldAlpha = p.opacity();
            p.setOpacity(drawing_ ? 0.45 : 1.0);
            drawChain(polygon_, true);
            p.setOpacity(oldAlpha);
        }

        if (drawing_) {
            // Scratch: committed clicks + rubber band to the cursor. When the
            // cursor approaches the first vertex the preview snaps onto it and
            // a closing-target ring appears, signalling that the next click
            // links the end-draw point back to the first point.
            QVector<QPointF> chain = scratch_;
            if (!chain.isEmpty() && !cursor_.isNull()) {
                QPointF endW = toWidget(cursor_);
                bool nearStart = false;
                QPointF firstW;
                if (chain.size() >= 3) {
                    firstW = toWidget(chain.first());
                    nearStart = QLineF(endW, firstW).length() <= CLOSE_SNAP_PX;
                    if (nearStart) {
                        endW = firstW;
                    }
                }
                p.setPen(QPen(QColor(tc.primary).lighter(nearStart ? 155 : 130),
                              1, Qt::DashLine));
                p.drawLine(toWidget(chain.last()), endW);
                if (nearStart) {
                    p.setPen(QPen(QColor(tc.primary).lighter(155), 1.2, Qt::DotLine));
                    p.setBrush(Qt::NoBrush);
                    p.drawEllipse(firstW, 6.5, 6.5);
                }
            }
            drawChain(chain, false);
        }
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (!drawing_ || frameW_ <= 0 || frameH_ <= 0) {
            QWidget::mousePressEvent(e);
            return;
        }
        e->accept();
        if (e->button() == Qt::RightButton) {
            // Undo the last vertex.
            if (!scratch_.isEmpty()) {
                scratch_.removeLast();
            }
            notifyScratch();
            update();
            return;
        }
        if (e->button() != Qt::LeftButton) {
            return;
        }

        const QRect imgRect = imageRect(rect().adjusted(1, 1, -1, -1));
        if (!imgRect.contains(e->pos())) {
            return; // outside the image area - ignore
        }
        const QPointF n = normalized(e->pos(), imgRect);

        // Clicking near the first vertex closes the region.
        if (scratch_.size() >= 3 && nearFirst(e->pos(), imgRect)) {
            const QVector<QPointF> done = scratch_;
            scratch_.clear();
            cursor_ = QPointF();
            setDrawing(false);
            if (onRegionClosed) {
                onRegionClosed(done);
            }
            update();
            return;
        }

        scratch_.append(n);
        cursor_ = n;
        notifyScratch();
        update();
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (drawing_ && frameW_ > 0 && frameH_ > 0) {
            // Keep the rubber-band target in the same normalized space as the
            // committed vertices so the preview lands exactly on the cursor.
            cursor_ = normalized(e->pos(), imageRect(rect().adjusted(1, 1, -1, -1)));
            update();
            e->accept();
            return;
        }
        QWidget::mouseMoveEvent(e);
    }

    void mouseDoubleClickEvent(QMouseEvent* e) override {
        // While drawing, a double-click finishes the polygon (standard ROI
        // editor affordance); either way it never falls through to frame nav.
        if (drawing_) {
            e->accept();
            finishDrawing();
            return;
        }
        QWidget::mouseDoubleClickEvent(e);
    }

    void keyPressEvent(QKeyEvent* e) override {
        if (drawing_) {
            if (e->key() == Qt::Key_Escape) {
                e->accept();
                scratch_.clear();
                cursor_ = QPointF();
                setDrawing(false);
                if (onDrawingCancelled) {
                    onDrawingCancelled();
                }
                return;
            }
            if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
                e->accept();
                finishDrawing();
                return;
            }
        }
        QWidget::keyPressEvent(e);
    }

private:
    QRect imageRect(const QRect& content) const {
        const double aspect = static_cast<double>(frameW_) / frameH_;
        QSize scaled = content.size();
        if (static_cast<double>(scaled.width()) / scaled.height() > aspect) {
            scaled.setWidth(qMax(1, static_cast<int>(scaled.height() * aspect)));
        } else {
            scaled.setHeight(qMax(1, static_cast<int>(scaled.width() / aspect)));
        }
        return QRect(content.center().x() - scaled.width() / 2,
                     content.center().y() - scaled.height() / 2,
                     scaled.width(), scaled.height());
    }

    QPointF normalized(const QPoint& pos, const QRect& imgRect) const {
        const double x = (pos.x() - imgRect.left()) / static_cast<double>(imgRect.width());
        const double y = (pos.y() - imgRect.top()) / static_cast<double>(imgRect.height());
        return QPointF(qBound(0.0, x, 1.0), qBound(0.0, y, 1.0));
    }

    bool nearFirst(const QPoint& pos, const QRect& imgRect) const {
        if (scratch_.isEmpty()) {
            return false;
        }
        const QPointF first = QPointF(imgRect.left() + scratch_.first().x() * imgRect.width(),
                                      imgRect.top() + scratch_.first().y() * imgRect.height());
        return QLineF(pos, first).length() <= CLOSE_SNAP_PX;
    }

    void updateMousePassthrough() {
        setAttribute(Qt::WA_TransparentForMouseEvents, !drawing_);
    }

    void notifyScratch() {
        if (onScratchChanged) {
            onScratchChanged(scratch_.size());
        }
    }

    static constexpr qreal CLOSE_SNAP_PX = 14.0;

    int frameW_ = 0;
    int frameH_ = 0;
    QVector<QPointF> polygon_;   // normalized committed region
    QVector<QPointF> scratch_;   // vertices being entered while drawing
    QPointF cursor_;
    bool drawing_ = false;
};

DetailView::DetailView(QWidget *parent) : QWidget(parent), isAdmin_(false) {
    setupUi();
}

DetailView::~DetailView() {}

void DetailView::setupUi() {
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(8);

    // --- Left Panel: Info + Parameters (Vertical Stack) ---
    QWidget* leftPanel = new QWidget(this);
    leftPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    leftPanel->setMinimumWidth(360);
    leftPanel->setMaximumWidth(480);
    QVBoxLayout* leftPanelLayout = new QVBoxLayout(leftPanel);
    leftPanelLayout->setContentsMargins(0, 0, 0, 0);
    leftPanelLayout->setSpacing(8);

    // Camera Info Group
    infoGroup_ = new QGroupBox("Camera Information", this);
    QFormLayout* infoLayout = new QFormLayout(infoGroup_);
    
    lblId_ = new QLabel("-", this);
    lblLocation_ = new QLabel("-", this);
    lblSide_ = new QLabel("-", this);
    lblDescription_ = new QLabel("-", this);
    lblModel_ = new QLabel("-", this);
    lblIP_ = new QLabel("-", this);
    lblImageSize_ = new QLabel("-", this);
    lblFPS_ = new QLabel("-", this);
    lblConfiguredFramePeriod_ = new QLabel("-", this);
    lblDisplayFps_ = new QLabel("-", this);
    lblActualFramePeriod_ = new QLabel("-", this);
    lblTemp_ = new QLabel("-", this);

    infoLayout->addRow("ID:", lblId_);
    infoLayout->addRow("Location:", lblLocation_);
    infoLayout->addRow("Side:", lblSide_);
    infoLayout->addRow("Description:", lblDescription_);
    infoLayout->addRow("Model:", lblModel_);
    infoLayout->addRow("IP Address:", lblIP_);
    infoLayout->addRow("Image Size:", lblImageSize_);
    infoLayout->addRow("Acquisition FPS:", lblFPS_);
    infoLayout->addRow("Configured Frame Period:", lblConfiguredFramePeriod_);
    infoLayout->addRow("Resulting FPS (Abs) [Hz]:", lblDisplayFps_);
    infoLayout->addRow("Actual Frame Period:", lblActualFramePeriod_);
    infoLayout->addRow("Temperature °C:", lblTemp_);

    lblDisplayFps_->setToolTip("Resulting Framerate reported by the camera. If this is much lower than Acquisition FPS, the camera may be limited by exposure time, sensor readout time, AOI, or transmission bandwidth.");    lblActualFramePeriod_->setToolTip("Computed from Resulting Framerate: 1,000,000 / resulting FPS in microseconds.\nExposure time must stay below this period.");
    lblConfiguredFramePeriod_->setToolTip("Computed from Acquisition FPS: 1,000,000 / configured FPS in microseconds.\nExposure time must stay below this period.");

    // Parameters Group (Below Camera Info)
    controlGroup_ = new QGroupBox("Camera Parameters", this);
    QVBoxLayout* controlLayout = new QVBoxLayout(controlGroup_);

    // Gain Group
    gainGroup_ = new QGroupBox("Gain", controlGroup_);
    QVBoxLayout* gainLayout = new QVBoxLayout(gainGroup_);
    spinGain_ = new QDoubleSpinBox(this);
    spinGain_->setRange(0.0, 24.0);
    spinGain_->setSingleStep(0.1);
    spinGain_->setDecimals(1);
    
    sliderGain_ = new QSlider(Qt::Horizontal, this);
    sliderGain_->setRange(0, 240);  // 0.0 to 24.0 with 0.1 precision
    sliderGain_->setValue(10);
    
    connect(spinGain_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
            [this](double val) {
                const int sliderValue = gainIsRaw_
                    ? static_cast<int>(std::llround(val))
                    : static_cast<int>(std::llround(val * 10.0));
                sliderGain_->setValue(sliderValue);
            });
    connect(sliderGain_, &QSlider::valueChanged, 
            [this](int val) {
                const double displayedValue = gainIsRaw_ ? static_cast<double>(val)
                                                         : val / 10.0;
                spinGain_->setValue(displayedValue);
                emit parameterChanged(currentCameraId_, "Gain", displayedValue);
            });
    gainLayout->addWidget(spinGain_);
    gainLayout->addWidget(sliderGain_);
    controlLayout->addWidget(gainGroup_);

    // Exposure Time Group
    QGroupBox* expGroup = new QGroupBox("Exposure Time", controlGroup_);
    QVBoxLayout* expLayout = new QVBoxLayout(expGroup);
    spinExposure_ = new QSpinBox(this);
    spinExposure_->setRange(100, 1000000);  // Will be constrained dynamically
    spinExposure_->setSuffix(" µs");
    
    sliderExposure_ = new QSlider(Qt::Horizontal, this);
    sliderExposure_->setRange(100, 1000000);
    sliderExposure_->setValue(5000);
    
    connect(spinExposure_, QOverload<int>::of(&QSpinBox::valueChanged), 
            sliderExposure_, &QSlider::setValue);
    connect(sliderExposure_, &QSlider::valueChanged, 
            [this](int val) {
                spinExposure_->setValue(val);
                // Live apply
                emit parameterChanged(currentCameraId_, "Exposure", val);
            });
    expLayout->addWidget(spinExposure_);
    expLayout->addWidget(sliderExposure_);
    controlLayout->addWidget(expGroup);

    // Gamma Group
    QGroupBox* gammaGroup = new QGroupBox("Gamma", controlGroup_);
    QVBoxLayout* gammaLayout = new QVBoxLayout(gammaGroup);
    spinGamma_ = new QDoubleSpinBox(this);
    spinGamma_->setRange(0.1, 4.0);
    spinGamma_->setSingleStep(0.1);
    spinGamma_->setDecimals(2);
    spinGamma_->setValue(1.0);
    
    sliderGamma_ = new QSlider(Qt::Horizontal, this);
    sliderGamma_->setRange(10, 400);  // 0.1 to 4.0 with 0.01 precision
    sliderGamma_->setValue(100);
    
    connect(spinGamma_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
            [this](double val) { sliderGamma_->setValue(static_cast<int>(val * 100)); });
    connect(sliderGamma_, &QSlider::valueChanged, 
            [this](int val) {
                spinGamma_->setValue(val / 100.0);
                // Live apply
                emit parameterChanged(currentCameraId_, "Gamma", val / 100.0);
            });
    gammaLayout->addWidget(spinGamma_);
    gammaLayout->addWidget(sliderGamma_);
    controlLayout->addWidget(gammaGroup);

    // Contrast Group
    QGroupBox* contrastGroup = new QGroupBox("Contrast", controlGroup_);
    QVBoxLayout* contrastLayout = new QVBoxLayout(contrastGroup);
    spinContrast_ = new QDoubleSpinBox(this);
    spinContrast_->setRange(0.0, 2.0);
    spinContrast_->setSingleStep(0.05);
    spinContrast_->setDecimals(2);
    spinContrast_->setValue(1.0);
    
    sliderContrast_ = new QSlider(Qt::Horizontal, this);
    sliderContrast_->setRange(0, 200);  // 0.0 to 2.0 with 0.01 precision
    sliderContrast_->setValue(100);
    
    connect(spinContrast_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
            [this](double val) { sliderContrast_->setValue(static_cast<int>(val * 100)); });
    connect(sliderContrast_, &QSlider::valueChanged, 
            [this](int val) { 
                spinContrast_->setValue(val / 100.0); 
                // Live apply
                emit parameterChanged(currentCameraId_, "Contrast", val / 100.0);
            });
    contrastLayout->addWidget(spinContrast_);
    contrastLayout->addWidget(sliderContrast_);
    controlLayout->addWidget(contrastGroup);

    // Save & Load Parameter buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLoad_ = new QPushButton("Load Parameters", this);
    btnSave_ = new QPushButton("Save Parameters", this);
    connect(btnLoad_, &QPushButton::clicked, this, &DetailView::onLoadParams);
    connect(btnSave_, &QPushButton::clicked, this, &DetailView::onSaveParams);
    
    btnLayout->addWidget(btnLoad_);
    btnLayout->addWidget(btnSave_);
    controlLayout->addLayout(btnLayout);

    leftPanelLayout->addWidget(infoGroup_);
    leftPanelLayout->addWidget(controlGroup_);
    leftPanelLayout->addStretch(); // Push content to top

    // --- Center Panel: Video ---
    cameraWidget_ = new CameraWidget(this);
    cameraWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // Connect camera widget double-click to go back to grid
    connect(cameraWidget_, &CameraWidget::doubleClicked, this, &DetailView::backRequested);

    // Center layout with just the camera widget (no back button for more space)
    QVBoxLayout* centerLayout = new QVBoxLayout();
    centerLayout->addWidget(cameraWidget_);

    // Layout Assembly: 25% Left Panel, 75% Video
    mainLayout->addWidget(leftPanel, 0);
    mainLayout->addLayout(centerLayout, 3); 

    buildAoiOverlay();
    buildRoiControls();

    applyLiveViewTypography();
    setAdminMode(false); // Default state
}

void DetailView::buildAoiOverlay() {
    const ThemeColors tc = CameraConfig::getThemeColors();

    // Sensor-context indicator behind the chip/panel; shown while the AOI
    // panel is open so offset edits are visible on the frame.
    aoiOverlay_ = new AoiRegionOverlay(cameraWidget_);
    aoiOverlay_->setGeometry(cameraWidget_->rect());
    aoiOverlay_->hide();

    // Chip on the video frame: checkable show/hide toggle for the AOI panel.
    aoiChip_ = new QToolButton(cameraWidget_);
    aoiChip_->setCheckable(true);
    aoiChip_->setText("AOI");
    aoiChip_->setCursor(Qt::PointingHandCursor);
    aoiChip_->setStyleSheet(aoiOverlayStyle(toRgba(tc.btnBg, 230)));
    connect(aoiChip_, &QToolButton::toggled, this, &DetailView::onAoiChipToggled);

    // Floating panel with the four AOI fields.
    aoiPanel_ = new QFrame(cameraWidget_);
    aoiPanel_->setObjectName("aoiPanel");
    aoiPanel_->setStyleSheet(QString(
        "QFrame#aoiPanel { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
        "QLabel { color: %3; font-size: 11px; }"
        "QSpinBox { background-color: %4; color: %3; border: 1px solid %2; border-radius: 4px;"
        " padding: 2px 4px; font-size: 11px; }")
        .arg(toRgba(tc.btnBg, 245), tc.border, tc.text, toRgba(tc.bg, 120)));
    QVBoxLayout* aoiLayout = new QVBoxLayout(aoiPanel_);
    aoiLayout->setContentsMargins(10, 8, 10, 8);
    aoiLayout->setSpacing(5);

    aoiWidthSpin_ = new QSpinBox(aoiPanel_);
    aoiHeightSpin_ = new QSpinBox(aoiPanel_);
    aoiOffsetXSpin_ = new QSpinBox(aoiPanel_);
    aoiOffsetYSpin_ = new QSpinBox(aoiPanel_);
    for (QSpinBox* spin : {aoiWidthSpin_, aoiHeightSpin_, aoiOffsetXSpin_, aoiOffsetYSpin_}) {
        spin->setRange(1, 100000);
        spin->setSuffix(" px");
    }
    aoiOffsetXSpin_->setRange(0, 100000);
    aoiOffsetYSpin_->setRange(0, 100000);

    auto addAoiRow = [&](const QString& label, QSpinBox* spin) {
        QHBoxLayout* row = new QHBoxLayout();
        QLabel* lbl = new QLabel(label, aoiPanel_);
        lbl->setMinimumWidth(64);
        row->addWidget(lbl);
        row->addWidget(spin, 1);
        aoiLayout->addLayout(row);
    };
    addAoiRow("Width", aoiWidthSpin_);
    addAoiRow("Height", aoiHeightSpin_);
    addAoiRow("Offset X", aoiOffsetXSpin_);
    addAoiRow("Offset Y", aoiOffsetYSpin_);

    // Draw mode: drag a rectangle on the frame to define the region freely.
    aoiDrawBtn_ = new QToolButton(aoiPanel_);
    aoiDrawBtn_->setText("Draw on Frame");
    aoiDrawBtn_->setCheckable(true);
    aoiDrawBtn_->setCursor(Qt::PointingHandCursor);
    aoiDrawBtn_->setFocusPolicy(Qt::NoFocus);
    aoiDrawBtn_->setToolTip("Drag on the frame to define the AOI region, then Apply.");
    aoiDrawBtn_->setStyleSheet(QString(
        "QToolButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px;"
        " padding: 6px 10px; font-size: 11px; font-weight: 600; }"
        "QToolButton:hover { background-color: %4; }"
        "QToolButton:checked { background-color: %5; color: %6; border-color: %5; }")
        .arg(toRgba(tc.btnBg, 255), tc.text, tc.border, toRgba(tc.btnHover, 255),
             toRgba(tc.primary, 45), tc.primary));
    connect(aoiDrawBtn_, &QToolButton::toggled, this, &DetailView::onAoiDrawToggled);
    aoiLayout->addWidget(aoiDrawBtn_);

    // Manual apply: editing only previews (overlay + limits); the camera is
    // stopped/restarted once when the operator presses this button.
    aoiApplyBtn_ = new QPushButton("Apply AOI", aoiPanel_);
    aoiApplyBtn_->setCursor(Qt::PointingHandCursor);
    aoiApplyBtn_->setFocusPolicy(Qt::NoFocus);
    connect(aoiApplyBtn_, &QPushButton::clicked, this, &DetailView::onAoiApplyClicked);
    aoiLayout->addWidget(aoiApplyBtn_);

    // Route finished drags from the overlay into the spin boxes.
    aoiOverlay_->onRegionDrawn = [this](int w, int h, int ox, int oy) {
        onAoiRegionDrawn(w, h, ox, oy);
    };

    for (QSpinBox* spin : {aoiWidthSpin_, aoiHeightSpin_, aoiOffsetXSpin_, aoiOffsetYSpin_}) {
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
            if (populatingAoi_) {
                return;
            }
            // Size edits shrink the valid offset range: re-clamp it so the
            // requested region can never exceed the sensor.
            updateAoiOffsetLimits();
            refreshAoiOverlay();
            updateAoiApplyState();
        });
    }

    updateAoiApplyState();
    aoiPanel_->hide();
    aoiChip_->raise();
    aoiPanel_->raise();
    cameraWidget_->installEventFilter(this);
    repositionAoiOverlay();
}

void DetailView::buildRoiControls() {
    const ThemeColors tc = CameraConfig::getThemeColors();

    // ROI overlay above the AOI sensor-context overlay; draws the analysis
    // region and (in draw mode) collects point-to-point vertices.
    roiOverlay_ = new RoiRegionOverlay(cameraWidget_);
    roiOverlay_->setGeometry(cameraWidget_->rect());
    roiOverlay_->hide();

    // ROI chip under the AOI chip (top-right stack).
    roiChip_ = new QToolButton(cameraWidget_);
    roiChip_->setCheckable(true);
    roiChip_->setText("ROI");
    roiChip_->setCursor(Qt::PointingHandCursor);
    roiChip_->setStyleSheet(aoiOverlayStyle(toRgba(tc.btnBg, 230)));
    connect(roiChip_, &QToolButton::toggled, this, [this](bool checked) {
        if (!checked && roiDrawBtn_ && roiDrawBtn_->isChecked()) {
            roiDrawBtn_->setChecked(false);
        }
        roiPanel_->setVisible(checked);
        if (checked) {
            repositionRoiPanel();
        }
        refreshRoiOverlay();
    });

    // Floating ROI panel.
    roiPanel_ = new QFrame(cameraWidget_);
    roiPanel_->setObjectName("roiPanel");
    roiPanel_->setStyleSheet(QString(
        "QFrame#roiPanel { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
        "QLabel { color: %3; font-size: 11px; }"
        "QCheckBox { color: %3; font-size: 11px; spacing: 4px; }")
        .arg(toRgba(tc.btnBg, 245), tc.border, tc.text));
    QVBoxLayout* roiLayout = new QVBoxLayout(roiPanel_);
    roiLayout->setContentsMargins(10, 8, 10, 8);
    roiLayout->setSpacing(5);

    roiStatusLabel_ = new QLabel(roiPanel_);
    roiStatusLabel_->setWordWrap(true);
    roiLayout->addWidget(roiStatusLabel_);

    // Draw / Whole frame / Clear row.
    roiDrawBtn_ = new QToolButton(roiPanel_);
    roiDrawBtn_->setText("Draw Region");
    roiDrawBtn_->setCheckable(true);
    roiDrawBtn_->setCursor(Qt::PointingHandCursor);
    roiDrawBtn_->setFocusPolicy(Qt::NoFocus);
    roiDrawBtn_->setToolTip("Click point-to-point on the frame; double-click"
                            " (or Enter / Finish) closes the region, right-click"
                            " removes the last point, Esc cancels.");
    const QString toolStyle = QString(
        "QToolButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px;"
        " padding: 4px 8px; font-size: 11px; font-weight: 600; }"
        "QToolButton:hover { background-color: %4; }"
        "QToolButton:checked { background-color: %5; color: %6; border-color: %5; }")
        .arg(toRgba(tc.btnBg, 255), tc.text, tc.border, toRgba(tc.btnHover, 255),
             toRgba(tc.primary, 45), tc.primary);
    roiDrawBtn_->setStyleSheet(toolStyle);
    connect(roiDrawBtn_, &QToolButton::toggled, this, &DetailView::onRoiDrawToggled);

    roiWholeBtn_ = new QPushButton("Whole Frame", roiPanel_);
    roiWholeBtn_->setCursor(Qt::PointingHandCursor);
    roiWholeBtn_->setFocusPolicy(Qt::NoFocus);
    roiWholeBtn_->setStyleSheet(QString(
        "QPushButton { background-color: transparent; color: %1; border: 1px solid %2;"
        " border-radius: 6px; padding: 4px 8px; font-size: 11px; }"
        "QPushButton:hover { background-color: %3; }")
        .arg(tc.primary, tc.border, toRgba(tc.btnHover, 255)));
    connect(roiWholeBtn_, &QPushButton::clicked, this, &DetailView::onRoiWholeFrameClicked);

    roiClearBtn_ = new QPushButton("Clear", roiPanel_);
    roiClearBtn_->setCursor(Qt::PointingHandCursor);
    roiClearBtn_->setFocusPolicy(Qt::NoFocus);
    roiClearBtn_->setStyleSheet(roiWholeBtn_->styleSheet());
    connect(roiClearBtn_, &QPushButton::clicked, this, &DetailView::onRoiClearClicked);

    // Finish commits an in-progress polygon (same as double-click / Enter);
    // visible only while drawing so it doubles as an in-panel escape hatch.
    roiFinishBtn_ = new QPushButton("Finish", roiPanel_);
    roiFinishBtn_->setCursor(Qt::PointingHandCursor);
    roiFinishBtn_->setFocusPolicy(Qt::NoFocus);
    roiFinishBtn_->setStyleSheet(roiWholeBtn_->styleSheet());
    connect(roiFinishBtn_, &QPushButton::clicked, this, [this]() {
        if (roiOverlay_) {
            if (!roiOverlay_->finishDrawing() && roiOverlay_->isDrawing()) {
                updateRoiStatus();  // still < 3 points — remind the operator
            }
        }
        updateRoiFinishState();
    });

    QHBoxLayout* roiBtnRow = new QHBoxLayout();
    roiBtnRow->setSpacing(4);
    roiBtnRow->addWidget(roiDrawBtn_);
    roiBtnRow->addWidget(roiWholeBtn_, 1);
    roiBtnRow->addWidget(roiClearBtn_);
    roiLayout->addLayout(roiBtnRow);

    QHBoxLayout* finishRow = new QHBoxLayout();
    finishRow->setSpacing(4);
    finishRow->addStretch(1);
    finishRow->addWidget(roiFinishBtn_);
    roiLayout->addLayout(finishRow);

    // Recorded-review scope toggles.
    roiCurvesCheck_ = new QCheckBox("Restrict review curves to region", roiPanel_);
    roiCurvesCheck_->setChecked(roiMaskCurves_);
    roiHitsCheck_ = new QCheckBox("Restrict defect hits to region", roiPanel_);
    roiHitsCheck_->setChecked(roiMaskHits_);
    roiLayout->addWidget(roiCurvesCheck_);
    roiLayout->addWidget(roiHitsCheck_);
    connect(roiCurvesCheck_, &QCheckBox::toggled, this, &DetailView::onRoiScopeChanged);
    connect(roiHitsCheck_, &QCheckBox::toggled, this, &DetailView::onRoiScopeChanged);

    // Route finished polygon edits into the stored region + status + signal.
    roiOverlay_->onRegionClosed = [this](const QVector<QPointF>& roi) {
        onRoiMaskDrawn(roi);
    };
    roiOverlay_->onDrawingCancelled = [this]() {
        if (roiDrawBtn_) {
            roiDrawBtn_->setChecked(false);
        }
        updateRoiStatus();
        updateRoiFinishState();
    };
    roiOverlay_->onScratchChanged = [this](int count) {
        updateRoiFinishState();
        if (roiStatusLabel_ && roiOverlay_ && roiOverlay_->isDrawing()) {
            roiStatusLabel_->setStyleSheet("QLabel { color: #81C784; font-size: 11px;"
                                           " font-weight: 600; }");
            if (count < 3) {
                roiStatusLabel_->setText(QString("Drawing: click to add points"
                                                  " (%1 so far) — right-click"
                                                  " removes the last point.")
                                             .arg(count));
            } else {
                roiStatusLabel_->setText("Drawing: double-click, Enter or Finish"
                                         " to close the region.");
            }
        }
    };

    roiPanel_->hide();
    roiFinishBtn_->hide();
    // Z-order: overlays draw on the video; panels sit above them so their
    // buttons stay clickable while drawing; chips on top so an open panel can
    // never cover the control that closes it.
    aoiChip_->raise();
    aoiPanel_->raise();
    roiOverlay_->raise();
    roiPanel_->raise();
    roiChip_->raise();
    updateRoiFinishState();
    refreshRoiOverlay();
    updateRoiStatus();
}

void DetailView::onRoiDrawToggled(bool checked) {
    if (checked) {
        beginRoiDraw();
    } else {
        endRoiDraw();
    }
    updateRoiFinishState();
}

void DetailView::beginRoiDraw() {
    if (!roiOverlay_ || !roiChip_) {
        return;
    }
    roiChip_->setChecked(true);  // ensure the panel (and overlay) is up
    if (!roiHasContent_) {
        roiDrawBtn_->setChecked(false);
        if (roiStatusLabel_) {
            roiStatusLabel_->setText("No live frame yet — wait for the image to"
                                     " appear before drawing the region.");
        }
        refreshRoiOverlay();
        return;
    }
    roiOverlay_->setFrameSize(roiFrameW_, roiFrameH_);
    roiOverlay_->setDrawing(true);
    roiOverlay_->setVisible(true);
}

void DetailView::endRoiDraw() {
    if (roiOverlay_) {
        roiOverlay_->setDrawing(false);
        refreshRoiOverlay();
    }
}

void DetailView::onRoiMaskDrawn(const QVector<QPointF>& roi) {
    if (roiDrawBtn_) {
        roiDrawBtn_->setChecked(false);  // uncheck but keep panel open
    }
    endRoiDraw();
    updateRoiFinishState();
    if (roi.size() >= 3) {
        roiNorm_ = roi;
        updateRoiStatus();
        refreshRoiOverlay();
        emitRoiState();
    }
}

void DetailView::onRoiWholeFrameClicked() {
    // Leaving draw mode (if active): the explicit selection replaces the
    // hand-drawn polygon the operator was starting.
    endRoiDraw();
    if (roiDrawBtn_) {
        roiDrawBtn_->setChecked(false);
    }
    updateRoiFinishState();
    // Whole delivered frame: full 0..1 rectangle (equivalent to today's
    // whole-frame analysis, but now explicit and required).
    roiNorm_ = {QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)};
    updateRoiStatus();
    refreshRoiOverlay();
    emitRoiState();
}

void DetailView::onRoiClearClicked() {
    endRoiDraw();
    if (roiDrawBtn_) {
        roiDrawBtn_->setChecked(false);
    }
    updateRoiFinishState();
    roiNorm_.clear();
    updateRoiStatus();
    refreshRoiOverlay();
    emitRoiState();
}

void DetailView::updateRoiFinishState() {
    if (!roiFinishBtn_) {
        return;
    }
    const bool drawing = roiOverlay_ && roiOverlay_->isDrawing();
    const int pts = drawing ? roiOverlay_->scratchCount() : 0;
    roiFinishBtn_->setVisible(drawing);
    roiFinishBtn_->setEnabled(pts >= 3);
    if (drawing) {
        roiFinishBtn_->setToolTip(pts >= 3
            ? "Close the region with the points drawn so far."
            : QString("Need %1 more point(s) before the region can be closed.")
                  .arg(3 - pts));
    }
    if (roiPanel_ && roiPanel_->isVisible()) {
        repositionRoiPanel();
    }
}

void DetailView::onRoiScopeChanged() {
    roiMaskCurves_ = roiCurvesCheck_->isChecked();
    roiMaskHits_ = roiHitsCheck_->isChecked();
    emitRoiState();
}

void DetailView::emitRoiState() {
    if (currentCameraId_ < 0) {
        return;
    }
    emit detectionRoiChanged(currentCameraId_, roiNorm_, roiMaskCurves_, roiMaskHits_);
}

void DetailView::refreshRoiOverlay() {
    if (!roiOverlay_ || !roiChip_) {
        return;
    }
    // The ROI is drawn over the delivered frame: the overlay must map against
    // what the user actually sees, so prefer the live image size and fall back
    // to the configured region size before the first frame arrives.
    const QSize img = cameraWidget_ ? cameraWidget_->currentImageSize() : QSize();
    if (img.isValid() && !img.isEmpty()) {
        roiFrameW_ = img.width();
        roiFrameH_ = img.height();
    }
    roiOverlay_->setFrameSize(roiFrameW_, roiFrameH_);
    roiOverlay_->setPolygon(roiNorm_);
    // The polygon drawer (region + scratch preview) only appears while the ROI
    // button is open; closing it hides the overlay from the live frame.
    const bool show = currentCameraId_ >= 0 && roiHasContent_ && roiChip_->isChecked();
    roiOverlay_->setVisible(show);
}

void DetailView::updateRoiStatus() {
    if (!roiStatusLabel_) {
        return;
    }
    if (roiNorm_.size() < 3) {
        roiStatusLabel_->setText("No inspection region — analysis is PAUSED. "
                                 "Draw the area to analyze or use Whole Frame.");
        roiStatusLabel_->setStyleSheet("QLabel { color: #FFB74D; font-size: 11px; font-weight: 600; }");
    } else {
        roiStatusLabel_->setText(QString("Region: %1 points — analysis runs inside it.")
                                     .arg(roiNorm_.size()));
        roiStatusLabel_->setStyleSheet("QLabel { color: #81C784; font-size: 11px; font-weight: 600; }");
    }
}

void DetailView::repositionRoiPanel() {
    if (!cameraWidget_ || !roiChip_ || !roiPanel_) {
        return;
    }
    const QRect vg = cameraWidget_->rect();
    const int margin = 8;
    const int pw = 260;
    const int ph = roiPanel_->sizeHint().height();
    // Anchor below the ROI chip (itself below the AOI chip) so the panel never
    // covers the chip that closes it.
    const QWidget* anchor = roiChip_->isVisible()
                                ? static_cast<const QWidget*>(roiChip_)
                                : static_cast<const QWidget*>(aoiChip_);
    int x = vg.right() - pw - margin;
    int y = (anchor ? anchor->geometry().bottom() : vg.top() + margin) + 6;
    if (y + ph > vg.bottom() - margin) {
        y = vg.bottom() - ph - margin;  // keep the panel inside the frame
    }
    roiPanel_->setGeometry(x, y, pw, ph);
    // Chips must stay above the panels (and the panel above the drawing
    // overlay) no matter which was created first.
    roiOverlay_->raise();
    roiPanel_->raise();
    roiChip_->raise();
    if (aoiChip_) {
        aoiChip_->raise();
    }
}

void DetailView::onAoiChipToggled(bool checked) {
    aoiPanel_->setVisible(checked);
    if (!checked && aoiDrawBtn_ && aoiDrawBtn_->isChecked()) {
        aoiDrawBtn_->setChecked(false);  // also disables overlay draw mode
    }
    if (aoiOverlay_) {
        aoiOverlay_->setVisible(checked);
        if (checked) {
            refreshAoiOverlay();
            updateAoiApplyState();
        }
    }
    if (checked) {
        repositionAoiOverlay();
    }
}

void DetailView::onAoiApplyClicked() {
    if (currentCameraId_ < 0) {
        return;
    }
    emitAoiValues();
}

void DetailView::onAoiDrawToggled(bool checked) {
    if (aoiOverlay_) {
        aoiOverlay_->setDrawEnabled(checked);
    }
}

void DetailView::onAoiRegionDrawn(int width, int height, int offsetX, int offsetY) {
    if (currentCameraId_ < 0) {
        return;
    }
    // Feed the drawn rectangle into the spin boxes (and therefore into the
    // overlay + Apply button). Size first so offset limits adapt, then offsets.
    populatingAoi_ = true;
    aoiWidthSpin_->setValue(qBound(1, width, aoiMaxW_));
    aoiHeightSpin_->setValue(qBound(1, height, aoiMaxH_));
    populatingAoi_ = false;
    updateAoiOffsetLimits();
    aoiOffsetXSpin_->setValue(qBound(0, offsetX, aoiOffsetXSpin_->maximum()));
    aoiOffsetYSpin_->setValue(qBound(0, offsetY, aoiOffsetYSpin_->maximum()));
    refreshAoiOverlay();
    updateAoiApplyState();
}

void DetailView::emitAoiValues() {
    if (currentCameraId_ < 0) {
        return;
    }
    emit aoiValuesChanged(currentCameraId_, aoiWidthSpin_->value(), aoiHeightSpin_->value(),
                          aoiOffsetXSpin_->value(), aoiOffsetYSpin_->value());
    // Remember what the camera now has so the button can show pending state.
    appliedAoiW_ = aoiWidthSpin_->value();
    appliedAoiH_ = aoiHeightSpin_->value();
    appliedAoiOX_ = aoiOffsetXSpin_->value();
    appliedAoiOY_ = aoiOffsetYSpin_->value();
    updateAoiApplyState();
}

void DetailView::updateAoiApplyState() {
    if (!aoiApplyBtn_) {
        return;
    }
    aoiPending_ = (aoiWidthSpin_->value() != appliedAoiW_
                   || aoiHeightSpin_->value() != appliedAoiH_
                   || aoiOffsetXSpin_->value() != appliedAoiOX_
                   || aoiOffsetYSpin_->value() != appliedAoiOY_);

    const ThemeColors tc = CameraConfig::getThemeColors();
    if (aoiPending_) {
        aoiApplyBtn_->setText("Apply AOI (Stop & Start)");
        aoiApplyBtn_->setEnabled(true);
        aoiApplyBtn_->setStyleSheet(
            QString("QPushButton { background-color: %1; color: %2; border: 1px solid %1;"
                    " border-radius: 6px; padding: 6px 10px; font-size: 11px; font-weight: 600; }"
                    "QPushButton:hover { background-color: %3; }")
                .arg(tc.primary, toRgba(tc.bg, 230), toRgba(tc.primary, 200)));
    } else {
        aoiApplyBtn_->setText("AOI Applied");
        aoiApplyBtn_->setEnabled(false);
        aoiApplyBtn_->setStyleSheet(
            QString("QPushButton { background-color: transparent; color: %1; border: 1px solid %2;"
                    " border-radius: 6px; padding: 6px 10px; font-size: 11px; }")
                .arg(tc.border, tc.border));
    }
}

void DetailView::setAoiInfo(int maxW, int maxH, int width, int height, int offsetX, int offsetY) {
    if (!aoiPanel_) {
        return;
    }
    populatingAoi_ = true;
    aoiMaxW_ = maxW > 0 ? maxW : 1;
    aoiMaxH_ = maxH > 0 ? maxH : 1;
    aoiWidthSpin_->setRange(1, aoiMaxW_);
    aoiHeightSpin_->setRange(1, aoiMaxH_);
    aoiWidthSpin_->setValue(qBound(1, width, aoiMaxW_));
    aoiHeightSpin_->setValue(qBound(1, height, aoiMaxH_));
    updateAoiOffsetLimits();
    aoiOffsetXSpin_->setValue(qBound(0, offsetX, aoiOffsetXSpin_->maximum()));
    aoiOffsetYSpin_->setValue(qBound(0, offsetY, aoiOffsetYSpin_->maximum()));
    populatingAoi_ = false;
    // The camera currently runs the values we just seeded - nothing pending.
    appliedAoiW_ = aoiWidthSpin_->value();
    appliedAoiH_ = aoiHeightSpin_->value();
    appliedAoiOX_ = aoiOffsetXSpin_->value();
    appliedAoiOY_ = aoiOffsetYSpin_->value();
    aoiPending_ = false;
    updateAoiApplyState();
    refreshAoiOverlay();
    repositionAoiOverlay();
}

void DetailView::refreshAoiOverlay() {
    if (!aoiOverlay_ || !aoiPanel_) {
        return;
    }
    aoiOverlay_->setValues(aoiMaxW_, aoiMaxH_,
                           aoiWidthSpin_->value(), aoiHeightSpin_->value(),
                           aoiOffsetXSpin_->value(), aoiOffsetYSpin_->value());
    // Fallback geometry for the ROI overlay before the first frame arrives;
    // refreshRoiOverlay() overrides with the actual delivered image size.
    roiFrameW_ = aoiWidthSpin_->value();
    roiFrameH_ = aoiHeightSpin_->value();
    roiHasContent_ = true;
    refreshRoiOverlay();
}

void DetailView::updateAoiOffsetLimits() {
    if (!aoiPanel_ || aoiMaxW_ <= 0 || aoiMaxH_ <= 0) {
        return;
    }
    // Offset X + Width <= sensor width (same for Y): when the region grows,
    // the remaining offset room shrinks with it. Clamp the current offset too
    // so the camera is never asked for an out-of-sensor position.
    const int maxOX = std::max(0, aoiMaxW_ - aoiWidthSpin_->value());
    const int maxOY = std::max(0, aoiMaxH_ - aoiHeightSpin_->value());

    const bool xb = aoiOffsetXSpin_->blockSignals(true);
    const bool yb = aoiOffsetYSpin_->blockSignals(true);
    aoiOffsetXSpin_->setRange(0, maxOX);
    aoiOffsetYSpin_->setRange(0, maxOY);
    aoiOffsetXSpin_->setValue(qBound(0, aoiOffsetXSpin_->value(), maxOX));
    aoiOffsetYSpin_->setValue(qBound(0, aoiOffsetYSpin_->value(), maxOY));
    aoiOffsetXSpin_->blockSignals(xb);
    aoiOffsetYSpin_->blockSignals(yb);
}

void DetailView::repositionAoiOverlay() {
    if (!cameraWidget_ || !aoiChip_) {
        return;
    }
    const QRect vg = cameraWidget_->rect();
    const int margin = 8;
    const int chipW = 56;
    const int chipH = 26;
    aoiChip_->setGeometry(vg.right() - chipW - margin, vg.top() + margin, chipW, chipH);

    // ROI chip sits below the AOI chip (same right-edge stack).
    if (roiChip_) {
        roiChip_->setGeometry(vg.right() - chipW - margin,
                              aoiChip_->geometry().bottom() + 6, chipW, chipH);
        if (roiPanel_ && roiPanel_->isVisible()) {
            repositionRoiPanel();
        }
    }

    if (aoiPanel_ && aoiPanel_->isVisible()) {
        const int pw = 228;
        const int ph = aoiPanel_->sizeHint().height();
        int x = vg.right() - pw - margin;
        int y = aoiChip_->geometry().bottom() + 6;
        if (y + ph > vg.bottom() - margin) {
            y = vg.bottom() - ph - margin;  // keep the panel inside the frame
        }
        aoiPanel_->setGeometry(x, y, pw, ph);
    }
}

bool DetailView::eventFilter(QObject* obj, QEvent* event) {
    if (obj == cameraWidget_) {
        if (event->type() == QEvent::Resize) {
            if (aoiOverlay_) {
                aoiOverlay_->setGeometry(cameraWidget_->rect());
                aoiOverlay_->update();
            }
            if (roiOverlay_) {
                roiOverlay_->setGeometry(cameraWidget_->rect());
                roiOverlay_->update();
            }
            repositionAoiOverlay();
            if (roiChip_) {
                repositionRoiPanel();
            }
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            // While the AOI panel or the ROI panel is open (or ROI drawing is
            // active), swallow double-clicks on the frame so adjusting values
            // / drawing can't accidentally bounce back to the grid.
            if ((aoiChip_ && aoiChip_->isChecked())
                || (roiOverlay_ && roiOverlay_->isDrawing())
                || (roiChip_ && roiChip_->isChecked())) {
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

QString DetailView::aoiOverlayStyle(const QString& bgColor) const {
    const ThemeColors tc = CameraConfig::getThemeColors();
    return QString(
        "QToolButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 13px;"
        " font-size: 11px; font-weight: 600; padding: 2px 10px; }"
        "QToolButton:hover { background-color: %4; }"
        "QToolButton:checked { background-color: %5; color: %6; border-color: %5; }")
        .arg(bgColor, tc.text, tc.border, toRgba(tc.btnHover, 255), toRgba(tc.primary, 40),
             tc.primary);
}

void DetailView::setCamera(int cameraId, const CameraInfo& info, CameraWidget* videoSource) {
    currentCameraId_ = cameraId;
    
    // Set camera ID on our video widget so it receives the correct frames
    cameraWidget_->setCameraId(cameraId);
    
    // Update Info Panel with comprehensive camera information
    lblId_->setText(QString("%1").arg(info.id, 2, 10, QChar('0')));
    lblLocation_->setText(info.location);
    lblSide_->setText(info.side);
    lblDescription_->setText(info.name);
    lblModel_->setText(info.model);
    lblIP_->setText(info.ipAddress);
    lblImageSize_->setText(info.imageSize);
    lblFPS_->setText(QString("%1 FPS").arg(static_cast<double>(info.fps), 0, 'f', 1));
    updateTemperature(info.temperature);
    
    // Set overlay text to match LiveDashboard
    cameraWidget_->setOverlayText(CameraConfig::getCameraLabel(cameraId));
    applyLiveViewTypography();
    
    // Exposure range is set from live camera node limits when available.
    // Keep a broad default here until MainWindow applies the actual device range.
    spinExposure_->blockSignals(true);
    sliderExposure_->blockSignals(true);
    spinExposure_->setRange(100, 1000000);
    sliderExposure_->setRange(100, 1000000);
    spinExposure_->blockSignals(false);
    sliderExposure_->blockSignals(false);
    
    setGainPresentation("Gain", false);

    // Show the AOI + ROI chips on the video frame for this camera.
    if (aoiChip_) {
        aoiChip_->setChecked(false);
        aoiChip_->setVisible(true);
        repositionAoiOverlay();
    }
    if (roiChip_) {
        roiChip_->setChecked(false);
        roiChip_->setVisible(true);
        repositionRoiPanel();
    }

    // Load current parameter values from info (config-backed defaults)
    // Block ALL slider AND spinbox signals to prevent parameterChanged from firing
    // during initialization — this avoids triggering StopGrabbing/StartGrabbing
    // on the acquisition loop while the detail view is being set up.
    // NOTE: MainWindow::showDetail calls setParameterValues() right after setCamera()
    // with live values from the camera NodeMap, so we just initialize to safe defaults here.
    spinGain_->blockSignals(true);
    sliderGain_->blockSignals(true);
    spinGain_->setValue(0);
    sliderGain_->setValue(0);
    spinGain_->blockSignals(false);
    sliderGain_->blockSignals(false);

    spinExposure_->blockSignals(true);
    sliderExposure_->blockSignals(true);
    spinExposure_->setValue(100);
    sliderExposure_->setValue(100);
    spinExposure_->blockSignals(false);
    sliderExposure_->blockSignals(false);

    spinGamma_->blockSignals(true);
    sliderGamma_->blockSignals(true);
    spinGamma_->setValue(1.0);
    sliderGamma_->setValue(100);
    spinGamma_->blockSignals(false);
    sliderGamma_->blockSignals(false);

    spinContrast_->blockSignals(true);
    sliderContrast_->blockSignals(true);
    spinContrast_->setValue(1.0);
    sliderContrast_->setValue(100);
    spinContrast_->blockSignals(false);
    sliderContrast_->blockSignals(false);
}

void DetailView::clearCamera() {
    currentCameraId_ = -1;
    cameraWidget_->setCameraId(-1);
    cameraWidget_->clearFrame();

    lblId_->setText("-");
    lblLocation_->setText("-");
    lblSide_->setText("-");
    lblDescription_->setText("-");
    lblModel_->setText("-");
    lblIP_->setText("-");
    lblImageSize_->setText("-");
    lblFPS_->setText("-");
    lblConfiguredFramePeriod_->setText("-");
    lblDisplayFps_->setText("-");
    lblActualFramePeriod_->setText("-");
    lblTemp_->setText("-");
    lblTemp_->setStyleSheet("");

    if (aoiChip_) {
        aoiChip_->setChecked(false);
        aoiChip_->setVisible(false);
    }
    if (aoiPanel_) {
        aoiPanel_->hide();
    }
    if (aoiOverlay_) {
        aoiOverlay_->hide();
        aoiOverlay_->setDrawEnabled(false);
    }
    if (aoiDrawBtn_) {
        aoiDrawBtn_->setChecked(false);
    }
    // Reset ROI state: no camera -> no region shown/editable.
    roiHasContent_ = false;
    roiNorm_.clear();
    if (roiOverlay_) {
        roiOverlay_->hide();
        roiOverlay_->setDrawing(false);
        roiOverlay_->setPolygon({});
    }
    if (roiChip_) {
        roiChip_->setChecked(false);
        roiChip_->setVisible(false);
    }
    if (roiPanel_) {
        roiPanel_->hide();
    }
    if (roiDrawBtn_) {
        roiDrawBtn_->setChecked(false);
    }
    if (roiStatusLabel_) {
        updateRoiStatus();
    }
    aoiPending_ = false;
    appliedAoiW_ = appliedAoiH_ = appliedAoiOX_ = appliedAoiOY_ = 0;
    if (aoiApplyBtn_) {
        updateAoiApplyState();
    }
}

void DetailView::updateTemperature(double temp) {
    if (!lblTemp_) return;

    TempStatus::Status status = TempStatus::classify(temp);

    QString text;
    QString color;

    switch (status) {
        case TempStatus::Error:
            text  = QString("⛔ %1 °C  ERROR").arg(temp, 0, 'f', 1);
            color = "#ff4444";
            break;
        case TempStatus::Critical:
            text  = QString("⚠ %1 °C  CRITICAL").arg(temp, 0, 'f', 1);
            color = "#ff9900";
            break;
        case TempStatus::Ok:
            text  = QString("%1 °C").arg(temp, 0, 'f', 1);
            color = "";  // Default theme text
            break;
        default:  // Unknown / N/A
            text  = "N/A";
            color = "#888888";
            break;
    }

    lblTemp_->setText(text);
    if (color.isEmpty()) {
        lblTemp_->setStyleSheet("");  // Revert to theme
    } else {
        lblTemp_->setStyleSheet(QString("color: %1; font-weight: bold;").arg(color));
    }
}

void DetailView::setAdminMode(bool isAdmin) {
    isAdmin_ = isAdmin;
    
    if (controlGroup_) {
        controlGroup_->setEnabled(isAdmin);
    }
    
    // Enable/disable spinboxes
    spinGain_->setEnabled(isAdmin);
    spinExposure_->setEnabled(isAdmin);
    spinGamma_->setEnabled(isAdmin);
    spinContrast_->setEnabled(isAdmin);
    
    // Enable/disable sliders
    sliderGain_->setEnabled(isAdmin);
    sliderExposure_->setEnabled(isAdmin);
    sliderGamma_->setEnabled(isAdmin);
    sliderContrast_->setEnabled(isAdmin);
    
    btnSave_->setEnabled(isAdmin);
    btnLoad_->setEnabled(isAdmin);

    if (aoiChip_) {
        aoiChip_->setEnabled(isAdmin);
        if (!isAdmin && aoiChip_->isChecked()) {
            aoiChip_->setChecked(false);
        }
    }
    if (roiChip_) {
        roiChip_->setEnabled(isAdmin);
        if (!isAdmin && roiChip_->isChecked()) {
            roiChip_->setChecked(false);
        }
        if (!isAdmin && roiOverlay_) {
            roiOverlay_->setDrawing(false);
        }
    }
}

void DetailView::setDetectionRoi(const QVector<QPointF>& roi, bool maskCurves, bool maskHits) {
    roiNorm_ = roi;
    roiMaskCurves_ = maskCurves;
    roiMaskHits_ = maskHits;
    if (roiCurvesCheck_) {
        roiCurvesCheck_->setChecked(maskCurves);
    }
    if (roiHitsCheck_) {
        roiHitsCheck_->setChecked(maskHits);
    }
    updateRoiStatus();
    refreshRoiOverlay();
}

void DetailView::setGainPresentation(const QString& title, bool isRaw) {
    gainIsRaw_ = isRaw;
    if (gainGroup_) {
        gainGroup_->setTitle(title);
        gainGroup_->setToolTip(isRaw ? "Camera uses GainRaw units from the device node." : QString());
    }
    spinGain_->blockSignals(true);
    sliderGain_->blockSignals(true);
    spinGain_->setDecimals(isRaw ? 0 : 1);
    spinGain_->setSingleStep(isRaw ? 1.0 : 0.1);
    spinGain_->blockSignals(false);
    sliderGain_->blockSignals(false);
}

void DetailView::setParameterValues(double gain, double exposureUs, double gamma, double contrast) {
    // Block signals while we update controls so we don't emit parameterChanged back
    const double gainClamped = std::max(spinGain_->minimum(), std::min(gain, spinGain_->maximum()));
    spinGain_->blockSignals(true);
    sliderGain_->blockSignals(true);
    spinGain_->setValue(gainClamped);
    sliderGain_->setValue(gainIsRaw_
        ? static_cast<int>(std::llround(gainClamped))
        : static_cast<int>(std::llround(gainClamped * 10.0)));
    spinGain_->blockSignals(false);
    sliderGain_->blockSignals(false);

    int expClamped = qBound(spinExposure_->minimum(), static_cast<int>(exposureUs), spinExposure_->maximum());
    spinExposure_->blockSignals(true);
    sliderExposure_->blockSignals(true);
    spinExposure_->setValue(expClamped);
    sliderExposure_->setValue(expClamped);
    spinExposure_->blockSignals(false);
    sliderExposure_->blockSignals(false);

    spinGamma_->blockSignals(true);
    sliderGamma_->blockSignals(true);
    spinGamma_->setValue(gamma);
    sliderGamma_->setValue(static_cast<int>(gamma * 100));
    spinGamma_->blockSignals(false);
    sliderGamma_->blockSignals(false);

    spinContrast_->blockSignals(true);
    sliderContrast_->blockSignals(true);
    spinContrast_->setValue(contrast);
    sliderContrast_->setValue(static_cast<int>(contrast * 100));
    spinContrast_->blockSignals(false);
    sliderContrast_->blockSignals(false);
}

void DetailView::setGainRange(double minGain, double maxGain) {
    const double safeMin = std::max(0.0, minGain);
    const double safeMax = std::max(safeMin, maxGain);
    const int sliderMin = gainIsRaw_
        ? static_cast<int>(std::floor(safeMin))
        : static_cast<int>(std::floor(safeMin * 10.0));
    const int sliderMax = gainIsRaw_
        ? static_cast<int>(std::ceil(safeMax))
        : static_cast<int>(std::ceil(safeMax * 10.0));

    spinGain_->blockSignals(true);
    sliderGain_->blockSignals(true);
    spinGain_->setRange(safeMin, safeMax);
    sliderGain_->setRange(sliderMin, std::max(sliderMin, sliderMax));
    spinGain_->blockSignals(false);
    sliderGain_->blockSignals(false);
}

void DetailView::setExposureRange(int minUs, int maxUs) {
    const int safeMin = std::max(1, minUs);
    const int safeMax = std::max(safeMin, maxUs);
    spinExposure_->blockSignals(true);
    sliderExposure_->blockSignals(true);
    spinExposure_->setRange(safeMin, safeMax);
    sliderExposure_->setRange(safeMin, safeMax);
    spinExposure_->setValue(qBound(safeMin, spinExposure_->value(), safeMax));
    sliderExposure_->setValue(qBound(safeMin, sliderExposure_->value(), safeMax));
    spinExposure_->blockSignals(false);
    sliderExposure_->blockSignals(false);
}

void DetailView::onSaveParams() {
    qDebug() << "[DetailView] Save parameters for camera" << currentCameraId_;
    emit saveParametersRequested(currentCameraId_);
}

void DetailView::onLoadParams() {
    qDebug() << "[DetailView] Load parameters for camera" << currentCameraId_;
    emit loadParametersRequested(currentCameraId_);
}

void DetailView::onBackClicked() {
    emit backRequested();
}

// Accessor for MainWindow to connect signals
CameraWidget* DetailView::videoWidget() {
    return cameraWidget_;
}

void DetailView::onAnalysisClicked() {
    emit analysisRequested();
}

void DetailView::onSnapshotClicked() {
    emit snapshotRequested(currentCameraId_);
}

QString DetailView::fpsMismatchStyle(double fps) {
    // Amber when the camera can't sustain the configured rate (typically
    // exposure time eating into the frame period) — healthy stays default.
    if (fps < 0 || acquisitionFps_ <= 0.0) return QString();
    const bool limited = fps < 0.9 * acquisitionFps_;
    return limited ? QStringLiteral("QLabel { color: #FFB74D; font-weight: 600; }")
                   : QString();
}

void DetailView::setDeviceInfo(const QString& model, const QString& ip, const QString& imageSize) {
    lblModel_->setText(model);
    lblIP_->setText(ip);
    lblImageSize_->setText(imageSize);
}

void DetailView::setDisplayFps(double fps) {
    displayFps_ = fps;
    if (fps < 0) {
        lblDisplayFps_->setText("N/A");
        lblActualFramePeriod_->setText("N/A");
    } else {
        lblDisplayFps_->setText(QString::number(fps, 'f', 1));
        lblActualFramePeriod_->setText(QString::number(1000000.0 / fps, 'f', 1) + " µs");
    }
    const QString style = fpsMismatchStyle(fps);
    lblDisplayFps_->setStyleSheet(style);
    lblActualFramePeriod_->setStyleSheet(style);
}

void DetailView::updateTheme() {
    applyLiveViewTypography();

    if (aoiChip_) {
        const ThemeColors tc = CameraConfig::getThemeColors();
        if (aoiOverlay_) {
            aoiOverlay_->update();
        }
        if (aoiApplyBtn_) {
            updateAoiApplyState();
        }
        if (aoiDrawBtn_) {
            aoiDrawBtn_->setStyleSheet(QString(
                "QToolButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px;"
                " padding: 6px 10px; font-size: 11px; font-weight: 600; }"
                "QToolButton:hover { background-color: %4; }"
                "QToolButton:checked { background-color: %5; color: %6; border-color: %5; }")
                .arg(toRgba(tc.btnBg, 255), tc.text, tc.border, toRgba(tc.btnHover, 255),
                     toRgba(tc.primary, 45), tc.primary));
        }
        aoiChip_->setStyleSheet(aoiOverlayStyle(toRgba(tc.btnBg, 230)));
        aoiPanel_->setStyleSheet(QString(
            "QFrame#aoiPanel { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
            "QLabel { color: %3; font-size: 11px; }"
            "QSpinBox { background-color: %4; color: %3; border: 1px solid %2; border-radius: 4px;"
            " padding: 2px 4px; font-size: 11px; }")
            .arg(toRgba(tc.btnBg, 245), tc.border, tc.text, toRgba(tc.bg, 120)));
        repositionAoiOverlay();
    }

    if (roiChip_) {
        const ThemeColors rt = CameraConfig::getThemeColors();
        roiChip_->setStyleSheet(aoiOverlayStyle(toRgba(rt.btnBg, 230)));
        if (roiPanel_) {
            roiPanel_->setStyleSheet(QString(
                "QFrame#roiPanel { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
                "QLabel { color: %3; font-size: 11px; }"
                "QCheckBox { color: %3; font-size: 11px; spacing: 4px; }")
                .arg(toRgba(rt.btnBg, 245), rt.border, rt.text));
        }
        if (roiOverlay_) {
            roiOverlay_->update();
        }
    }

    if (cameraWidget_) {
        cameraWidget_->update();
    }
}

void DetailView::setAcquisitionFps(double fps) {
    acquisitionFps_ = fps;
    // 0 = node unreadable/disabled (e.g. AcquisitionFrameRateEnable off) —
    // same as unknown; also avoids 1e6/0 = "inf µs".
    if (fps <= 0) {
        lblFPS_->setText("N/A");
        lblConfiguredFramePeriod_->setText("N/A");
        setDisplayFps(displayFps_);
        return;
    }
    lblFPS_->setText(QString("%1 FPS").arg(fps, 0, 'f', 1));
    lblConfiguredFramePeriod_->setText(QString::number(1000000.0 / fps, 'f', 1) + " µs");
    // Re-evaluate the mismatch highlight against the new configured rate.
    setDisplayFps(displayFps_);
}

void DetailView::applyLiveViewTypography() {
    const LiveViewCardStyle style = CameraConfig::getLiveViewCardStyle();

    auto makeFont = [](const QString& family, int pixelSize, bool bold) {
        QFont font(family);
        font.setPixelSize(pixelSize);
        font.setBold(bold);
        return font;
    };

    const QFont detailTitleFont = makeFont(style.detailTitleFontFamily, style.detailTitleFontSize, true);
    const QFont sectionFont = makeFont(style.detailSectionFontFamily, style.detailSectionFontSize, true);

    if (cameraWidget_) {
        cameraWidget_->setOverlayFont(detailTitleFont);
    }
    if (infoGroup_) {
        infoGroup_->setFont(sectionFont);
    }
    if (controlGroup_) {
        controlGroup_->setFont(sectionFont);
    }
}
