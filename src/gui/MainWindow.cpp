#include "MainWindow.h"
#include "CameraInfo.h"
#include "../config/CameraConfig.h"
#include "ConfigDialog.h"
#include "../communication/OpcUaClientService.h"
#include "../core/EventDatabase.h"
#include <cstdlib>
#include <QToolBar>
#include <QStatusBar>
#include <QDateTime>
#include <QDebug>
#include <QMenu>
#include <QMenuBar>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QDesktopWidget>
#include <QApplication>
#include <QFont>
#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QFormLayout>
#include <QAction>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScreen>
#include <QStyle>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWindow>
#include <QtConcurrent>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QStorageInfo>

#ifdef Q_OS_LINUX
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
// X11 headers define macros (KeyPress, FocusIn, ...) that collide with Qt
// identifiers used in this file; drop them right after the includes.
#undef KeyPress
#undef KeyRelease
#undef ButtonPress
#undef ButtonRelease
#undef MotionNotify
#undef EnterNotify
#undef LeaveNotify
#undef FocusIn
#undef FocusOut
#undef None
#undef True
#undef False
#undef Success
#undef Status
#endif

// Register cv::Mat for signal/slot
Q_DECLARE_METATYPE(cv::Mat)

namespace {

// Queries the real Caps Lock state from the window system (X11). Returns false
// when the state can't be queried; the login dialog's letter-key heuristic then
// acts as a fallback.
bool systemCapsLockOn() {
#ifdef Q_OS_LINUX
    // Opened once and kept for the app lifetime (intentional; avoids reconnecting
    // to the X server on every 400ms poll).
    static Display* display = XOpenDisplay(nullptr);
    if (!display) {
        return false;
    }
    unsigned int state = 0;
    const bool ok = XkbGetIndicatorState(display, XkbUseCoreKbd, &state) == 0;  // X11 Success
    return ok && (state & 0x01);  // bit 0 = Caps Lock indicator
#else
    return false;
#endif
}

// Draws a simple eye icon for the password visibility toggle.
QIcon makeEyeIcon(bool visible, const QColor& color) {
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    // Eye outline
    p.setPen(QPen(color, 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(1.5, 4.5, 13.0, 7.0));
    // Pupil
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QRectF(6.0, 7.0, 4.0, 4.0));
    if (!visible) {
        // Strike-through for the "hidden" state
        p.setPen(QPen(color, 1.6));
        p.drawLine(3.0, 13.0, 13.0, 3.0);
    }
    p.end();
    return QIcon(pm);
}

// Draws the classic Caps Lock glyph (an up-arrow over a baseline bar) as a
// compact indicator icon for the login dialog footer.
QIcon makeCapsLockIcon(const QColor& color) {
    QPixmap pm(18, 18);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    // Up arrow
    p.drawLine(9.0, 3.0, 9.0, 12.0);
    p.drawLine(9.0, 3.0, 4.0, 8.0);
    p.drawLine(9.0, 3.0, 14.0, 8.0);
    // Baseline bar
    p.drawLine(4.0, 16.0, 14.0, 16.0);
    p.end();
    return QIcon(pm);
}

class AdminLoginDialog : public QDialog {
public:
    explicit AdminLoginDialog(QWidget* parent = nullptr)
        : QDialog(parent) {
        setWindowTitle("Administrator Login");
        setModal(true);
        setFixedWidth(340);

        QVBoxLayout* root = new QVBoxLayout(this);
        root->setContentsMargins(16, 16, 16, 12);
        root->setSpacing(10);

        // ── Header ──
        QLabel* titleLabel = new QLabel("Administrator Login", this);
        QFont titleFont = titleLabel->font();
        titleFont.setBold(true);
        titleFont.setPointSize(titleFont.pointSize() + 2);
        titleLabel->setFont(titleFont);
        root->addWidget(titleLabel);

        QLabel* subtitleLabel = new QLabel("Enter your credentials to access system configuration.", this);
        subtitleLabel->setWordWrap(true);
        root->addWidget(subtitleLabel);
        root->addSpacing(4);

        // ── Form (native Qt layout) ──
        QFormLayout* form = new QFormLayout();
        form->setContentsMargins(0, 0, 0, 0);
        form->setHorizontalSpacing(10);
        form->setVerticalSpacing(8);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        usernameCombo_ = new QComboBox(this);
        usernameCombo_->setEditable(true);
        usernameCombo_->addItem("admin");
        usernameCombo_->setCurrentText("admin");
        usernameCombo_->setMinimumWidth(220);
        form->addRow("Username:", usernameCombo_);

        // Password field with a native trailing-action show/hide toggle.
        passwordEdit_ = new QLineEdit(this);
        passwordEdit_->setEchoMode(QLineEdit::Password);
        passwordEdit_->setPlaceholderText("Enter password...");
        passwordEdit_->setMinimumWidth(220);
        const QColor fg = palette().color(QPalette::WindowText);
        toggleAction_ = passwordEdit_->addAction(makeEyeIcon(false, fg), QLineEdit::TrailingPosition);
        toggleAction_->setCheckable(true);
        toggleAction_->setToolTip("Show password");
        connect(toggleAction_, &QAction::toggled, this, [this, fg](bool checked) {
            passwordEdit_->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
            toggleAction_->setIcon(makeEyeIcon(checked, fg));
            toggleAction_->setToolTip(checked ? "Hide password" : "Show password");
        });
        form->addRow("Password:", passwordEdit_);

        root->addLayout(form);
        root->addSpacing(4);

        // ── Error message (native label, no heavy styling) ──
        errorLabel_ = new QLabel(this);
        errorLabel_->setVisible(false);
        errorLabel_->setWordWrap(true);
        errorLabel_->setStyleSheet("color: #E06060;");
        root->addWidget(errorLabel_);

        root->addStretch();

        // ── Native dialog button box, with a compact Caps Lock icon on the left ──
        QDialogButtonBox* buttonBox = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        loginButton_ = buttonBox->button(QDialogButtonBox::Ok);
        loginButton_->setText("Login");
        loginButton_->setDefault(true);
        connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

        capsLockIconLabel_ = new QLabel(this);
        capsLockIconLabel_->setVisible(false);
        capsLockIconLabel_->setToolTip("Caps Lock is ON — passwords are case-sensitive.");
        capsLockIconLabel_->setPixmap(makeCapsLockIcon(QColor("#E0A800")).pixmap(18, 18));

        QHBoxLayout* footerLayout = new QHBoxLayout();
        footerLayout->setContentsMargins(0, 0, 0, 0);
        footerLayout->addWidget(capsLockIconLabel_, 0, Qt::AlignVCenter);
        footerLayout->addStretch();
        footerLayout->addWidget(buttonBox);
        root->addLayout(footerLayout);

        // ── Focus ──
        passwordEdit_->setFocus();
        connect(passwordEdit_, &QLineEdit::returnPressed, this, &QDialog::accept);

        // ── Caps Lock tracking ──
        // On Linux, poll the window-system state so the hint appears the moment
        // Caps Lock is toggled, without requiring a letter key press first.
        // Elsewhere, the event filter's letter-key heuristic owns the state.
        passwordEdit_->installEventFilter(this);
#ifdef Q_OS_LINUX
        capsLockTimer_ = new QTimer(this);
        capsLockTimer_->setInterval(400);
        connect(capsLockTimer_, &QTimer::timeout, this, [this]() {
            capsLockOn_ = systemCapsLockOn();
            updateCapsLockLabel();
        });
        capsLockTimer_->start();
#endif
        updateCapsLockLabel();
    }

    QString username() const {
        return usernameCombo_->currentText().trimmed();
    }

    QString password() const {
        return passwordEdit_->text();
    }

    void showInvalidPasswordState() {
        errorLabel_->setText("Incorrect username or password. Please try again.");
        errorLabel_->setVisible(true);
        passwordEdit_->clear();
        passwordEdit_->setFocus();
    }

    void clearInvalidPasswordState() {
        errorLabel_->clear();
        errorLabel_->setVisible(false);
    }

protected:
    void showEvent(QShowEvent* event) override {
        QDialog::showEvent(event);
        // Refresh from the window system so the state is right on open.
        capsLockOn_ = systemCapsLockOn();
        updateCapsLockLabel();
    }

    bool eventFilter(QObject* obj, QEvent* event) override {
        if (obj == passwordEdit_) {
            if (event->type() == QEvent::KeyPress) {
                // Qt5 has no CapsLock keyboard modifier, so infer it from letter
                // key presses: a letter that renders uppercase while Shift is not
                // held (or lowercase while Shift is held) means Caps Lock is on.
                auto* keyEvent = static_cast<QKeyEvent*>(event);
                if (keyEvent->text().size() == 1) {
                    const QChar ch = keyEvent->text().at(0);
                    if (ch.isLetter()) {
                        const bool shiftHeld = keyEvent->modifiers().testFlag(Qt::ShiftModifier);
                        capsLockOn_ = ch.isUpper() != shiftHeld;
                        updateCapsLockLabel();
                    }
                }
            } else if (event->type() == QEvent::FocusIn) {
                updateCapsLockLabel();
            }
        }
        return QDialog::eventFilter(obj, event);
    }

private:
    void updateCapsLockLabel() {
        if (!capsLockIconLabel_) {
            return;
        }
        capsLockIconLabel_->setVisible(capsLockOn_);
    }

    QComboBox* usernameCombo_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QAction* toggleAction_ = nullptr;
    QLabel* capsLockIconLabel_ = nullptr;
    QLabel* errorLabel_ = nullptr;
    QPushButton* loginButton_ = nullptr;
    QTimer* capsLockTimer_ = nullptr;
    bool capsLockOn_ = false;
};
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), currentFps_(0.0), frameCount_(0), isAdmin_(false) {
    
    // Initialize throttle flags
    for (int i = 0; i < 16; ++i) {
        framePending_[i] = false;
    }
    
    qRegisterMetaType<cv::Mat>("cv::Mat");
    qRegisterMetaType<int>("int");
    
    setupUi();
    setupCore();

    // FPS Timer
    connect(&fpsTimer_, &QTimer::timeout, [this]() {
        // Simple FPS calculation
        currentFps_ = frameCount_ * 2.0; // Called every 500ms
        frameCount_ = 0;
        liveDashboard_->updateStatus(currentFps_, false); // Need to update this to support Status per view?

        if (stackedWidget_ && detailView_ && stackedWidget_->currentWidget() == detailView_ && cameraManager_) {
            const int cameraId = detailView_->videoWidget()->cameraId();
            if (cameraId >= 0) {
                detailView_->setAcquisitionFps(cameraManager_->getCameraAcquisitionFps(cameraId));
                detailView_->setDisplayFps(cameraManager_->getCameraFps(cameraId));
                // Identity rows can change when a camera attaches late — keep
                // them live too, so "Not Connected / 0 x 0" never sticks.
                cv::Size res = cameraManager_->getCameraResolution(cameraId);
                detailView_->setDeviceInfo(
                    QString::fromStdString(cameraManager_->getModelName(cameraId)),
                    QString::fromStdString(cameraManager_->getIpAddress(cameraId)),
                    QString("%1 x %2").arg(res.width).arg(res.height));
                detailView_->updateTemperature(cameraManager_->getTemperature(cameraId));
            } else {
                detailView_->setAcquisitionFps(-1.0);
                detailView_->setDisplayFps(-1.0);
            }
        }
    });
    fpsTimer_.start(500);
}

MainWindow::~MainWindow() {
    if (cameraLifecycleWatcher_) {
        cameraLifecycleWatcher_->waitForFinished();
    }

    // Stop camera before destruction
    if (cameraManager_) {
        cameraManager_->stopAcquisition();
    }
}

void MainWindow::raiseAndActivate() {
    if (isMinimized()) {
        setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    }

    show();
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    QMainWindow::closeEvent(event);
}

bool MainWindow::validateSavedCameraConfiguration(QStringList* errors) const {
    // The global Camera Mode selector (or PYLON_CAMEMU) is a gate that only
    // activates the per-card "Emulated" Source setting. In emulation mode the
    // Real + MAC requirement is relaxed: Real cards simply stay offline instead
    // of blocking startup, while Emulated cards attach to the emulated devices.
    const bool emulationMode = CameraConfig::isEmulationActive();
    const std::vector<CameraInfo> cams = CameraConfig::getCameras();
    QMap<QString, QList<int>> ipUsage;
    QMap<QString, QList<int>> macUsage;

    for (const auto& cam : cams) {
        if (cam.source == 2) {
            continue;
        }
        if (emulationMode) {
            continue;
        }

        const QString configuredIp = cam.ipAddress.trimmed();
        if (!configuredIp.isEmpty()) {
            ipUsage[configuredIp].append(cam.id);
        }

        if (cam.source == 1) {
            const QString configuredMac = cam.macAddress.trimmed().toUpper();
            if (configuredMac.isEmpty()) {
                if (errors) {
                    errors->append(QString("Camera ID %1 is set to Real but has no MAC assigned.").arg(cam.id));
                }
            } else {
                macUsage[configuredMac].append(cam.id);
            }
        }
    }

    auto appendDuplicates = [errors](const QString& prefix, const QMap<QString, QList<int>>& usage) {
        if (!errors) {
            return;
        }

        for (auto it = usage.cbegin(); it != usage.cend(); ++it) {
            if (it.value().size() < 2) {
                continue;
            }

            QStringList ids;
            for (int id : it.value()) {
                ids.append(QString::number(id));
            }

            errors->append(QString("%1 %2 is assigned to multiple camera IDs (%3).").arg(prefix, it.key(), ids.join(", ")));
        }
    };

    appendDuplicates("Configured IP", ipUsage);
    appendDuplicates("MAC", macUsage);

    return !errors || errors->isEmpty();
}
void MainWindow::initializeEventController() {
    const double configuredFps = static_cast<double>(CameraConfig::getFps());
    const int preTriggerFrames = CameraConfig::getPreTriggerSeconds() * static_cast<int>(configuredFps);
    const int postTriggerFrames = CameraConfig::getPostTriggerSeconds() * static_cast<int>(configuredFps);
    EventController::instance().initialize(preTriggerFrames, configuredFps, postTriggerFrames);
    // Per-camera FPS truth: the recorder scales each camera's buffers, capture
    // targets, and RAW header fps from the rate frames ACTUALLY arrive at
    // (ResultingFrameRate — exposure/bandwidth-limited). The requested
    // AcquisitionFrameRate is only a target: when exposure time eats into the
    // frame period the real rate drops, and the ring buffer must shrink to
    // match. Falls back to the configured value when the node is unreadable.
    EventController::instance().setCameraFpsProvider(
        [this](int cameraId) -> double {
            return cameraManager_
                ? cameraManager_->getCameraFps(cameraId - 1)
                : 0.0;
        });
}

void MainWindow::applyOpcUaSettings() {
    if (!opcUaClientService_) {
        return;
    }

    opcUaClientService_->stop();
    const OpcUaSettings opcUaSettings = CameraConfig::getOpcUaSettings();
    opcUaClientService_->setSettings(opcUaSettings);

    const QString endpointUrl = opcUaSettings.endpointUrl.trimmed();
    const bool hasNonDefaultEndpoint = !endpointUrl.isEmpty()
        && endpointUrl.compare(QStringLiteral("opc.tcp://127.0.0.1:4840"), Qt::CaseInsensitive) != 0
        && endpointUrl.compare(QStringLiteral("opc.tcp://localhost:4840"), Qt::CaseInsensitive) != 0;

    bool hasEnabledNode = false;
    for (const auto& triggerTag : opcUaSettings.triggerTags) {
        if (triggerTag.enabled && (!triggerTag.nodeId.trimmed().isEmpty() || triggerTag.simulated)) {
            hasEnabledNode = true;
            break;
        }
    }
    if (!hasEnabledNode) {
        for (const auto& speedTag : opcUaSettings.speedTags) {
            if (speedTag.enabled && (!speedTag.nodeId.trimmed().isEmpty() || speedTag.simulated)) {
                hasEnabledNode = true;
                break;
            }
        }
    }

    bool hasSimulatedTag = false;
    for (const auto& triggerTag : opcUaSettings.triggerTags) {
        if (triggerTag.enabled && triggerTag.simulated) {
            hasSimulatedTag = true;
            break;
        }
    }
    if (!hasSimulatedTag) {
        for (const auto& speedTag : opcUaSettings.speedTags) {
            if (speedTag.enabled && speedTag.simulated) {
                hasSimulatedTag = true;
                break;
            }
        }
    }

    // Simulated tags fire without a server, so a default/empty endpoint is fine
    // when at least one enabled tag is simulated.
    if (opcUaSettings.enabled && hasEnabledNode && (hasNonDefaultEndpoint || hasSimulatedTag)) {
        opcUaClientService_->start();
    }
}


void MainWindow::startCameraLifecycleAsync(bool restart, const QString& reason) {
    qInfo() << "[MainWindow] startCameraLifecycleAsync called"
            << "restart=" << restart
            << "reason=" << reason
            << "inProgress=" << cameraLifecycleInProgress_;

    if (!cameraManager_ || cameraLifecycleInProgress_) {
        qWarning() << "[MainWindow] Camera lifecycle request ignored"
                   << "cameraManager=" << (cameraManager_ != nullptr)
                   << "inProgress=" << cameraLifecycleInProgress_;
        // Keep the Control Panel button honest when a request is ignored. If a
        // lifecycle is already running, leave the Connecting… state alone — its
        // own finished handler sets the final Online/Offline state.
        if (analysisView_ && !cameraManager_) {
            analysisView_->setServerRunning(false);
        }
        return;
    }

    QStringList validationErrors;
    if (!validateSavedCameraConfiguration(&validationErrors)) {
        qWarning() << "[MainWindow] Saved camera configuration invalid:" << validationErrors;
        const QString message = QString("Camera startup blocked due to invalid configuration:\n%1").arg(validationErrors.join("\n"));
        statusBar()->showMessage("Camera startup blocked by invalid network configuration.", 5000);
        QMessageBox::warning(this, "Invalid Camera Configuration", message);
        pauseBtn_->setEnabled(false);
        if (analysisView_) {
            analysisView_->setServerRunning(false);
        }
        if (configWindow_) {
            configWindow_->setEnabled(true);
        }
        return;
    }

    if (!cameraLifecycleWatcher_) {
        cameraLifecycleWatcher_ = new QFutureWatcher<bool>(this);
        connect(cameraLifecycleWatcher_, &QFutureWatcher<bool>::finished, this, [this]() {
            cameraLifecycleInProgress_ = false;

            const bool success = cameraLifecycleWatcher_->result();
            qInfo() << "[MainWindow] Camera lifecycle finished. success=" << success;

            // Guard: configWindow_ may have been closed/deleted (e.g. admin logout)
            // while the async camera restart was in progress. Always null-check before use.
            if (success) {
                statusBar()->showMessage("Acquisition running", 3000);
                pauseBtn_->setEnabled(true);
                if (configWindow_) {
                    configWindow_->setEnabled(true);
                }
                // Reflect the real state in the Analysis Control Panel button.
                if (analysisView_) {
                    analysisView_->setServerRunning(true);
                }
                return;
            }

            statusBar()->showMessage("Camera startup failed. Check camera connections and configuration.", 5000);
            pauseBtn_->setEnabled(false);
            if (analysisView_) {
                analysisView_->setServerRunning(false);
            }
            if (configWindow_) {
                configWindow_->setEnabled(true);
            }
        });
    }

    cameraLifecycleInProgress_ = true;
    // Show the intermediate Connecting… state on the Analysis Control Panel
    // button until the async startup resolves to Online or Offline.
    if (analysisView_) {
        analysisView_->setServerConnecting(true);
    }
    pauseBtn_->setEnabled(false);
    if (configWindow_) {
        configWindow_->setEnabled(false);
    }

    statusBar()->showMessage(reason, 0);

    cameraLifecycleWatcher_->setFuture(QtConcurrent::run([manager = cameraManager_.get(), restart]() {
        qInfo() << "[MainWindow] Camera lifecycle worker started. restart=" << restart;
        if (!manager) {
            qWarning() << "[MainWindow] Camera lifecycle worker missing manager";
            return false;
        }

        if (restart) {
            qInfo() << "[MainWindow] Worker stopping acquisition";
            manager->stopAcquisition();
        }

        qInfo() << "[MainWindow] Worker initializing cameras";
        if (!manager->initialize()) {
            qWarning() << "[MainWindow] Worker failed to initialize cameras";
            return false;
        }

        qInfo() << "[MainWindow] Worker starting acquisition";
        manager->startAcquisition();

        const std::vector<CameraInfo> cams = CameraConfig::getCameras();
        qInfo() << "[MainWindow] Worker applying frame rates for" << cams.size() << "cameras";
        for (int i = 0; i < static_cast<int>(cams.size()); ++i) {
            manager->setCameraFrameRate(i, cams[i].fps, cams[i].enableAcquisitionFps);
        }

        manager->setDefectDetectionEnabled(CameraConfig::isDefectDetectionEnabled());
        qInfo() << "[MainWindow] Worker completed successfully";
        return true;
    }));
}

void MainWindow::setupUi() {
    QScreen* targetScreen = windowHandle() ? windowHandle()->screen() : QGuiApplication::primaryScreen();
    if (targetScreen) {
        const QRect screenGeometry = targetScreen->availableGeometry();
        setGeometry(screenGeometry);
    }
    showMaximized();
    setWindowTitle("PaperVision System - Industrial Monitor");
    qInfo() << "Setting up UI...";
    
    // Apply theme globally right at startup
    applyGlobalTheme();

    // --- Central Widget (Create FIRST) ---
    mainTabWidget_ = new QTabWidget(this);
    mainTabWidget_->setTabBarAutoHide(false);
    mainTabWidget_->setDocumentMode(true); // Optional: cleaner look
    setCentralWidget(mainTabWidget_);

    // --- Tab 1: Live Area ---
    QWidget* liveTab = new QWidget();
    QVBoxLayout* liveLayout = new QVBoxLayout(liveTab);
    liveLayout->setContentsMargins(0, 0, 0, 0);

    // Live controls in a single horizontal row to preserve Live View height.
    QHBoxLayout* liveControlsLayout = new QHBoxLayout();
    liveControlsLayout->setContentsMargins(8, 8, 8, 8);
    liveControlsLayout->setSpacing(8);

    triggerBtn_ = new QPushButton("Trigger");
    triggerBtn_->setToolTip("Trigger record (S)");
    connect(triggerBtn_, &QPushButton::clicked, this, &MainWindow::manualTrigger);
    
    snapshotBtn_ = new QPushButton("Snapshot");
    snapshotBtn_->setToolTip("Capture snapshot from Detail View");
    connect(snapshotBtn_, &QPushButton::clicked, this, [this]() {
        if (stackedWidget_->currentWidget() == detailView_) {
            if (cameraManager_) {
                const int cameraId = detailView_->videoWidget()->cameraId();
                if (cameraId >= 0 && cameraId < CameraConfig::getCameraCount()) {
                    cameraManager_->triggerSnapshot(cameraId);
                    statusBar()->showMessage("Snapshot triggered.", 2000);
                } else {
                    statusBar()->showMessage("No valid camera selected.", 2000);
                }
            }
        } else {
            QMessageBox::information(this, "Snapshot", "Snapshots can only be taken from the Detail View.");
        }
    });

    liveControlsLayout->addWidget(triggerBtn_);
    liveControlsLayout->addWidget(snapshotBtn_);
    
    pauseBtn_ = new QPushButton("Pause");
    pauseBtn_->setToolTip("Pause camera grabbing");
    connect(pauseBtn_, &QPushButton::clicked, this, &MainWindow::togglePauseGrab);
    liveControlsLayout->addWidget(pauseBtn_);
     
    QLabel* defectLabel = new QLabel("Trigger on Defect:");
    liveControlsLayout->addWidget(defectLabel);
    
    defectDetectionCheck_ = new ToggleSwitch(this);
    defectDetectionCheck_->setEnabled(isAdmin_); // Linked to Admin
    connect(defectDetectionCheck_, &ToggleSwitch::toggled, [this](bool checked) {
        CameraConfig::setDefectDetectionEnabled(checked);
        if (cameraManager_) {
            cameraManager_->setDefectDetectionEnabled(checked);
        }
        refreshRoiPausedBadges();
    });
    liveControlsLayout->addWidget(defectDetectionCheck_);
    liveControlsLayout->addStretch();
    
    liveLayout->addLayout(liveControlsLayout);

    stackedWidget_ = new QStackedWidget(this);
    
    // Views
    liveDashboard_ = new LiveDashboard(CameraConfig::getCameraCount(), this);
    connect(liveDashboard_, &LiveDashboard::cameraSelected, this, &MainWindow::showDetail);

    detailView_ = new DetailView(this);
    connect(detailView_, &DetailView::backRequested, this, &MainWindow::showGrid);
    connect(detailView_, &DetailView::analysisRequested, [this]() {
         switchView(ViewMode::Analysis);
    });
    connect(detailView_, &DetailView::snapshotRequested, [this](int cameraId) {
        if (cameraManager_) cameraManager_->triggerSnapshot(cameraId);
    });
    // Live parameter adjustment: forward signal to CameraManager
    connect(detailView_, &DetailView::parameterChanged, [this](int cameraId, QString param, double value) {
        if (!cameraManager_) return;
        if (param == "Gain") {
            cameraManager_->setCameraGain(cameraId, value);
        } else if (param == "Exposure") {
            cameraManager_->setCameraExposure(cameraId, value);

            // Exposure affects the camera's resulting rate (exposure-limited
            // fps): keep the ring buffer capacity in sync with the real rate
            // so RAM FRAME reflects what the camera delivers now.
            EventController::instance().updateCameraFps(cameraId + 1);

            std::vector<CameraInfo> cameras = CameraConfig::getCameras();
            if (cameraId >= 0 && cameraId < static_cast<int>(cameras.size())) {
                cameras[cameraId].exposureTimeAbs = value;
                CameraConfig::saveCameras(cameras);
            }
        } else if (param == "Gamma") {
            cameraManager_->setCameraGamma(cameraId, value);
        } else if (param == "Contrast") {
            cameraManager_->setCameraContrast(cameraId, value);
        }
    });
    // AOI adjustments from the Live View overlay. Basler requires the camera
    // to be idle while the ROI is changed, so applyCameraAOI stops, writes the
    // nodes, and restarts the camera automatically. Persist to config and keep
    // the ring-buffer fps estimate in sync (AOI changes the resulting rate).
    connect(detailView_, &DetailView::aoiValuesChanged, [this](int cameraId, int width, int height, int offsetX, int offsetY) {
        if (!cameraManager_) return;
        cameraManager_->applyCameraAOI(cameraId, width, height, offsetX, offsetY);
        std::vector<CameraInfo> cameras = CameraConfig::getCameras();
        if (cameraId >= 0 && cameraId < static_cast<int>(cameras.size())) {
            cameras[cameraId].width = width;
            cameras[cameraId].height = height;
            cameras[cameraId].offsetX = offsetX;
            cameras[cameraId].offsetY = offsetY;
            // The drawn detection ROI lives in delivered-frame coordinates; a
            // changed AOI crop shifts what those pixels show, so the region
            // must be redrawn for the new geometry.
            cameras[cameraId].detectionRoi.clear();
            CameraConfig::saveCameras(cameras);
            if (cameraManager_) {
                cameraManager_->setCameraDetectionRoi(cameraId, QVector<QPointF>());
            }
            // Drop the drawn region from the overlay/panel too (it now refers
            // to the old crop geometry).
            detailView_->setDetectionRoi(QVector<QPointF>(), true, true);
            refreshRoiPausedBadges();
        }
        EventController::instance().updateCameraFps(cameraId + 1);
    });
    // Software detection ROI (analysis region) edits from the Live View ROI
    // panel: persist per-camera and push into the live scan. An AOI geometry
    // change clears the region (the drawn area would no longer match content),
    // which the aoiValuesChanged handler above performs after each apply.
    connect(detailView_, &DetailView::detectionRoiChanged,
            [this](int cameraId, const QVector<QPointF>& roi, bool maskCurves, bool maskHits) {
        std::vector<CameraInfo> cameras = CameraConfig::getCameras();
        if (cameraId >= 0 && cameraId < static_cast<int>(cameras.size())) {
            cameras[cameraId].detectionRoi = roi;
            cameras[cameraId].roiMaskCurves = maskCurves;
            cameras[cameraId].roiMaskHits = maskHits;
            CameraConfig::saveCameras(cameras);
        }
        if (cameraManager_) {
            cameraManager_->setCameraDetectionRoi(cameraId, roi);
        }
        refreshRoiPausedBadges();
        statusBar()->showMessage(
            roi.size() >= 3
                ? QString("Inspection region set for Camera %1 (%2 pts)").arg(cameraId + 1).arg(roi.size())
                : QString("Camera %1 inspection region cleared — analysis paused")
                      .arg(cameraId + 1),
            3500);
    });
    connect(detailView_, &DetailView::saveParametersRequested, [this](int cameraId) {
        if (!cameraManager_) return;
        bool ok = cameraManager_->saveParameters(cameraId);
        statusBar()->showMessage(ok ? QString("📁 Parameters saved for Camera %1").arg(cameraId + 1)
                                    : QString("⚠ Failed to save parameters for Camera %1").arg(cameraId + 1), 3000);
    });
    connect(detailView_, &DetailView::loadParametersRequested, [this](int cameraId) {
        if (!cameraManager_) return;
        bool ok = cameraManager_->loadParameters(cameraId);
        if (ok) {
            // Readback current values from Pylon nodemap and refresh UI spinboxes
            auto p = cameraManager_->getCameraParams(cameraId);
            detailView_->setGainPresentation(p.gainDisplayName, p.gainIsRaw);
            detailView_->setGainRange(p.gainMin, p.gainMax);
            detailView_->setExposureRange(static_cast<int>(p.exposureMinUs), static_cast<int>(p.exposureMaxUs));
            detailView_->setParameterValues(p.gain, p.exposureUs, p.gamma, p.contrast);
            std::vector<CameraInfo> cameras = CameraConfig::getCameras();
            if (cameraId >= 0 && cameraId < static_cast<int>(cameras.size())) {
                cameras[cameraId].exposureTimeAbs = p.exposureUs;
                CameraConfig::saveCameras(cameras);
            }
            statusBar()->showMessage(QString("✅ Parameters loaded for Camera %1").arg(cameraId + 1), 3000);
        } else {
            statusBar()->showMessage(QString("⚠ No saved parameters found for Camera %1").arg(cameraId + 1), 3000);
        }
    });

    stackedWidget_->addWidget(liveDashboard_); // Index 0
    stackedWidget_->addWidget(detailView_);    // Index 1
    
    liveLayout->addWidget(stackedWidget_);
    mainTabWidget_->addTab(liveTab, "Live View");

    // --- Tab 2: Analysis Area ---
    analysisView_ = new AnalysisView(CameraConfig::getCameraCount(), this);
    connect(analysisView_, &AnalysisView::recordAllToggled, this, &MainWindow::toggleRecording);
    connect(analysisView_, &AnalysisView::manualTriggerRequested, this, &MainWindow::manualTrigger);
    // Control Panel: the Server button is the master switch for the live vision
    // system (start/stop camera acquisition; triggers, recording, and defect
    // detection are all gated on streaming frames).
    connect(analysisView_, &AnalysisView::serverToggled, this, [this](bool running) {
        if (!cameraManager_) {
            return;
        }
        if (running) {
            // Server Offline->Online == system restart for the timing config:
            // re-read the saved fallback fps and rebuild the recorder buffers
            // exactly like an app start, then bring the cameras back with it.
            CameraManager::resetAppStartFallbackFps();
            initializeEventController();
            startCameraLifecycleAsync(false, "Starting camera acquisition...");
        } else {
            cameraManager_->stopAcquisition();
            pauseBtn_->setEnabled(false);
            statusBar()->showMessage("Vision system offline — camera acquisition stopped.", 3000);
        }
    });
    
    mainTabWidget_->addTab(analysisView_, "Analysis View");
    
    // Connect tab change logic
    connect(mainTabWidget_, &QTabWidget::currentChanged, [this](int index) {
        if (index == 0) {
            analysisView_->clearData(); // Memory optimization
            
            statusBar()->showMessage(QString("Live View - Grid %1x%2")
                .arg(liveDashboard_->getCurrentRows())
                .arg(liveDashboard_->getCurrentCols()));
        } else if (index == 1) {
            statusBar()->showMessage("Analysis View");
        } else if (configWindow_ && index == mainTabWidget_->indexOf(configWindow_)) {
            statusBar()->showMessage("System Configuration");
        }
    });

    ensureConfigTab();
    updateConfigTabAccess();

    // --- Menu Bar (Create AFTER central widget) ---
    QMenuBar* menu = menuBar();
    menu->setNativeMenuBar(false); // Force menu bar to appear in window, not system menu

    // File Menu
    QMenu* fileMenu = menu->addMenu("File");
    QAction* exitAction = fileMenu->addAction("Exit");
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    // View Menu - Grid Layout Options
    QMenu* viewMenu = menu->addMenu("View");
    
    // Grid layouts (removed "Live View" and "Analysis View" from submenu)
    viewMenu->addAction("Grid 1x1", [this]() { changeLayout(1, 1); });
    viewMenu->addAction("Grid 1x2", [this]() { changeLayout(1, 2); });
    viewMenu->addAction("Grid 2x1", [this]() { changeLayout(2, 1); });
    viewMenu->addAction("Grid 2x2", [this]() { changeLayout(2, 2); });
    viewMenu->addAction("Grid 2x3", [this]() { changeLayout(2, 3); });
    viewMenu->addAction("Grid 3x3", [this]() { changeLayout(3, 3); });
    viewMenu->addAction("Grid 4x3", [this]() { changeLayout(4, 3); });
    viewMenu->addAction("Grid 5x4", [this]() { changeLayout(5, 4); });
    viewMenu->addSeparator(); // Divider added
    customLayoutAction_ = viewMenu->addAction("Custom Grid...");
    connect(customLayoutAction_, &QAction::triggered, this, &MainWindow::promptCustomLayout);

    // Settings Menu
    QMenu* settingsMenu = menu->addMenu("Settings");

    // Configuration Window
    configAction_ = settingsMenu->addAction("System Configuration");
    connect(configAction_, &QAction::triggered, this, &MainWindow::openSystemConfiguration);

    // Docs Menu — in-app documentation (how it works, workflows, frameworks, tutorial)
    QMenu* docsMenu = menu->addMenu("Docs");
    QAction* docsAction = docsMenu->addAction("Documentation");
    connect(docsAction, &QAction::triggered, this, &MainWindow::showDocs);

    // Help Menu
    QMenu* helpMenu = menu->addMenu("Help");
    aboutAction_ = helpMenu->addAction("About");
    connect(aboutAction_, &QAction::triggered, this, &MainWindow::showAbout);

    // Administrator Login/Logout anchored to the far right of the main tab
    // bar row (same line as the "Live View | Analysis View" tabs), so it is
    // visible and reachable from every view. Text follows admin state via
    // updateAdminStatusIndicator().
    // Geometry is computed in code (styleAdminLoginButton), NOT from QSS
    // padding: corner widgets are sized from the sizeHint at insertion, so
    // stylesheet-driven metric changes left the widget stale and clipped
    // the text ("Logout" rendered as ".ogou").
    // QToolButton + autoRaise: the widget type Qt natively expects in
    // corner/toolbar slots — correct small-size rendering, no raised-button
    // chrome, and no stylesheet-driven geometry drift (the earlier QPushButton
    // attempts clipped inside the corner widget).
    adminLoginButton_ = new QToolButton(this);
    adminLoginButton_->setObjectName("adminLoginButton");
    adminLoginButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    adminLoginButton_->setAutoRaise(true);
    adminLoginButton_->setCursor(Qt::PointingHandCursor);
    adminLoginButton_->setToolTip("Admin Login");
    connect(adminLoginButton_, &QToolButton::clicked, this, &MainWindow::toggleAdmin);
    styleAdminLoginButton();
    mainTabWidget_->setCornerWidget(adminLoginButton_, Qt::TopRightCorner);

    // Emulation-mode badge: lives on the bottom status line (shared by Live
    // View, Detail View, and Analysis View). Visible only when the app runs on
    // emulated cameras (PYLON_CAMEMU or the Camera Mode selector in Settings).
    emulationBadge_ = new QLabel("\u26A1 Emulation Mode", this);
    emulationBadge_->setObjectName("emulationBadge");
    emulationBadge_->setToolTip("Running on emulated cameras (no real hardware). "
                                "Configured in Settings > Recording & Triggers > Camera Mode.");
    emulationBadge_->setVisible(CameraConfig::isEmulationActive());
    statusBar()->addPermanentWidget(emulationBadge_);

    // Low-disk warning badge: hidden while storage is healthy, turns amber/red
    // when free space drops below the configured threshold (Settings > Recording
    // & Triggers > Record Storage). Clicking it opens the Recording & Triggers
    // settings page. Refreshed on a slow timer.
    diskBadge_ = new QPushButton(this);
    diskBadge_->setObjectName("diskBadge");
    diskBadge_->setFlat(true);
    diskBadge_->setCursor(Qt::PointingHandCursor);
    diskBadge_->setVisible(false);
    connect(diskBadge_, &QPushButton::clicked, this, &MainWindow::openRecordingSettings);
    statusBar()->addPermanentWidget(diskBadge_);
    connect(&diskBadgeTimer_, &QTimer::timeout, this, &MainWindow::refreshDiskBadge);
    diskBadgeTimer_.start(15000);
    refreshDiskBadge();

    adminStatusLabel_ = new QLabel(this);
    adminStatusLabel_->setObjectName("adminStatusLabel");
    statusBar()->addPermanentWidget(adminStatusLabel_);
    updateAdminStatusIndicator();

    // Initial Status
    statusBar()->showMessage("System Initialized");
}

void MainWindow::pushDetectionRoisToManager() {
    if (!cameraManager_) {
        return;
    }
    const std::vector<CameraInfo> cams = CameraConfig::getCameras();
    for (int i = 0; i < static_cast<int>(cams.size()); ++i) {
        cameraManager_->setCameraDetectionRoi(i, cams[i].detectionRoi);
    }
}

void MainWindow::refreshRoiPausedBadges() {
    if (!liveDashboard_) {
        return;
    }
    // The inspection region only gates analysis while defect detection is on;
    // when it is off no camera scans, so clear every badge.
    const bool detectionEnabled = CameraConfig::isDefectDetectionEnabled();
    const std::vector<CameraInfo> cams = CameraConfig::getCameras();
    for (int i = 0; i < static_cast<int>(cams.size()); ++i) {
        liveDashboard_->setCameraRoiPaused(i,
            detectionEnabled && cams[i].detectionRoi.size() < 3);
    }
}

void MainWindow::setupCore() {
    // 1. Initialize Components
    cameraManager_ = std::make_unique<CameraManager>();
    if (configWindow_) {
        // ConfigDialog is created in setupUi(), before this manager exists.
        // Re-wire so device-settings/IP panels get the live manager.
        configWindow_->setCameraManager(cameraManager_.get());
    }
    opcUaClientService_ = std::make_unique<OpcUaClientService>(this);
    if (configWindow_) {
        configWindow_->setOpcUaRuntimeSource(opcUaClientService_.get());
    }
    // Feed the live machine speed to the recorder so defect/OPC UA triggers can
    // spatially align every camera's window to when the defect passes it.
    EventController::instance().setSpeedProvider([this](double* mPerMin) {
        return opcUaClientService_ && opcUaClientService_->currentSpeedMperMin(mPerMin);
    });
    // Feed the full speed profile (position mm + actual local speed per drive)
    // so every event snapshots all anchors at trigger time — the defect
    // projector later interpolates the local speed anywhere along the machine.
    EventController::instance().setSpeedAnchorsProvider([this](
            std::vector<EventDatabase::SpeedAnchorSnapshot>* anchors) {
        if (!opcUaClientService_ || !anchors) {
            return false;
        }
        QVector<OpcUaClientService::SpeedSample> samples;
        if (!opcUaClientService_->currentSpeedAnchors(&samples)) {
            return false;
        }
        anchors->clear();
        anchors->reserve(static_cast<size_t>(samples.size()));
        for (const OpcUaClientService::SpeedSample& s : samples) {
            EventDatabase::SpeedAnchorSnapshot anchor;
            anchor.positionMm = s.positionMm;
            anchor.speedValue = s.value;
            anchor.tagName = s.tagName;
            anchor.nodeId = s.nodeId;
            anchors->push_back(anchor);
        }
        return !anchors->empty();
    });
    imageBuffer_ = std::make_unique<ImageBuffer>(200, 1024, 1040);
    defectDetector_ = std::make_unique<DefectDetector>();
    videoEncoder_ = std::make_unique<VideoEncoder>();

    // 2. Connect Signals
    connect(this, &MainWindow::frameReady, this, &MainWindow::handleFrame, Qt::QueuedConnection);

    cameraManager_->registerStatusCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            statusBar()->showMessage(QString::fromStdString(msg), 5000);
        }, Qt::QueuedConnection);
    });

    if (opcUaClientService_) {
        connect(opcUaClientService_.get(), &OpcUaClientService::statusChanged, this, [this](const QString& message) {
            statusBar()->showMessage(message, 5000);
        });
        connect(opcUaClientService_.get(), &OpcUaClientService::triggerReceived, this,
                [this](const OpcUaClientService::TriggerEvent& event) {
            EventController::TriggerContext triggerContext;
            triggerContext.reason = event.tagName.trimmed().isEmpty()
                ? QStringLiteral("OPC UA Trigger")
                : QString("OPC UA: %1").arg(event.tagName.trimmed());
            triggerContext.source = event.source.trimmed().isEmpty() ? QStringLiteral("opcua") : event.source.trimmed();
            triggerContext.triggerTagName = event.tagName.trimmed();
            triggerContext.triggerTagNodeId = event.nodeId.trimmed();
            triggerContext.speedTagName = event.speedTagName.trimmed();
            triggerContext.speedTagNodeId = event.speedTagNodeId.trimmed();
            triggerContext.speedUnit = event.speedUnit.trimmed();
            triggerContext.speedSampleTimeUtc = event.speedSampleTimeUtc;
            triggerContext.speedValue = event.speedValue;
            triggerContext.hasSpeed = event.hasSpeed;
            triggerContext.group = event.group;
            triggerContext.triggerPositionMm = event.positionMm;
            triggerContext.speedStale = event.speedStale;
            triggerContext.positionDirectionSign = event.positionDirectionSign;
            triggerContext.speedAnchors.reserve(static_cast<size_t>(event.speedAnchors.size()));
            for (const OpcUaClientService::SpeedSample& s : event.speedAnchors) {
                EventDatabase::SpeedAnchorSnapshot anchor;
                anchor.positionMm = s.positionMm;
                anchor.speedValue = s.value;
                anchor.tagName = s.tagName;
                anchor.nodeId = s.nodeId;
                triggerContext.speedAnchors.push_back(anchor);
            }
            EventController::instance().triggerEvent(triggerContext);
            statusBar()->showMessage(QString("%1 triggered recording").arg(triggerContext.reason), 3000);
        });
    }

    cameraManager_->registerCallback([this](int cameraId, const cv::Mat& frame) {
        if (cameraId >= 0 && cameraId < 16) {
            bool expected = false;
            if (frame.empty()) {
                emit frameReady(cameraId, cv::Mat());
                return;
            }
            if (framePending_[cameraId].compare_exchange_strong(expected, true)) {
                emit frameReady(cameraId, frame.clone());
            }
        }
    });

    initializeEventController();

    // Self-heal per-camera ring-buffer capacities: the capacity is sized from
    // a one-shot ResultingFrameRate read when a camera's first frame arrives,
    // and during camera (re)starts that read can catch pylon mid-configuration
    // and report a transient rate (observed: 13.8 instead of 35 -> buffer
    // locked at 207 frames / 5.9s instead of 525 / 15s, so that camera's
    // events came out shorter). Reconcile against the settled rate every few
    // seconds; updateCameraFps is a no-op when the rate is unchanged and
    // skips active captures.
    auto* fpsReconcileTimer = new QTimer(this);
    connect(fpsReconcileTimer, &QTimer::timeout, this, [this]() {
        if (!cameraManager_) {
            return;
        }
        const std::vector<CameraInfo> cams = CameraConfig::getCameras();
        for (int i = 0; i < static_cast<int>(cams.size()); ++i) {
            // Repair free-running cameras first, then reconcile the ring
            // buffer capacity with the (possibly corrected) rate.
            cameraManager_->ensureConfiguredFrameRate(i);
            EventController::instance().updateCameraFps(i + 1);
        }
    });
    fpsReconcileTimer->start(3000);

    // 4. Start Camera
    cameraManager_->setDefectDetectionEnabled(CameraConfig::isDefectDetectionEnabled());
    // Seed the live defect scan with each camera's configured analysis region
    // (empty = no region -> that camera's analysis is paused until drawn).
    pushDetectionRoisToManager();
    // Mark the grid tiles that are paused for a missing inspection region.
    refreshRoiPausedBadges();
    startCameraLifecycleAsync(false, "Starting camera acquisition...");
    applyOpcUaSettings();


    // 5. Register Temperature Alert Callback (Basler App Note AW00138003000)
    cameraManager_->registerTemperatureAlertCallback(
        [this](int camId, double temp, CameraManager::TemperatureStatus status) {
            QMetaObject::invokeMethod(this, [this, camId, temp, status]() {
                // Update grid tile badge
                if (liveDashboard_) {
                    liveDashboard_->updateCameraTemperature(camId, temp, status);
                }
                // Update DetailView if it's currently showing this camera
                if (detailView_ && detailView_->videoWidget() &&
                    detailView_->videoWidget()->cameraId() == camId) {
                    detailView_->updateTemperature(temp);
                }
                // Status bar alert for Critical or Error
                if (status == TempStatus::Error) {
                    statusBar()->showMessage(
                        QString("🔴 OVER-TEMP ERROR: Camera %1 at %2°C — Sensor may power down!").arg(camId + 1).arg(temp, 0, 'f', 1),
                        10000);
                } else if (status == TempStatus::Critical) {
                    statusBar()->showMessage(
                        QString("🟠 CRITICAL TEMP: Camera %1 at %2°C — approaching sensor limit!").arg(camId + 1).arg(temp, 0, 'f', 1),
                        8000);
                }
            }, Qt::QueuedConnection);
        }
    );

    // Register PTP status callback (sampled with the temperature monitor) so
    // the Live View tiles show each camera's IEEE 1588 clock state.
    cameraManager_->registerPtpStatusCallback(
        [this](int camId, const CameraManager::PtpStatus& ptp) {
            QMetaObject::invokeMethod(this, [this, camId, ptp]() {
                if (liveDashboard_) {
                    liveDashboard_->updateCameraPtpStatus(camId, ptp);
                }

                // Offset threshold alert: warn on the status bar when a slave
                // camera's offset from the PTP master exceeds 1 ms (the value
                // Basler suggests as a "sufficiently synchronized" check). Fire
                // on the rising edge only, and quietly clear once it recovers.
                static constexpr int64_t kMaxPtpOffsetFromMasterNs = 1000000; // 1 ms
                const bool overThreshold =
                    ptp.available && ptp.enabled
                    && ptp.state == QLatin1String("Slave")
                    && ptp.offsetFromMasterNs > kMaxPtpOffsetFromMasterNs;
                if (overThreshold) {
                    if (!ptpOffsetAlertCameras_.contains(camId)) {
                        ptpOffsetAlertCameras_.insert(camId);
                        const double ms = static_cast<double>(ptp.offsetFromMasterNs) / 1e6;
                        statusBar()->showMessage(
                            QString("🟡 PTP OFFSET HIGH: Camera %1 is %2 ms from the master clock "
                                    "(limit 1 ms) — check the PTP network/switch.")
                                .arg(camId + 1).arg(ms, 0, 'f', 2),
                            10000);
                    }
                } else {
                    if (ptpOffsetAlertCameras_.remove(camId)) {
                        statusBar()->showMessage(
                            QString("PTP offset for Camera %1 is back to normal.").arg(camId + 1),
                            5000);
                    }
                }
            }, Qt::QueuedConnection);
        }
    );

    // Wire CameraManager into AnalysisView so the Diagnostic tab can poll live data
    if (analysisView_)
        analysisView_->setCameraManager(cameraManager_.get());
}

void MainWindow::handleFrame(int cameraId, const cv::Mat& frame) {
    // Acknowledge receipt immediately so next frame for THIS camera can be queued.
    if (cameraId >= 0 && cameraId < 16) {
        framePending_[cameraId] = false;
    }

    if (frame.empty()) {
        liveDashboard_->clearCameraWidget(cameraId);
        if (detailView_->videoWidget()->cameraId() == cameraId) {
            detailView_->videoWidget()->clearFrame();
        }
        return;
    }

    frameCount_++;
    
    // 1. Add to buffer (only from camera 0 for now)
    if (cameraId == 0) {
        imageBuffer_->addFrame(frame);
    }
    
    // 2. Update GUI
    liveDashboard_->updateFrame(cameraId, frame);
    
    // Always keep DetailView in sync if it's currently focused on this camera
    if (detailView_->videoWidget()->cameraId() == cameraId) {
        detailView_->videoWidget()->updateFrame(frame);
    }
    
    // Also update Analysis View (always, so it has latest frames)
    // USER REQUEST: Remove live view from Analysis View (data record only).
    // QImage qimg(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
    // analysisView_->updateCameraFrame(cameraId, qimg.rgbSwapped().copy());
}

void MainWindow::toggleView() {
    int currentIndex = mainTabWidget_->currentIndex();
    if (currentIndex == 1) {
        mainTabWidget_->setCurrentIndex(0);
    } else {
        mainTabWidget_->setCurrentIndex(1);
    }
}

void MainWindow::showDetail(int cameraId) {
    qDebug() << "Showing detail for camera" << cameraId;

    if (cameraId < 0 || cameraId >= CameraConfig::getCameraCount()) {
        qWarning() << "[MainWindow] Ignoring invalid detail camera selection" << cameraId;
        return;
    }
    
    // MEMORY OPTIMIZATION: Switch tab to ensure live stack logic
    if (mainTabWidget_->currentIndex() == 1) {
        mainTabWidget_->setCurrentIndex(0); 
    }
    
    // Get camera info from centralized config
    CameraInfo info = CameraConfig::getCameraInfo(cameraId);
    
    // Smooth transition: Fetch current live image from the grid so it doesn't flash "waiting"
    QImage currentFrame = liveDashboard_->getCameraImage(cameraId);
    if (!currentFrame.isNull()) {
        detailView_->videoWidget()->setImage(currentFrame);
    } else {
        detailView_->videoWidget()->clearFrame();
    }
    
    // OVERRIDE with actual data from Pylon (via CameraManager)
    if (cameraManager_) {
        info.model = QString::fromStdString(cameraManager_->getModelName(cameraId));
        info.ipAddress = QString::fromStdString(cameraManager_->getIpAddress(cameraId));
        cv::Size res = cameraManager_->getCameraResolution(cameraId);
        info.imageSize = QString("%1 x %2").arg(res.width).arg(res.height);
        double acquisitionFps = cameraManager_->getCameraAcquisitionFps(cameraId);
        if (acquisitionFps > 0) info.fps = acquisitionFps; // only override if readable
        info.temperature = cameraManager_->getTemperature(cameraId);

        // Read actual live camera parameters (Gain, Exposure, Gamma) directly from the camera
        CameraManager::CameraParams liveParams = cameraManager_->getCameraParams(cameraId);

        detailView_->setCamera(cameraId, info, nullptr);
        detailView_->setGainPresentation(liveParams.gainDisplayName, liveParams.gainIsRaw);
        detailView_->setGainRange(liveParams.gainMin, liveParams.gainMax);
        detailView_->setExposureRange(static_cast<int>(liveParams.exposureMinUs), static_cast<int>(liveParams.exposureMaxUs));
        detailView_->setParameterValues(liveParams.gain, liveParams.exposureUs, liveParams.gamma, liveParams.contrast);

        // Seed the AOI overlay with the camera's real geometry so the user can
        // tune the region of interest and see the frame crop live.
        const CameraManager::AOILimits aoiLim = cameraManager_->getCameraAOILimits(cameraId);
        const CameraManager::LiveDeviceSettings liveS = cameraManager_->readLiveDeviceSettings(cameraId, /*allowDirectOpen=*/false);
        detailView_->setAoiInfo(aoiLim.maxWidth, aoiLim.maxHeight,
                                liveS.ok && liveS.width > 0 ? liveS.width : info.width,
                                liveS.ok && liveS.height > 0 ? liveS.height : info.height,
                                liveS.ok ? liveS.offsetX : info.offsetX,
                                liveS.ok ? liveS.offsetY : info.offsetY);

        // Seed the software detection ROI (analysis region) for this camera.
        detailView_->setDetectionRoi(info.detectionRoi, info.roiMaskCurves, info.roiMaskHits);
    } else {
        detailView_->setCamera(cameraId, info, nullptr);
    }
    
    // Show the camera's resulting frame rate on the second info row as requested.
    if (cameraManager_) {
        detailView_->setAcquisitionFps(cameraManager_->getCameraAcquisitionFps(cameraId));
        detailView_->setDisplayFps(cameraManager_->getCameraFps(cameraId));
    } else {
        detailView_->setAcquisitionFps(-1.0);
        detailView_->setDisplayFps(-1.0);
    }
    stackedWidget_->setCurrentWidget(detailView_);
    
    // Use centralized camera label in status bar
    statusBar()->showMessage(QString("Viewing Details: %1").arg(CameraConfig::getCameraLabel(cameraId)));
}

void MainWindow::showGrid() {
    stackedWidget_->setCurrentWidget(liveDashboard_);
    
    // Show grid info in status bar
    statusBar()->showMessage(QString("Grid View %1x%2")
        .arg(liveDashboard_->getCurrentRows())
        .arg(liveDashboard_->getCurrentCols()));
}

void MainWindow::toggleAdmin() {
    if (isAdmin_) {
        // Logout
        if (mainTabWidget_ && configWindow_) {
            const int configIndex = mainTabWidget_->indexOf(configWindow_);
            if (configIndex >= 0 && mainTabWidget_->currentIndex() == configIndex) {
                switchView(ViewMode::Live);
            }
        }

        isAdmin_ = false;
        statusBar()->showMessage("Administrator Logged Out");
        
        if (defectDetectionCheck_) {
            defectDetectionCheck_->setChecked(false);
            defectDetectionCheck_->setEnabled(false);
        }
        if (analysisView_) {
            analysisView_->setAdminMode(false);
        }
    } else {
        promptAdminLogin();
    }
    
    if (detailView_) {
        detailView_->setAdminMode(isAdmin_);
    }

    updateConfigTabAccess();
    updateAdminStatusIndicator();
}

bool MainWindow::promptAdminLogin() {
    if (isAdmin_) {
        return true;
    }

    AdminLoginDialog loginDialog(this);
    while (true) {
        loginDialog.clearInvalidPasswordState();
        if (loginDialog.exec() != QDialog::Accepted) {
            return false;
        }

        if (loginDialog.username() == "admin" && loginDialog.password() == "admin") {
            break;
        }

        loginDialog.showInvalidPasswordState();
    }

    isAdmin_ = true;
    statusBar()->showMessage("Administrator Logged In");

    if (defectDetectionCheck_) {
        defectDetectionCheck_->setEnabled(true);
    }
    if (analysisView_) {
        analysisView_->setAdminMode(true);
    }
    if (detailView_) {
        detailView_->setAdminMode(true);
    }

    updateConfigTabAccess();
    updateAdminStatusIndicator();
    return true;
}

// ---------------------------------------------------------------------------
// styleAdminLoginButton — size + style the tab-bar admin pill (top-right
// corner widget of the main tab widget). Geometry comes from the font in
// code, colors from a direct widget stylesheet (which also overrides the
// global stylesheet and survives theme swaps untouched). Size covers both
// labels so the Login<->Logout swap never needs re-layout.
// ---------------------------------------------------------------------------
void MainWindow::styleAdminLoginButton() {
    if (!adminLoginButton_) {
        return;
    }
    const ThemeColors tc = CameraConfig::getThemeColors();

    QFont f = adminLoginButton_->font();
    f.setPointSizeF(8.0);
    f.setBold(true);
    adminLoginButton_->setFont(f);
    const QFontMetrics fm(f);
    const int textW = qMax(fm.horizontalAdvance(QStringLiteral("Login")),
                           fm.horizontalAdvance(QStringLiteral("Logout")));
    // Icon (10) + gap (4) + text + slack — sized for both labels so the
    // Login<->Logout swap never clips or re-layouts the corner widget.
    constexpr int kIconSz = 10;
    adminLoginButton_->setIconSize(QSize(kIconSz, kIconSz));
    adminLoginButton_->setFixedSize(kIconSz + 4 + textW + 14, 18);

    // Padlock icon (shackle + body) drawn in code like the other programmatic
    // icons of this app; tinted to the state color so it follows the
    // logged-in accent swap.
    const QColor iconColor(isAdmin_ ? QColor(tc.primary) : QColor(tc.text));
    QPixmap pm(kIconSz, kIconSz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen shacklePen(iconColor, 1.4);
    shacklePen.setCapStyle(Qt::RoundCap);
    p.setPen(shacklePen);
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(2.7, 0.8, 4.6, 5.2), 0, 180 * 16);
    p.setPen(Qt::NoPen);
    p.setBrush(iconColor);
    p.drawRoundedRect(QRectF(1.8, 4.6, 6.4, 4.8), 1.2, 1.2);
    p.end();
    adminLoginButton_->setIcon(QIcon(pm));

    adminLoginButton_->setProperty("logged-in", isAdmin_);
    // Minimal color-only stylesheet: autoRaise keeps the native flat look;
    // QSS only tints text and adds a soft hover fill.
    adminLoginButton_->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; color: %1; font-weight: bold; }"
        "QToolButton:hover { background: %2; border-radius: 4px; }"
        "QToolButton:pressed { background: %3; color: %4; }"
        "QToolButton[logged-in=\"true\"] { color: %3; }"
    ).arg(tc.text, tc.btnBg, tc.primary, tc.bg));
}

void MainWindow::updateAdminStatusIndicator() {
    if (adminLoginButton_) {
        adminLoginButton_->setText(isAdmin_ ? "Logout" : "Login");
        adminLoginButton_->setToolTip(isAdmin_ ? "Logout Administrator" : "Admin Login");
        // Restyle (also flips the [logged-in] state); safe to call repeatedly.
        styleAdminLoginButton();
    }
    if (!adminStatusLabel_) {
        return;
    }

    adminStatusLabel_->setText(isAdmin_ ? "Admin: On" : "Admin: Off");
    adminStatusLabel_->setToolTip(isAdmin_ ? "Administrator mode enabled" : "Administrator mode disabled");
    adminStatusLabel_->setProperty("adminActive", isAdmin_);
    adminStatusLabel_->style()->unpolish(adminStatusLabel_);
    adminStatusLabel_->style()->polish(adminStatusLabel_);
}

void MainWindow::openRecordingSettings() {
    // Reuse the full flow: admin login check, config-tab creation, access update,
    // and switching to the tab. Then drill into the Recording & Triggers page.
    openSystemConfiguration();
    if (configWindow_) {
        configWindow_->showRecordingSettingsPage();
    }
}

void MainWindow::refreshDiskBadge() {
    if (!diskBadge_)
        return;

    // Check the event storage volume (same path as the Record Storage stats).
    const QString path = CameraConfig::getEventStoragePath();
    QStorageInfo storage(path);
    if (!storage.isValid() || storage.bytesTotal() <= 0) {
        diskBadge_->setVisible(false);
        return;
    }

    const double freePct = (100.0 * storage.bytesAvailable()) / storage.bytesTotal();
    const double warningPct = CameraConfig::getLowDiskWarningPct();
    const double criticalPct = qMax(1.0, warningPct / 2.0);

    if (freePct >= warningPct) {
        diskBadge_->setVisible(false);
        return;
    }

    const bool critical = freePct < criticalPct;
    diskBadge_->setProperty("critical", critical);
    diskBadge_->setText(QString("\u26A0 Disk: %1% free").arg(QString::number(freePct, 'f', 0)));
    diskBadge_->setToolTip(QStringLiteral(
        "Low disk space on the event storage volume (%1).\n"
        "Free: %2 MB of %3 MB (%4%).\n"
        "Threshold: %5% (warning), %6% (critical). "
        "Consider freeing space or increasing storage capacity.")
        .arg(path,
             QString::number(storage.bytesAvailable() / (1024.0 * 1024.0), 'f', 1),
             QString::number(storage.bytesTotal() / (1024.0 * 1024.0), 'f', 1),
             QString::number(freePct, 'f', 1),
             QString::number(warningPct, 'f', 0),
             QString::number(criticalPct, 'f', 0)));
    diskBadge_->style()->unpolish(diskBadge_);
    diskBadge_->style()->polish(diskBadge_);
    diskBadge_->setVisible(true);
}

void MainWindow::ensureConfigTab() {
    if (!mainTabWidget_) {
        return;
    }

    if (!configWindow_) {
        configWindow_ = new ConfigDialog(cameraManager_.get(), mainTabWidget_);
        connect(configWindow_, &ConfigDialog::configUpdated, [this](bool requiresCameraRestart) {
            qInfo() << "[MainWindow] configUpdated received. requiresCameraRestart=" << requiresCameraRestart;

            // Update camera counts in views to handle newly added cameras
            int newCamCount = CameraConfig::getCameraCount();
            qInfo() << "[MainWindow] Applying updated camera count" << newCamCount;
            if (liveDashboard_) {
                liveDashboard_->setCameraCount(newCamCount);
                liveDashboard_->refreshCameraLabels();
            }
            if (analysisView_) analysisView_->setCameraCount(newCamCount);

            if (detailView_ && detailView_->videoWidget()) {
                const int selectedCameraId = detailView_->videoWidget()->cameraId();
                if (selectedCameraId >= newCamCount) {
                    qInfo() << "[MainWindow] Clearing stale detail selection for removed camera" << selectedCameraId;
                    detailView_->clearCamera();
                    if (stackedWidget_ && stackedWidget_->currentWidget() == detailView_) {
                        showGrid();
                    }
                } else if (selectedCameraId >= 0 && stackedWidget_ && stackedWidget_->currentWidget() == detailView_) {
                    showDetail(selectedCameraId);
                }
            }

            // Redraw UI to remove empty placeholders for newly configured cameras
            if (liveDashboard_) {
                liveDashboard_->setGridDimensions(liveDashboard_->getCurrentRows(), liveDashboard_->getCurrentCols());
            }

            initializeEventController();

            if (requiresCameraRestart) {
                qInfo() << "[MainWindow] Starting async camera restart after save";
                startCameraLifecycleAsync(true, "Restarting cameras with updated configuration...");
            } else {
                qInfo() << "[MainWindow] Save did not require camera restart";
                // NOTE: the fallback fps is intentionally an init-time setting —
                // it is applied when cameras (re)start, not hot-applied on save.
                statusBar()->showMessage("Settings saved", 3000);
            }

            qInfo() << "[MainWindow] Reapplying global theme after save";
            applyOpcUaSettings();
            applyGlobalTheme();

            // Camera Mode (Settings > Recording & Triggers) may have switched:
            // keep the status-bar emulation badge in sync live instead of only
            // reflecting it at app start.
            if (emulationBadge_) {
                emulationBadge_->setVisible(CameraConfig::isEmulationActive());
            }

            if (analysisView_) {
                qInfo() << "[MainWindow] Reloading analysis event storage after save";
                analysisView_->reloadEventStorage();
            }
        });

        connect(configWindow_, &ConfigDialog::cameraDeviceSettingsChanged, this,
                [this](int cameraIndex, const CameraInfo& info) {
                    // The acquisition fps may have changed: resize the ring
                    // buffer so RAM FRAME capacity reflects the new rate.
                    EventController::instance().updateCameraFps(cameraIndex + 1);

                    if (!detailView_ || !detailView_->videoWidget()) {
                        return;
                    }

                    if (detailView_->videoWidget()->cameraId() != cameraIndex) {
                        return;
                    }

                    if (!cameraManager_) {
                        return;
                    }

                    cameraManager_->setCameraExposure(cameraIndex, info.exposureTimeAbs);
                    cameraManager_->setCameraFrameRate(cameraIndex, info.fps, info.enableAcquisitionFps);

                    const CameraManager::CameraParams p = cameraManager_->getCameraParams(cameraIndex);
                    detailView_->setGainPresentation(p.gainDisplayName, p.gainIsRaw);
                    detailView_->setGainRange(p.gainMin, p.gainMax);
                    detailView_->setParameterValues(p.gain, info.exposureTimeAbs, p.gamma, p.contrast);
                    detailView_->setAcquisitionFps(info.fps);
                    detailView_->setDisplayFps(cameraManager_->getCameraFps(cameraIndex));
                });

        // Manual push-hold trigger buttons in the OPC UA config: forward to the
        // service so held buttons fire triggers repeatedly (works without a server
        // for Simulated tags, and as a manual override for Live tags).
        connect(configWindow_, &ConfigDialog::opcUaManualTriggerRequested, this,
                [this](int tagIndex, bool held, const OpcUaTriggerTagSettings& tagSettings) {
            if (opcUaClientService_) {
                opcUaClientService_->setManualTriggerHeld(tagIndex, held, tagSettings);
            }
        });

        connect(configWindow_, &QObject::destroyed, [this]() {
            // The dialog may be destroyed mid-press (e.g. admin logout) without a
            // hideEvent, so make sure no manual trigger stays held forever.
            if (opcUaClientService_) {
                opcUaClientService_->releaseAllManualTriggers();
            }
            configTabIndex_ = -1;
            configWindow_ = nullptr;
        });
    }

    configTabIndex_ = mainTabWidget_->indexOf(configWindow_);
    if (configTabIndex_ < 0) {
        configTabIndex_ = mainTabWidget_->addTab(configWindow_, "System Configuration");
        mainTabWidget_->setTabToolTip(configTabIndex_, "System configuration and camera setup");
    }
}

void MainWindow::updateConfigTabAccess() {
    if (!mainTabWidget_ || !configWindow_) {
        return;
    }

    const int tabIndex = mainTabWidget_->indexOf(configWindow_);
    configTabIndex_ = tabIndex;
    if (tabIndex < 0) {
        return;
    }

    mainTabWidget_->setTabEnabled(tabIndex, isAdmin_);
    mainTabWidget_->tabBar()->setTabTextColor(tabIndex, isAdmin_ ? palette().color(QPalette::WindowText) : QColor("#777777"));
    configWindow_->setAdminMode(isAdmin_);
}

void MainWindow::changeLayout(int rows, int cols) {
    // Get number of cameras from config
    int numCameras = CameraConfig::getCameraCount();
    
    // Validate grid capacity
    if (rows * cols < numCameras) {
        QMessageBox::warning(this, "Insufficient Grid Capacity",
            QString("Cannot use %1x%2 grid for %3 cameras.\n"
                    "Grid capacity (%4) is less than camera count (%5).\n\n"
                    "Please select a larger grid layout.")
                .arg(rows).arg(cols).arg(numCameras)
                .arg(rows * cols).arg(numCameras));
        return;
    }
    
    liveDashboard_->setGridDimensions(rows, cols);
    statusBar()->showMessage(QString("Layout changed to %1x%2").arg(rows).arg(cols));
}

void MainWindow::promptCustomLayout() {
    bool ok;
    QString text = QInputDialog::getText(this, "Custom Grid Layout",
                                         "Enter rows and columns (e.g., '2 3' for 2 rows, 3 columns):", 
                                         QLineEdit::Normal, "2 2", &ok);
    if (ok && !text.isEmpty()) {
        QStringList parts = text.split(" ", QString::SkipEmptyParts);
        if (parts.size() >= 2) {
            bool rowOk, colOk;
            int rows = parts[0].toInt(&rowOk);
            int cols = parts[1].toInt(&colOk);
            
            if (!rowOk || !colOk) {
                QMessageBox::warning(this, "Input Error", 
                    "Please enter valid numbers.\nExample: '2 3' for 2 rows and 3 columns.");
                return;
            }
            
            if (rows < 1 || cols < 1) {
                QMessageBox::warning(this, "Invalid Dimensions", 
                    "Rows and columns must be at least 1.");
                return;
            }
            
            if (rows > 10 || cols > 10) {
                QMessageBox::warning(this, "Dimensions Too Large", 
                    "Maximum allowed is 10x10 grid.\nLarger grids may cause display issues.");
                return;
            }
            
            changeLayout(rows, cols);
        } else {
            QMessageBox::warning(this, "Invalid Format", 
                "Please enter two numbers separated by space.\nExample: '2 3' for 2 rows and 3 columns.");
        }
    }
}

void MainWindow::showAbout() {
    QMessageBox::about(this, "About PaperVision",
                       "PaperVision System v1.0\n\n"
                       "Industrial Vision System for Paper Machine Monitoring.\n"
                       "Built with Qt 5, OpenCV 4, and Basler Pylon 6.");
}

void MainWindow::showDocs() {
    if (!docsDialog_) {
        docsDialog_ = new DocsDialog(this);
    }
    docsDialog_->show();
    docsDialog_->raise();
    docsDialog_->activateWindow();
}

void MainWindow::openSystemConfiguration() {
    if (!promptAdminLogin()) {
        statusBar()->showMessage("Administrator login required for System Configuration.", 3000);
        return;
    }

    ensureConfigTab();
    updateConfigTabAccess();

    if (configTabIndex_ >= 0) {
        mainTabWidget_->setCurrentIndex(configTabIndex_);
    }
}

void MainWindow::switchView(ViewMode mode) {
    switch (mode) {
        case ViewMode::Live:
            mainTabWidget_->setCurrentIndex(0);
            stackedWidget_->setCurrentIndex(0);
            break;
        case ViewMode::Detail:
            mainTabWidget_->setCurrentIndex(0);
            stackedWidget_->setCurrentIndex(1);
            break;
        case ViewMode::Analysis:
            mainTabWidget_->setCurrentIndex(1);
            break;
    }
}

void MainWindow::toggleRecording(bool recording) {
    if (recording) {
        statusBar()->showMessage("RECORDING STARTED for all cameras", 0); // 0 means persistent
    } else {
        statusBar()->showMessage("Recording stopped.", 3000);
    }
}

void MainWindow::manualTrigger() {
    std::cout << "[MainWindow] Manual trigger requested." << std::endl;
    EventController::TriggerContext triggerContext;
    triggerContext.reason = QStringLiteral("Manual Trigger");
    triggerContext.source = QStringLiteral("manual");
    // Capture the live/simulated Machine Speed (OPC UA speed tag) so the event
    // persists speedValue and review-time Align can compute per-camera offsets.
    // currentSpeedMperMin() already rejects stale samples.
    if (opcUaClientService_) {
        opcUaClientService_->refreshSimulatedSpeed();
    }
    double speedMperMin = 0.0;
    if (opcUaClientService_ && opcUaClientService_->currentSpeedMperMin(&speedMperMin) && speedMperMin > 0.0) {
        triggerContext.speedValue = speedMperMin;
        triggerContext.hasSpeed = true;
        const QString unit = opcUaClientService_->speedUnit();
        triggerContext.speedUnit = unit.isEmpty() ? QStringLiteral("m/min") : unit;
    }
    if (!EventController::instance().triggerEvent(triggerContext)) {
        statusBar()->showMessage("Trigger ignored: no active camera is streaming frames.", 4000);
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_S) {
        manualTrigger();
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::togglePauseGrab() {
    if (!cameraManager_) return;
    
    bool isPaused = cameraManager_->isGrabbingPaused();
    cameraManager_->pauseGrabbing(!isPaused);
    
    if (!isPaused) {
        pauseBtn_->setText("Resume");
        pauseBtn_->setToolTip("Resume camera grabbing");
        // Apply a visual warning tint (light red) to signify paused state. 
        // This takes precedence over global stylesheet.
        pauseBtn_->setStyleSheet("background-color: #ffaaaa; color: #880000;");
        statusBar()->showMessage("Camera Grabbing PAUSED", 3000);
    } else {
        pauseBtn_->setText("Pause");
        pauseBtn_->setToolTip("Pause camera grabbing");
        // Clear local style to revert to global theme
        pauseBtn_->setStyleSheet("");
        statusBar()->showMessage("Camera Grabbing RESUMED", 3000);
    }
}

void MainWindow::applyGlobalTheme() {
    ThemeColors tc = CameraConfig::getThemeColors();
    const QString& bgColor    = tc.bg;
    const QString& borderColor = tc.border;
    const QString& btnBg      = tc.btnBg;
    const QString& btnHover   = tc.btnHover;
    const QString& primaryColor = tc.primary;
    const QString& sliderBg   = tc.sliderBg;
    const QString& handleColor = tc.handle;
    const QString& textColor  = tc.text;

    QString globalStyle = QString(
        // Base Window & Widget backgrounds
        "QMainWindow, QDialog { background-color: %1; color: %8; }"
        "QMainWindow QWidget, QDialog QWidget { background-color: %1; color: %8; }"
        "QLabel:disabled, QCheckBox:disabled, QRadioButton:disabled, QGroupBox:disabled, QGroupBox::title:disabled { color: #888888; }"
        "QLabel#adminStatusLabel { padding: 0 8px; color: %8; font-weight: bold; }"
        "QLabel#adminStatusLabel[adminActive='true'] { color: %5; }"
        // Emulation-mode badge (cyan accent matches the emulated status color)
        "QLabel#emulationBadge { background-color: rgba(0, 229, 255, 0.16); color: #00E5FF; "
        "  border: 1px solid #00E5FF; border-radius: 10px; padding: 3px 12px; "
        "  font-weight: bold; font-size: 12px; }"
        // Low-disk badge (clickable): amber below threshold, red at critical (< half threshold)
        "QPushButton#diskBadge { background-color: rgba(255, 176, 32, 0.16); color: #FFB020; "
        "  border: 1px solid #FFB020; border-radius: 10px; padding: 3px 12px; "
        "  font-weight: bold; font-size: 12px; }"
        "QPushButton#diskBadge:hover { background-color: rgba(255, 176, 32, 0.32); }"
        "QPushButton#diskBadge:focus { outline: none; }"
        "QPushButton#diskBadge[critical='true'] { background-color: rgba(255, 90, 90, 0.18); "
        "  color: #FF5A5A; border: 1px solid #FF5A5A; }"
        "QPushButton#diskBadge[critical='true']:hover { background-color: rgba(255, 90, 90, 0.34); }"
        // Toolbars and Borders
        "QToolBar, QMenuBar { background-color: %3; border-bottom: 1px solid %2; color: %8; }"
        "QMenu { background-color: %3; border: 1px solid %2; color: %8; }"
        "QMenu::item:selected { background-color: %4; color: %5; }"
        "QMenu::item:disabled { color: #888888; }"
        // Tooltips — one standard, readable look app-wide (light background,
        // dark text, thin border, normal weight) instead of inheriting the
        // dark app theme.
        "QToolTip { background-color: #FFF8E7; color: #111111; border: 1px solid #888888; "
        "  padding: 3px 6px; font-size: 12px; font-weight: normal; }"
        // Buttons
        "QPushButton { background-color: %3; color: %8; border: 1px solid %2; border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
        "QPushButton:hover { background-color: %4; border-color: %5; }"
        "QPushButton:pressed { background-color: %5; color: %1; }"
        "QPushButton:disabled { background-color: %1; color: #888888; border: 1px solid %2; }"
        // Checkboxes — always render a clearly visible box (dark theme), with
        // a filled primary-colored state when checked.
        "QCheckBox { color: %8; font-weight: bold; spacing: 6px; }"
        "QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid %2; border-radius: 3px; background-color: %1; }"
        "QCheckBox::indicator:hover { border-color: %5; background-color: %4; }"
        "QCheckBox::indicator:pressed { background-color: %4; }"
        "QCheckBox::indicator:checked { background-color: %5; border-color: %5; }"
        "QCheckBox::indicator:checked:hover { background-color: %4; border-color: %5; }"
        "QCheckBox::indicator:disabled { background-color: %1; border-color: %2; }"
        // Tables / Grids
        "QTableWidget, QTableView { background-color: %1; alternate-background-color: %3; color: %8; gridline-color: %2; border: 1px solid %2; }"
        "QHeaderView::section { background-color: %3; color: %8; padding: 4px; border: 1px solid %2; }"
        "QTableWidget::item:selected { background-color: %4; color: %5; }"
        // Sliders (For Analysis View)
        "QSlider::groove:horizontal { height: 4px; background: %2; border-radius: 2px; }"
        "QSlider::groove:horizontal:disabled { background: %1; border: 1px solid %2; }"
        "QSlider::handle:horizontal { width: 12px; height: 12px; margin: -4px 0; background: %7; border-radius: 6px; }"
        "QSlider::handle:horizontal:hover { transform: scale(1.2); background: %5; }"
        "QSlider::handle:horizontal:disabled { background: %2; }"
        "QSlider::sub-page:horizontal { background: %6; border-radius: 2px; }"
        "QSlider::sub-page:horizontal:disabled { background: #888888; border-radius: 2px; }"
        "QSlider::add-page:horizontal { background: %2; border-radius: 2px; }"
        // Inputs
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background-color: %3; color: %8; border: 1px solid %2; border-radius: 2px; padding: 2px 4px; }"
        "QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { background-color: %1; color: #888888; border: 1px solid %2; }"
        "QComboBox::drop-down { border-left: 1px solid %2; }"
        // SpinBox buttons — must be styled explicitly when background-color is set
        "QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 16px; border-left: 1px solid %2; background-color: %3; }"
        "QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 16px; border-left: 1px solid %2; border-top: 1px solid %2; background-color: %3; }"
        "QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover { background-color: %4; }"
        "QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background-color: %4; }"
        "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(:/assets/icons/arrow_up.svg); width: 8px; height: 8px; }"
        "QSpinBox::up-arrow:disabled, QDoubleSpinBox::up-arrow:disabled { image: url(:/assets/icons/arrow_up_disabled.svg); }"
        "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(:/assets/icons/arrow_down.svg); width: 8px; height: 8px; }"
        "QSpinBox::down-arrow:disabled, QDoubleSpinBox::down-arrow:disabled { image: url(:/assets/icons/arrow_down_disabled.svg); }"
        // Specific Overrides for Playback Panel to match theme instead of staying transparent
        "QWidget#playbackPanel QPushButton { background-color: %3; border: 1px solid %2; padding: 4px; border-radius: 4px; }"
        "QWidget#playbackPanel QPushButton:hover { background-color: %4; border-color: %5; }"
        "QWidget#playbackPanel QPushButton[active=\"true\"] { background-color: %5; color: %1; }"
        "QWidget#playbackPanel { border-top: 1px solid %2; }"
    ).arg(bgColor, borderColor, btnBg, btnHover, primaryColor, sliderBg, handleColor, textColor);

    qApp->setStyleSheet(globalStyle);

    // Tooltips get their look from two mechanisms (the stylesheet rule above
    // AND the palette Qt falls back to). Keep BOTH explicitly light so every
    // tooltip renders identically regardless of the system theme.
    QPalette tipPalette;
    tipPalette.setColor(QPalette::ToolTipBase, QColor("#FFF8E7"));
    tipPalette.setColor(QPalette::ToolTipText, QColor("#111111"));
    QToolTip::setPalette(tipPalette);
    QToolTip::setFont(qApp->font());
    
    // Sub-components that manage their own local stylesheets using theme variables
    if (liveDashboard_) liveDashboard_->updateTheme();
    if (detailView_) detailView_->updateTheme();
    if (analysisView_) analysisView_->updateTheme();
}
