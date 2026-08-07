# Device Settings Dialog Polish — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the per-camera `CameraDeviceSettingsDialog` as a two-pane dialog (live status sidebar + grouped detail pages) with hybrid live/staged apply semantics and a consistent dark-theme visual system.

**Architecture:** One dialog file pair (`CameraDeviceSettingsDialog.{h,cpp}`) is restructured: sidebar `QListWidget` nav + `QStackedWidget` detail pages replace the `QTabWidget`; a staged-change model (`stagedFields_` set) routes live parameters to immediate camera writes and stop-required parameters to an explicit "Apply Staged" flow. Camera write paths (`CameraManager::applyCameraDeviceSettings`, `setCameraExposure`, `setCameraFrameRate`) are reused unchanged.

**Tech Stack:** C++17, Qt5 Widgets, Pylon 6.2 (via `CameraManager`), CMake. Build ONLY in Docker (`bash ./docker-rebuild-app.sh` — never rebuild the `web-vision-pro:1.0` image). Manual verification at DISPLAY=:0 (no test framework in repo).

## Global Constraints

- Working directory: `.worktrees/feature/polish-device-setting` (branch `feature/polish-device-setting` @ `f5da9c1`). All git commands run with `-C .worktrees/feature/polish-device-setting`.
- Commit format: `type: description` (feat/fix/refactor/docs/style).
- Existing camera write APIs must NOT change: `cameraManager_->applyCameraDeviceSettings(index, info)`, `setCameraExposure(index, us)`, `setCameraFrameRate(index, fps, enabled)`.
- Public dialog API preserved: `CameraInfo updatedInfo() const`, `bool requiresRestart() const`, signal `settingsApplied(const CameraInfo&)` — `ConfigDialog.cpp` is NOT modified.
- Admin gating preserved: non-admin (`editable_ == false`) gets fully read-only dialog.
- Theme tokens (keep exact): surface `#24292E`, inset `#1C2128`, border `#30363D`, text `#E3E3E3`, muted `#8B949E`, accent `#00E5FF`, success `#2EA043`, warn `#E0A800`, danger `#FF5A5A`.
- Build/verify per task: compile the WORKTREE source with a one-off container (the compose-based `docker-rebuild-app.sh` mounts the MAIN checkout, so it would build the wrong code):

```bash
docker rm -f paper_vision_wt_build 2>/dev/null; docker run --rm --name paper_vision_wt_build \
  -v /home/autoinst578/web_vision_pro/.worktrees/feature/polish-device-setting:/app \
  -w /app web-vision-pro:1.0 \
  bash -lc "mkdir -p /app/build && cd /app/build && cmake .. && cmake --build . --target PaperVision_App -- -j\$(nproc)"
```

Expected: `[100%] Built target PaperVision_App` and no `error:` in the output. The running `paper_vision_node` app (main checkout) is left untouched during per-task verification; visual verification of the worktree build happens at Task 6 via the compose mount swap.

---

### Task 1: Two-pane skeleton (sidebar + stacked detail pages)

**Files:**
- Modify: `src/gui/widgets/CameraDeviceSettingsDialog.h`
- Modify: `src/gui/widgets/CameraDeviceSettingsDialog.cpp` (setupUi rewrite)

**Interfaces:**
- Consumes: existing members from current header (`pixelFormatCombo_`, `widthSpin_`, `heightSpin_`, `offsetXSpin_`, `offsetYSpin_`, `sensorWidthValueLabel_`, `sensorHeightValueLabel_`, `maxWidthValueLabel_`, `maxHeightValueLabel_`, `exposureTimeAbsSpin_`, `enableExposureTimeBaseCheck_`, `exposureTimeBaseSpin_`, `exposureTimeRawSpin_`, `enableAcquisitionRateCheck_`, `acquisitionRateSpin_`, `resultingRateValueLabel_`, `chunkModeActiveCheck_`, `chunkListWidget_`, `vendorValueLabel_`, `modelInfoValueLabel_`, `manufacturerInfoValueLabel_`, `deviceVersionValueLabel_`, `firmwareVersionValueLabel_`, `deviceIdValueLabel_`, `modelValueLabel_`, `ipValueLabel_`).
- Produces (later tasks use): members `navList_` (`QListWidget*`), `detailStack_` (`QStackedWidget*`), `statusTitleLabel_`, `statusModelLabel_`, `statusIpLabel_`, `statusTempLabel_`, `runStateBtn_` (`QPushButton*`), `statusChipLabel_`, `applyStagedBtn_`, `navItems_` (`QHash<int,QListWidgetItem*>` group id → item), `stagedCallouts_` (`QHash<int,QFrame*>` group id → callout frame), `QTimer* refreshTimer_`, `QSet<QString> stagedFields_`.

- [ ] **Step 1: Restructure the header**

In `CameraDeviceSettingsDialog.h`: keep all existing form-field members (listed in Interfaces above); **remove** `applyCloseBtn_`, `resetDeviceBtn_`, `impactLabel_`, `statusLabel_`, `subtitleLabel_`, `cameraRunStateBtn_`. Add `#include <QHash>` and `#include <QSet>`; forward-declare `class QTimer;`. Replace the removed members with:

```cpp
    // Layout
    QListWidget* navList_;
    QStackedWidget* detailStack_;
    QHash<int, QListWidgetItem*> navItems_;    // groupId -> nav item
    QHash<int, QFrame*> stagedCallouts_;       // groupId -> callout frame
    QFrame* statusCard_;
    QLabel* statusChipLabel_;
    QLabel* statusTitleLabel_;
    QLabel* statusModelLabel_;
    QLabel* statusIpLabel_;
    QLabel* statusTempLabel_;
    QPushButton* runStateBtn_;
    QPushButton* applyBtn_;
    QPushButton* applyStagedBtn_;
    QPushButton* cancelBtn_;
    QTimer* refreshTimer_;

    // Staged-change model
    QSet<QString> stagedFields_;
    int stagedCount() const;
    QSet<QString> stagedFieldsInGroup(int groupId) const;
```

Add to private slots: `void onNavChanged(int row);` and `void onCancelClicked();` and `void applyStagedChanges();`. Add private methods: `QWidget* buildSidebar();`, `void buildDetailPages();`, `void updateSidebarStatus();`, `void updateStagedBadges();`, `void updateStagedCallouts();`, `void updateApplyStagedEnabled();`, `void stageField(const QString& field);`, `void clearStaged();`.

- [ ] **Step 2: Rewrite `setupUi()` — root, title bar, body, footer**

Replace the body of `setupUi()` with:

```cpp
void CameraDeviceSettingsDialog::setupUi() {
    setWindowTitle(QString("Camera %1 - Device Settings").arg(originalInfo_.id));
    setModal(true);
    resize(980, 680);

    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setSpacing(12);
    rootLayout->setContentsMargins(14, 14, 14, 14);

    // Title bar: camera icon + title + live status chip
    QHBoxLayout* titleLayout = new QHBoxLayout();
    titleLayout->setSpacing(10);
    QLabel* titleIcon = new QLabel(this);
    titleIcon->setPixmap(IconManager::instance().camera(20).pixmap(20, 20));
    titleLayout->addWidget(titleIcon);
    QLabel* titleLabel = new QLabel(QString("Camera %1 - Device Settings").arg(originalInfo_.id), this);
    titleLabel->setStyleSheet("font-size: 17px; font-weight: 600; color: #E3E3E3;");
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    statusChipLabel_ = new QLabel(this);
    statusChipLabel_->setStyleSheet(
        "QLabel { color: #8B949E; font-size: 12px; font-weight: 600; "
        "padding: 3px 10px; border: 1px solid #30363D; border-radius: 10px; }");
    titleLayout->addWidget(statusChipLabel_);
    rootLayout->addLayout(titleLayout);

    // Two-pane body
    QHBoxLayout* bodyLayout = new QHBoxLayout();
    bodyLayout->setSpacing(12);
    bodyLayout->addWidget(buildSidebar());
    detailStack_ = new QStackedWidget(this);
    bodyLayout->addWidget(detailStack_, 1);
    rootLayout->addLayout(bodyLayout, 1);

    // Footer
    QHBoxLayout* footerLayout = new QHBoxLayout();
    footerLayout->setSpacing(10);
    footerLayout->addStretch();
    cancelBtn_ = new QPushButton("Cancel", this);
    cancelBtn_->setIcon(IconManager::instance().close(16));
    applyStagedBtn_ = new QPushButton("Apply Staged", this);
    applyStagedBtn_->setIcon(IconManager::instance().save(16));
    applyStagedBtn_->setEnabled(false);
    applyBtn_ = new QPushButton("Close", this);
    applyBtn_->setIcon(IconManager::instance().check(16));
    footerLayout->addWidget(cancelBtn_);
    footerLayout->addWidget(applyStagedBtn_);
    footerLayout->addWidget(applyBtn_);
    rootLayout->addLayout(footerLayout);

    const QString buttonBase =
        "QPushButton { border: 1px solid #30363D; border-radius: 6px; padding: 7px 14px; font-size: 12px; font-weight: 600; } ";
    cancelBtn_->setStyleSheet(buttonBase +
        "QPushButton { background: transparent; color: #E3E3E3; } QPushButton:hover { border-color: #8B949E; }");
    applyStagedBtn_->setStyleSheet(buttonBase +
        "QPushButton { background: #1C2128; color: #E0A800; } QPushButton:hover { border-color: #E0A800; }"
        "QPushButton:disabled { color: #6E7681; border-color: #30363D; }");
    applyBtn_->setStyleSheet(buttonBase +
        "QPushButton { background: #238636; color: white; } QPushButton:hover { background: #2EA043; }");

    buildDetailPages();
    navList_->setCurrentRow(0);

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(2000);
    refreshTimer_->start();

    const auto registerChangeSignal = [this](QObject* obj, const char* signal) {
        connect(obj, signal, this, SLOT(onValueChanged()));
    };
    registerChangeSignal(pixelFormatCombo_, SIGNAL(currentIndexChanged(int)));
    registerChangeSignal(widthSpin_, SIGNAL(valueChanged(int)));
    registerChangeSignal(heightSpin_, SIGNAL(valueChanged(int)));
    registerChangeSignal(offsetXSpin_, SIGNAL(valueChanged(int)));
    registerChangeSignal(offsetYSpin_, SIGNAL(valueChanged(int)));
    registerChangeSignal(exposureTimeAbsSpin_, SIGNAL(valueChanged(double)));
    registerChangeSignal(enableExposureTimeBaseCheck_, SIGNAL(toggled(bool)));
    registerChangeSignal(exposureTimeBaseSpin_, SIGNAL(valueChanged(double)));
    registerChangeSignal(exposureTimeRawSpin_, SIGNAL(valueChanged(int)));
    registerChangeSignal(enableAcquisitionRateCheck_, SIGNAL(toggled(bool)));
    registerChangeSignal(acquisitionRateSpin_, SIGNAL(valueChanged(double)));
    registerChangeSignal(chunkModeActiveCheck_, SIGNAL(toggled(bool)));
    connect(chunkListWidget_, &QListWidget::itemChanged, this, &CameraDeviceSettingsDialog::onValueChanged);
    connect(navList_, &QListWidget::currentRowChanged, this, &CameraDeviceSettingsDialog::onNavChanged);
    connect(cancelBtn_, &QPushButton::clicked, this, &CameraDeviceSettingsDialog::onCancelClicked);
    connect(applyBtn_, &QPushButton::clicked, this, &CameraDeviceSettingsDialog::closeDialog);
    connect(applyStagedBtn_, &QPushButton::clicked, this, &CameraDeviceSettingsDialog::applyStagedChanges);
    connect(runStateBtn_, &QPushButton::clicked, this, &CameraDeviceSettingsDialog::toggleCameraRunState);
    connect(refreshTimer_, &QTimer::timeout, this, &CameraDeviceSettingsDialog::refreshLiveDeviceInfo);
}
```

- [ ] **Step 3: Add `buildSidebar()`**

```cpp
QWidget* CameraDeviceSettingsDialog::buildSidebar() {
    QWidget* sidebar = new QWidget(this);
    sidebar->setFixedWidth(230);
    QVBoxLayout* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(10);

    statusCard_ = new QFrame(sidebar);
    statusCard_->setStyleSheet("QFrame { background-color: #1C2128; border: 1px solid #30363D; border-radius: 8px; }");
    QVBoxLayout* cardLayout = new QVBoxLayout(statusCard_);
    cardLayout->setContentsMargins(12, 10, 12, 10);
    cardLayout->setSpacing(4);
    statusTitleLabel_ = new QLabel(statusCard_);
    statusTitleLabel_->setStyleSheet("font-size: 13px; font-weight: 600; color: #E3E3E3;");
    statusModelLabel_ = new QLabel(statusCard_);
    statusModelLabel_->setStyleSheet("font-size: 11px; color: #8B949E;");
    statusIpLabel_ = new QLabel(statusCard_);
    statusIpLabel_->setStyleSheet("font-size: 11px; color: #8B949E; font-family: 'SF Mono', Monaco, monospace;");
    statusTempLabel_ = new QLabel(statusCard_);
    statusTempLabel_->setStyleSheet("font-size: 11px; color: #8B949E;");
    runStateBtn_ = new QPushButton(statusCard_);
    runStateBtn_->setStyleSheet(
        "QPushButton { border-radius: 6px; padding: 6px 10px; font-size: 12px; font-weight: 600; margin-top: 6px; }"
        "QPushButton:disabled { color: #6E7681; border: 1px solid #30363D; background: transparent; }");
    cardLayout->addWidget(statusTitleLabel_);
    cardLayout->addWidget(statusModelLabel_);
    cardLayout->addWidget(statusIpLabel_);
    cardLayout->addWidget(statusTempLabel_);
    cardLayout->addWidget(runStateBtn_);
    sideLayout->addWidget(statusCard_);

    navList_ = new QListWidget(sidebar);
    navList_->setStyleSheet(
        "QListWidget { background: transparent; border: none; outline: 0; }"
        "QListWidget::item { padding: 8px 10px; border-radius: 6px; color: #8B949E; font-size: 12px; font-weight: 500; }"
        "QListWidget::item:hover { background-color: rgba(0, 229, 255, 0.06); color: #E3E3E3; }"
        "QListWidget::item:selected { background-color: rgba(0, 229, 255, 0.12); color: #E3E3E3; border-left: 2px solid #00E5FF; }");
    navList_->setFocusPolicy(Qt::StrongFocus);
    sideLayout->addWidget(navList_, 1);
    return sidebar;
}
```

- [ ] **Step 4: Add `buildDetailPages()` — all six pages**

Add these file-local helpers (full code):

```cpp
QString groupBoxStyle() {
    return "QGroupBox { font-weight: 600; color: #00E5FF; border: 1px solid #30363D; border-radius: 8px; "
           "margin-top: 6px; padding-top: 8px; font-size: 12px; }"
           "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }";
}
QString comboStyle() {
    return "QComboBox { background-color: #1C2128; border: 1px solid #30363D; border-radius: 6px; "
           "padding: 6px 8px; color: #E3E3E3; font-size: 12px; }"
           "QComboBox:focus { border-color: #00E5FF; }";
}
QString spinStyle() {
    return "QSpinBox { background-color: #1C2128; border: 1px solid #30363D; border-radius: 6px; "
           "padding: 6px 8px; color: #E3E3E3; font-size: 12px; }"
           "QSpinBox:focus { border-color: #00E5FF; }";
}
QString doubleSpinStyle() {
    return "QDoubleSpinBox { background-color: #1C2128; border: 1px solid #30363D; border-radius: 6px; "
           "padding: 6px 8px; color: #E3E3E3; font-size: 12px; }"
           "QDoubleSpinBox:focus { border-color: #00E5FF; }";
}
QFrame* buildCallout(QWidget* parent) {
    QFrame* callout = new QFrame(parent);
    callout->setStyleSheet(
        "QFrame { background-color: rgba(224, 168, 0, 0.08); border: 1px solid #E0A800; border-radius: 6px; }");
    QHBoxLayout* lay = new QHBoxLayout(callout);
    lay->setContentsMargins(10, 6, 10, 6);
    lay->setSpacing(8);
    QLabel* icon = new QLabel(callout);
    icon->setPixmap(IconManager::instance().warning(16).pixmap(16, 16));
    QLabel* text = new QLabel("Changes staged - stop the camera to apply.", callout);
    text->setStyleSheet("color: #E0A800; font-size: 11px; font-weight: 500; background: transparent;");
    QPushButton* stopApply = new QPushButton("Stop & Apply", callout);
    stopApply->setStyleSheet(
        "QPushButton { background: #1C2128; color: #E0A800; border: 1px solid #E0A800; border-radius: 4px; "
        "padding: 3px 8px; font-size: 10px; font-weight: 600; }"
        "QPushButton:hover { background: rgba(224, 168, 0, 0.15); }");
    lay->addWidget(icon);
    lay->addWidget(text, 1);
    lay->addWidget(stopApply);
    callout->setVisible(false);
    return callout;
}
```

The callout's "Stop & Apply" button is created here but wired in Task 4 (its slot `applyStagedChanges()` does not exist yet).

`buildDetailPages()` constructs pages in this order (group ids fixed):

| id | Page title | Hint | Widgets (re-parent into this page) |
|----|-----------|------|-------------------------------------|
| 0 | Image Format | "Pixel format of the acquired image." | `pixelFormatCombo_` in `imageFormatGroup` |
| 1 | AOI | "Region of interest; requires camera stop to apply." | `widthSpin_`, `heightSpin_`, `offsetXSpin_`, `offsetYSpin_` + 4 sensor/max labels in `roiGroup` |
| 2 | Exposure & Rate | "Exposure applies live; base/raw exposure requires camera stop." | `exposureTimeAbsSpin_`, `enableExposureTimeBaseCheck_`, `exposureTimeBaseSpin_`, `exposureTimeRawSpin_`, `enableAcquisitionRateCheck_`, `acquisitionRateSpin_`, `resultingRateValueLabel_` in `exposureGroup` |
| 3 | Chunk Data | "Choose chunk items included in the payload; requires camera stop." | `chunkModeActiveCheck_`, `chunkListWidget_` in `chunkGroup` |
| 4 | Device Information | "Read-only device identity and live connection details." | 8 read-only rows via `createInfoValueLabel()` + `addFormRow` (exact rows as today) |
| 5 | Service | "Camera maintenance actions." | muted note QLabel "Reset Device is not available in this build." only |

Page template (apply to every group; group 0 shown in full — all other pages follow the identical pattern with their own title/hint/widgets):

```cpp
    // --- Page: Image Format (group 0) ---
    QWidget* imagePage = new QWidget(this);
    QVBoxLayout* imagePageLayout = new QVBoxLayout(imagePage);
    imagePageLayout->setContentsMargins(4, 0, 4, 0);
    imagePageLayout->setSpacing(10);
    QLabel* imageTitle = new QLabel("Image Format", imagePage);
    imageTitle->setStyleSheet("font-size: 14px; font-weight: 600; color: #E3E3E3;");
    imagePageLayout->addWidget(imageTitle);
    QLabel* imageHint = new QLabel("Pixel format of the acquired image.", imagePage);
    imageHint->setStyleSheet("font-size: 11px; color: #8B949E;");
    imagePageLayout->addWidget(imageHint);
    stagedCallouts_.insert(0, buildCallout(imagePage));
    imagePageLayout->addWidget(stagedCallouts_.value(0));

    QGroupBox* imageFormatGroup = new QGroupBox("Image Format Controls", imagePage);
    imageFormatGroup->setStyleSheet(groupBoxStyle());
    QFormLayout* imageFormatLayout = new QFormLayout(imageFormatGroup);
    imageFormatLayout->setContentsMargins(14, 16, 14, 14);
    imageFormatLayout->setHorizontalSpacing(14);
    imageFormatLayout->setVerticalSpacing(10);
    pixelFormatCombo_ = new QComboBox(imageFormatGroup);
    pixelFormatCombo_->setEditable(false);
    pixelFormatCombo_->setStyleSheet(comboStyle());
    addFormRow(imageFormatLayout, "Pixel Format:", pixelFormatCombo_);
    imagePageLayout->addWidget(imageFormatGroup);
    imagePageLayout->addStretch();
    detailStack_->addWidget(imagePage);
```

Keep the existing widget construction bodies (ranges, suffixes, check-box labels, chunk item flags) identical to the current file — only the parent argument, stylesheet source, and enclosing layout change. For group 4, `createInfoValueLabel()` + `addFormRow` rows are copied unchanged from the current Device Info tab. For group 5, only the muted note; no buttons.

After all pages, populate the nav:

```cpp
    struct NavEntry { int id; const char* icon; const char* label; };
    const NavEntry entries[] = {
        {0, Icons::EDIT, "Image Format"},
        {1, Icons::SETTINGS, "AOI"},
        {2, Icons::SPEED, "Exposure & Rate"},
        {3, Icons::INFO, "Chunk Data"},
        {4, Icons::CAMERA, "Device Info"},
        {5, Icons::SETTINGS, "Service"},
    };
    for (const auto& e : entries) {
        QListWidgetItem* item = new QListWidgetItem(
            IconManager::instance().getIcon(e.icon, 16), QString::fromLatin1(e.label), navList_);
        navItems_.insert(e.id, item);
    }
```

- [ ] **Step 5: Implement `onNavChanged`, `stagedCount`, `stagedFieldsInGroup`**

```cpp
void CameraDeviceSettingsDialog::onNavChanged(int row) {
    if (row >= 0 && row < detailStack_->count()) {
        detailStack_->setCurrentIndex(row);
    }
}

int CameraDeviceSettingsDialog::stagedCount() const {
    return stagedFields_.size();
}

QSet<QString> CameraDeviceSettingsDialog::stagedFieldsInGroup(int groupId) const {
    QSet<QString> fields;
    switch (groupId) {
    case 0: if (stagedFields_.contains("pixelFormat")) fields.insert("pixelFormat"); break;
    case 1: for (const char* f : {"width", "height", "offsetX", "offsetY"}) if (stagedFields_.contains(f)) fields.insert(f); break;
    case 2: for (const char* f : {"exposureTimeBase", "exposureTimeRaw"}) if (stagedFields_.contains(f)) fields.insert(f); break;
    case 3: for (const char* f : {"chunkMode", "chunks"}) if (stagedFields_.contains(f)) fields.insert(f); break;
    default: break;
    }
    return fields;
}
```

- [ ] **Step 6: Trim old setupUi leftovers**

Remove from the .cpp: the old `impactFrame`/`impactLabel_`/`statusLabel_` block, the old `tabs` QTabWidget, the old footer with `applyCloseBtn_`, the Service-tab `resetDeviceBtn_`/`cameraRunStateBtn_`, and the old duplicate inline style strings (replaced by the shared helpers). Then make these four consistency fixes so the file still compiles with the trimmed header:

1. **Delete `updateImpactBanner()`** — remove its definition in the .cpp and its declaration in the header (members `impactLabel_`/`statusLabel_` are gone). Remove its call sites: in the constructor, in `onValueChanged()`, and in `toggleCameraRunState()`.
2. **`refreshLiveDeviceInfo()`** — delete the final `statusLabel_->setText(...)` cascade (the `isCameraReachable`/`configuredReal`/connected branches). Keep only the read-only label updates and the resulting-rate line. The status chip and sidebar labels are populated from Task 2's `updateSidebarStatus()`.
3. **`updateControlAvailability()`** — delete the `if (cameraRunStateBtn_) { ... }` block (the member is gone; `runStateBtn_` is driven by Task 2's `updateSidebarStatus()`) and the `applyCloseBtn_->setVisible(false);` line (member removed). Keep the `applyBtn_->setEnabled(true);` and `cancelBtn_->setText("Cancel");` lines (Task 3 rewrites this function).
4. **`populateUi()`** — replace the `subtitleLabel_->setText(...)` line with `statusTitleLabel_->setText(QString("%1 | %2 | %3 mm").arg(originalInfo_.name).arg(originalInfo_.location).arg(originalInfo_.machinePosition));`.

The constructor keeps its existing `populateUi(); refreshLiveDeviceInfo();` sequence — no `updateImpactBanner()` call, no `updateSidebarStatus()` call yet.

- [ ] **Step 7: Build and verify**

Run: `docker rm -f paper_vision_wt_build 2>/dev/null; docker run --rm --name paper_vision_wt_build -v /home/autoinst578/web_vision_pro/.worktrees/feature/polish-device-setting:/app -w /app web-vision-pro:1.0 bash -lc "mkdir -p /app/build && cd /app/build && cmake .. && cmake --build . --target PaperVision_App -- -j\$(nproc)"`
Expected: output ends with `[100%] Built target PaperVision_App`, no `error:` lines.

- [ ] **Step 8: Commit**

```bash
git -C .worktrees/feature/polish-device-setting add src/gui/widgets/CameraDeviceSettingsDialog.h src/gui/widgets/CameraDeviceSettingsDialog.cpp
git -C .worktrees/feature/polish-device-setting commit -m "refactor: two-pane skeleton for device settings dialog"
```

---

### Task 2: Live status card + status chip + refresh timer

**Files:**
- Modify: `src/gui/widgets/CameraDeviceSettingsDialog.cpp`

**Interfaces:**
- Consumes: `statusTitleLabel_`, `statusModelLabel_`, `statusIpLabel_`, `statusTempLabel_`, `runStateBtn_`, `statusChipLabel_`, `refreshTimer_` (Task 1), `isCameraReachable()`, `cameraManager_`.
- Produces: `updateSidebarStatus()` (used by Tasks 3–5); run-state button contract: text `"Stop Camera"` (running) / `"Start Camera"` (stopped) / `"Unavailable"` (unreachable); running state colored `#2EA043`, stopped `#E0A800`, offline `#FF5A5A`.

- [ ] **Step 1: Rewrite `refreshLiveDeviceInfo()` status section**

Keep all existing read-only label updates (vendor/model/device/sensor/max/resulting-rate). Replace the final `statusLabel_->setText(...)` cascade and the `updateImpactBanner()` call in `onValueChanged` (Task 3) with:

```cpp
void CameraDeviceSettingsDialog::updateSidebarStatus() {
    const bool reachable = isCameraReachable(cameraManager_, cameraIndex_, currentInfo_);
    const bool connected = cameraManager_ && (cameraManager_->isCameraConnected(cameraIndex_) || cameraManager_->isCameraOpen(cameraIndex_));
    const bool running = connected && cameraManager_->isCameraRunning(cameraIndex_);

    QString chipText;
    QString chipColor;
    if (!reachable) {
        chipText = "Offline";
        chipColor = "#FF5A5A";
    } else if (running) {
        chipText = "Running";
        chipColor = "#2EA043";
    } else if (connected) {
        chipText = "Stopped";
        chipColor = "#E0A800";
    } else {
        chipText = "Online";
        chipColor = "#E0A800";
    }
    statusChipLabel_->setText(chipText);
    statusChipLabel_->setStyleSheet(
        QString("QLabel { color: %1; font-size: 12px; font-weight: 600; padding: 3px 10px; border: 1px solid %1; border-radius: 10px; }")
            .arg(chipColor));

    statusModelLabel_->setText(formatReadOnlyValue(QString::fromStdString(cameraManager_ ? cameraManager_->getModelName(cameraIndex_) : std::string())));
    statusIpLabel_->setText(formatReadOnlyValue(QString::fromStdString(cameraManager_ ? cameraManager_->getIpAddress(cameraIndex_) : std::string())));
    statusTempLabel_->setText(QString("Temperature: %1 C").arg(currentInfo_.temperature, 0, 'f', 1));

    if (!reachable) {
        runStateBtn_->setText("Unavailable");
        runStateBtn_->setEnabled(false);
        runStateBtn_->setStyleSheet(
            "QPushButton { border-radius: 6px; padding: 6px 10px; font-size: 12px; font-weight: 600; margin-top: 6px; "
            "color: #6E7681; border: 1px solid #30363D; background: transparent; }");
        return;
    }
    runStateBtn_->setEnabled(editable_);
    runStateBtn_->setText(running ? "Stop Camera" : "Start Camera");
    runStateBtn_->setStyleSheet(
        QString("QPushButton { border-radius: 6px; padding: 6px 10px; font-size: 12px; font-weight: 600; margin-top: 6px; "
                "color: %1; border: 1px solid %1; background: transparent; }"
                "QPushButton:hover { background: rgba(0, 229, 255, 0.08); border-color: #00E5FF; }"
                "QPushButton:disabled { color: #6E7681; border: 1px solid #30363D; }")
            .arg(running ? "#2EA043" : "#E0A800"));
}
```

In `refreshLiveDeviceInfo()`, delete the old `statusLabel_` cascade and the `isCameraReachable`/`configuredReal` text branches; end the function with `updateSidebarStatus();`. The 2s timer (wired in Task 1) now drives this — it must never touch form values, only labels/buttons/chip. `toggleCameraRunState()` ends with `refreshLiveDeviceInfo();` (existing) which now also refreshes the sidebar.

- [ ] **Step 2: Build and verify**

Run: `docker rm -f paper_vision_wt_build 2>/dev/null; docker run --rm --name paper_vision_wt_build -v /home/autoinst578/web_vision_pro/.worktrees/feature/polish-device-setting:/app -w /app web-vision-pro:1.0 bash -lc "mkdir -p /app/build && cd /app/build && cmake .. && cmake --build . --target PaperVision_App -- -j\$(nproc)"`
Expected: `[100%] Built target PaperVision_App`, no `error:`.

- [ ] **Step 3: Commit**

```bash
git -C .worktrees/feature/polish-device-setting add src/gui/widgets/CameraDeviceSettingsDialog.cpp
git -C .worktrees/feature/polish-device-setting commit -m "feat: live camera status card and chip in device settings"
```

---

### Task 3: Staged-change model (edit-while-running)

**Files:**
- Modify: `src/gui/widgets/CameraDeviceSettingsDialog.cpp`

**Interfaces:**
- Consumes: `stagedFields_`, `stagedCount()`, `stagedFieldsInGroup(int)`, `navItems_`, `stagedCallouts_`, `applyStagedBtn_` (Task 1).
- Produces: `stageField(const QString&)`, `clearStaged()`, `updateStagedBadges()`, `updateStagedCallouts()`, `updateApplyStagedEnabled()`; field-key vocabulary: `pixelFormat`, `width`, `height`, `offsetX`, `offsetY`, `exposureTimeBase`, `exposureTimeRaw`, `chunkMode`, `chunks` (staged) vs `exposureTimeAbs`, `enableAcquisitionFps`, `fps` (live).

- [ ] **Step 1: Implement staging helpers**

```cpp
void CameraDeviceSettingsDialog::stageField(const QString& field) {
    if (!stagedFields_.contains(field)) {
        stagedFields_.insert(field);
        updateStagedBadges();
        updateStagedCallouts();
        updateApplyStagedEnabled();
    }
}

void CameraDeviceSettingsDialog::clearStaged() {
    stagedFields_.clear();
    updateStagedBadges();
    updateStagedCallouts();
    updateApplyStagedEnabled();
}

void CameraDeviceSettingsDialog::updateStagedBadges() {
    // Amber dot via DecorationRole; empty pixmap removes it.
    const QPixmap amberDot = [this]() {
        QPixmap pm(10, 10);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor("#E0A800"));
        p.setPen(Qt::NoPen);
        p.drawEllipse(1, 1, 8, 8);
        return pm;
    }();
    for (auto it = navItems_.constBegin(); it != navItems_.constEnd(); ++it) {
        const bool hasStaged = !stagedFieldsInGroup(it.key()).isEmpty();
        it.value()->setData(Qt::DecorationRole, hasStaged ? QVariant(amberDot) : QVariant());
    }
}

void CameraDeviceSettingsDialog::updateStagedCallouts() {
    for (auto it = stagedCallouts_.constBegin(); it != stagedCallouts_.constEnd(); ++it) {
        it.value()->setVisible(!stagedFieldsInGroup(it.key()).isEmpty());
    }
}

void CameraDeviceSettingsDialog::updateApplyStagedEnabled() {
    const bool reachable = isCameraReachable(cameraManager_, cameraIndex_, currentInfo_);
    const bool connected = cameraManager_ && (cameraManager_->isCameraConnected(cameraIndex_) || cameraManager_->isCameraOpen(cameraIndex_));
    const bool running = connected && cameraManager_->isCameraRunning(cameraIndex_);
    applyStagedBtn_->setEnabled(editable_ && reachable && !running && !stagedFields_.isEmpty());
}
```

Add `#include <QPainter>` at the top of the .cpp.

- [ ] **Step 2: Route `onValueChanged()` — live vs staged**

Replace the body of `onValueChanged()` with:

```cpp
void CameraDeviceSettingsDialog::onValueChanged() {
    if (populating_) {
        return;
    }

    const CameraInfo previousInfo = currentInfo_;
    currentInfo_.pixelFormat = pixelFormatCombo_->currentText();
    currentInfo_.width = widthSpin_->value();
    currentInfo_.height = heightSpin_->value();
    currentInfo_.offsetX = offsetXSpin_->value();
    currentInfo_.offsetY = offsetYSpin_->value();
    currentInfo_.exposureTimeAbs = exposureTimeAbsSpin_->value();
    currentInfo_.enableExposureTimeBase = enableExposureTimeBaseCheck_->isChecked();
    currentInfo_.exposureTimeBaseAbs = exposureTimeBaseSpin_->value();
    currentInfo_.exposureTimeRaw = exposureTimeRawSpin_->value();
    currentInfo_.enableAcquisitionFps = enableAcquisitionRateCheck_->isChecked();
    currentInfo_.fps = acquisitionRateSpin_->value();
    currentInfo_.chunkModeActive = chunkModeActiveCheck_->isChecked();
    currentInfo_.enabledChunks = selectedChunks();

    // Staged (stop-required) fields
    if (currentInfo_.pixelFormat != previousInfo.pixelFormat) stageField("pixelFormat");
    if (currentInfo_.width != previousInfo.width) stageField("width");
    if (currentInfo_.height != previousInfo.height) stageField("height");
    if (currentInfo_.offsetX != previousInfo.offsetX) stageField("offsetX");
    if (currentInfo_.offsetY != previousInfo.offsetY) stageField("offsetY");
    if (currentInfo_.enableExposureTimeBase != previousInfo.enableExposureTimeBase) stageField("exposureTimeBase");
    if (currentInfo_.exposureTimeBaseAbs != previousInfo.exposureTimeBaseAbs) stageField("exposureTimeBase");
    if (currentInfo_.exposureTimeRaw != previousInfo.exposureTimeRaw) stageField("exposureTimeRaw");
    if (currentInfo_.chunkModeActive != previousInfo.chunkModeActive) stageField("chunkMode");
    if (currentInfo_.enabledChunks != previousInfo.enabledChunks) stageField("chunks");

    // Live (immediate) fields — only when reachable
    const bool reachable = cameraManager_ && isCameraReachable(cameraManager_, cameraIndex_, currentInfo_);
    if (reachable) {
        cameraManager_->setCameraExposure(cameraIndex_, currentInfo_.exposureTimeAbs);
        cameraManager_->setCameraFrameRate(cameraIndex_, currentInfo_.fps, currentInfo_.enableAcquisitionFps);
    }

    persistSharedCameraSettings(cameraIndex_, currentInfo_);
    emit settingsApplied(currentInfo_);

    updateControlAvailability();
    refreshLiveDeviceInfo();
    updateStagedBadges();
    updateStagedCallouts();
    updateApplyStagedEnabled();
}
```

Remove the call to `applyImmediateChanges(includesStopRequiredChanges)` and the now-unused `includesStopRequiredChanges` computation (and the `applyImmediateChanges` method itself — its live-write path is inlined above and its staged path becomes `applyStagedChanges` in Task 4).

- [ ] **Step 3: Update `updateControlAvailability()` for edit-while-running**

`stopRequiredEditable` is gone. New gating:

```cpp
void CameraDeviceSettingsDialog::updateControlAvailability() {
    const bool reachable = isCameraReachable(cameraManager_, cameraIndex_, currentInfo_);
    const bool configuredReal = hasConfiguredRealDevice(currentInfo_);
    const bool baseEnabled = editable_ && (reachable || configuredReal);

    // Staged fields: editable whenever the camera is editable (even while running)
    pixelFormatCombo_->setEnabled(baseEnabled);
    widthSpin_->setEnabled(baseEnabled);
    heightSpin_->setEnabled(baseEnabled);
    offsetXSpin_->setEnabled(baseEnabled);
    offsetYSpin_->setEnabled(baseEnabled);
    enableExposureTimeBaseCheck_->setEnabled(baseEnabled);
    exposureTimeBaseSpin_->setEnabled(baseEnabled && currentInfo_.enableExposureTimeBase);
    exposureTimeRawSpin_->setEnabled(baseEnabled);
    chunkModeActiveCheck_->setEnabled(baseEnabled);
    chunkListWidget_->setEnabled(baseEnabled && currentInfo_.chunkModeActive);

    // Live fields
    exposureTimeAbsSpin_->setEnabled(baseEnabled);
    enableAcquisitionRateCheck_->setEnabled(baseEnabled);
    acquisitionRateSpin_->setEnabled(baseEnabled && currentInfo_.enableAcquisitionFps);

    updateApplyStagedEnabled();
}
```

- [ ] **Step 4: Build and verify**

Run: `docker rm -f paper_vision_wt_build 2>/dev/null; docker run --rm --name paper_vision_wt_build -v /home/autoinst578/web_vision_pro/.worktrees/feature/polish-device-setting:/app -w /app web-vision-pro:1.0 bash -lc "mkdir -p /app/build && cd /app/build && cmake .. && cmake --build . --target PaperVision_App -- -j\$(nproc)"`
Expected: `[100%] Built target PaperVision_App`, no `error:`.

- [ ] **Step 5: Commit**

```bash
git -C .worktrees/feature/polish-device-setting add src/gui/widgets/CameraDeviceSettingsDialog.cpp
git -C .worktrees/feature/polish-device-setting commit -m "feat: staged-change model for stop-required device settings"
```

---

### Task 4: Apply Staged flow + guards

**Files:**
- Modify: `src/gui/widgets/CameraDeviceSettingsDialog.cpp`

**Interfaces:**
- Consumes: `stageField/clearStaged/update*` (Task 3), `validateInputs()`, `persistSharedCameraSettings()`, `hasStopRequiredChanges()` (existing), `toggleCameraRunState()` (existing).
- Produces: `applyStagedChanges()` slot, `onCancelClicked()` slot, staged-loss guard dialog.

- [ ] **Step 1: Implement `applyStagedChanges()`**

```cpp
void CameraDeviceSettingsDialog::applyStagedChanges() {
    if (stagedFields_.isEmpty()) {
        return;
    }
    if (!validateInputs(nullptr)) {
        return;
    }

    const bool connected = cameraManager_ && (cameraManager_->isCameraConnected(cameraIndex_) || cameraManager_->isCameraOpen(cameraIndex_));
    const bool running = connected && cameraManager_->isCameraRunning(cameraIndex_);
    if (running) {
        QMessageBox::information(this, "Apply Staged",
            "Stop the camera first (sidebar button) to apply staged changes.");
        return;
    }

    if (cameraManager_ && connected) {
        cameraManager_->applyCameraDeviceSettings(cameraIndex_, currentInfo_);
    }
    persistSharedCameraSettings(cameraIndex_, currentInfo_);
    emit settingsApplied(currentInfo_);
    clearStaged();
    refreshLiveDeviceInfo();
}
```

Wire the callout "Stop & Apply" buttons (created in Task 1) at the end of `applyStagedChanges()` — or, better, once in the constructor after the connects. Use `findChild<QPushButton*>()` on each callout (each callout contains exactly one button):

```cpp
    // Wire Stop & Apply on every callout: stop the camera if running, then apply staged.
    for (auto it = stagedCallouts_.constBegin(); it != stagedCallouts_.constEnd(); ++it) {
        QPushButton* btn = it.value()->findChild<QPushButton*>();
        if (btn) {
            connect(btn, &QPushButton::clicked, this, [this]() {
                if (cameraManager_ && cameraManager_->isCameraRunning(cameraIndex_)) {
                    cameraManager_->stopCamera(cameraIndex_);
                }
                applyStagedChanges();
                refreshLiveDeviceInfo();
            });
        }
    }
```

- [ ] **Step 2: Implement `onCancelClicked()` with staged-loss guard**

```cpp
void CameraDeviceSettingsDialog::onCancelClicked() {
    if (!stagedFields_.isEmpty()) {
        const bool connected = cameraManager_ && (cameraManager_->isCameraConnected(cameraIndex_) || cameraManager_->isCameraOpen(cameraIndex_));
        const bool running = connected && cameraManager_->isCameraRunning(cameraIndex_);
        QMessageBox box(this);
        box.setWindowTitle("Staged Changes");
        box.setIcon(QMessageBox::Warning);
        box.setText(QString("%1 change(s) are staged but not yet applied to the camera.")
                        .arg(stagedCount()));
        QPushButton* applyAndClose = box.addButton("Apply & Close", QMessageBox::AcceptRole);
        QPushButton* discard = box.addButton("Discard", QMessageBox::DestructiveRole);
        box.addButton("Keep Editing", QMessageBox::RejectRole);
        if (running) {
            applyAndClose->setEnabled(false);
            applyAndClose->setToolTip("Stop the camera to apply staged changes.");
        }
        box.exec();
        if (box.clickedButton() == applyAndClose) {
            applyStagedChanges();
            if (stagedFields_.isEmpty()) {
                accept();
            }
            return;
        }
        if (box.clickedButton() == discard) {
            clearStaged();
            reject();
            return;
        }
        return;  // Keep Editing
    }
    reject();
}
```

Note: `clearStaged()` before `reject()` discards only staged state — live-applied writes stay on the camera (per spec).

- [ ] **Step 3: Delete `applyImmediateChanges()`**

Remove the method definition and its declaration in the header (`updateImpactBanner()` was already deleted in Task 1). Verify no remaining call sites (grep `applyImmediateChanges|updateImpactBanner` → no matches).

- [ ] **Step 4: Build and verify**

Run: `docker rm -f paper_vision_wt_build 2>/dev/null; docker run --rm --name paper_vision_wt_build -v /home/autoinst578/web_vision_pro/.worktrees/feature/polish-device-setting:/app -w /app web-vision-pro:1.0 bash -lc "mkdir -p /app/build && cd /app/build && cmake .. && cmake --build . --target PaperVision_App -- -j\$(nproc)"`
Expected: `[100%] Built target PaperVision_App`, no `error:`.

- [ ] **Step 5: Commit**

```bash
git -C .worktrees/feature/polish-device-setting add src/gui/widgets/CameraDeviceSettingsDialog.h src/gui/widgets/CameraDeviceSettingsDialog.cpp
git -C .worktrees/feature/polish-device-setting commit -m "feat: apply-staged flow with staged-loss guard"
```

---

### Task 5: Visual polish — focus states, consistent tokens, dead UI removal

**Files:**
- Modify: `src/gui/widgets/CameraDeviceSettingsDialog.cpp`

**Interfaces:**
- Consumes: shared style helpers from Task 1 (`groupBoxStyle`, `comboStyle`, `spinStyle`, `doubleSpinStyle`).
- Produces: final dialog styling; `closeDialog()` now guards staged changes.

- [ ] **Step 1: Finish `closeDialog()` semantics**

```cpp
void CameraDeviceSettingsDialog::closeDialog() {
    if (!validateInputs(nullptr)) {
        return;
    }
    if (!stagedFields_.isEmpty()) {
        const bool connected = cameraManager_ && (cameraManager_->isCameraConnected(cameraIndex_) || cameraManager_->isCameraOpen(cameraIndex_));
        const bool running = connected && cameraManager_->isCameraRunning(cameraIndex_);
        QMessageBox box(this);
        box.setWindowTitle("Staged Changes");
        box.setIcon(QMessageBox::Warning);
        box.setText(QString("%1 change(s) are staged but not yet applied. Apply them before closing?")
                        .arg(stagedCount()));
        QPushButton* applyAndClose = box.addButton("Apply & Close", QMessageBox::AcceptRole);
        QPushButton* closeAnyways = box.addButton("Close without Applying", QMessageBox::DestructiveRole);
        box.addButton("Keep Editing", QMessageBox::RejectRole);
        if (running) {
            applyAndClose->setEnabled(false);
            applyAndClose->setToolTip("Stop the camera to apply staged changes.");
        }
        box.exec();
        if (box.clickedButton() == applyAndClose) {
            applyStagedChanges();
            if (stagedFields_.isEmpty()) {
                accept();
            }
            return;
        }
        if (box.clickedButton() == closeAnyways) {
            clearStaged();
            accept();
            return;
        }
        return;  // Keep Editing
    }
    accept();
}
```

- [ ] **Step 2: Style sweep**

Apply to every remaining form control the shared token styles from Task 1 (already wired at construction): verify `comboStyle()`, `spinStyle()`, `doubleSpinStyle()` are used for all combos/spins/doublespins, `groupBoxStyle()` for all groups, checkbox stylesheet `"color: #E3E3E3; font-size: 12px;"` for `enableExposureTimeBaseCheck_`, `enableAcquisitionRateCheck_`, `chunkModeActiveCheck_`. `createInfoValueLabel()` chips keep their current styling. All control heights uniform (~30px) via consistent padding `6px 8px`. Grep for any leftover raw hex stylesheet literals that differ from the token table — normalize them.

- [ ] **Step 3: Build and verify**

Run: `docker rm -f paper_vision_wt_build 2>/dev/null; docker run --rm --name paper_vision_wt_build -v /home/autoinst578/web_vision_pro/.worktrees/feature/polish-device-setting:/app -w /app web-vision-pro:1.0 bash -lc "mkdir -p /app/build && cd /app/build && cmake .. && cmake --build . --target PaperVision_App -- -j\$(nproc)"`
Expected: `[100%] Built target PaperVision_App`, no `error:`.

- [ ] **Step 4: Commit**

```bash
git -C .worktrees/feature/polish-device-setting add src/gui/widgets/CameraDeviceSettingsDialog.cpp
git -C .worktrees/feature/polish-device-setting commit -m "style: unify device settings dialog tokens and focus states"
```

---

### Task 6: Docs + final verification

**Files:**
- Modify: `context.md` (repo root — worktree copy)

**Interfaces:**
- Consumes: nothing new.

- [ ] **Step 1: Update `context.md`**

Find the Device Settings / CameraDeviceSettingsDialog description (source layout + ConfigDialog section). Replace the "tabbed 5-tab dialog" description with: two-pane layout (sidebar status card + run toggle + group nav with staged badges; stacked detail pages), hybrid apply semantics (live: exposure abs/framerate write immediately; staged: format/AOI/exposure base/raw/chunk apply via Apply Staged after camera stop), staged-loss guards on Cancel/Close, 2s live status refresh, admin gating unchanged.

- [ ] **Step 2: Full build + launch + user verification checklist**

Run the worktree build in the app container. Since compose mounts the main checkout, swap the mount to the worktree for the visual check: stop the app container (`docker stop paper_vision_node`), then start a one-off run from the worktree with the same env as compose (DISPLAY=:0, XAUTHORITY, --network host, privileged, `-v /home/autoinst578/web_vision_pro/.worktrees/feature/polish-device-setting:/app -v $XAUTHORITY:/tmp/.docker.xauth:ro -e XAUTHORITY=/tmp/.docker.xauth -e DISPLAY=:0 -e QT_X11_NO_MITSHM=1`), command `bash -lc "mkdir -p /app/build && cd /app/build && cmake .. && cmake --build . --target PaperVision_App -- -j\$(nproc) && cp PaperVision_App PaperVision_App.run && exec ./PaperVision_App.run"`. Verify the app window opens on the display. After the check, `docker stop` that container and restart `paper_vision_node` with `bash ./docker-rebuild-app.sh` (or leave the worktree run active while the user verifies, then swap back).

Ask the user to verify at DISPLAY=:0 (login `admin`/`admin` if prompted):
1. Camera Cards → Device Settings opens the two-pane dialog; sidebar shows model/IP/temp/run state; chip shows Running/Stopped/Offline
2. Edit a staged field (e.g. Width) while camera runs → amber dot on AOI nav item + callout appears; Apply Staged disabled
3. Stop camera → Apply Staged enabled → click → badges clear
4. Cancel with staged changes → guard dialog shows; Discard drops staged state only
5. Exposure (Abs) edit applies live while running; status card reflects it
6. Focus ring visible on all inputs (Tab navigation)
7. Non-admin login → whole dialog read-only

- [ ] **Step 3: Commit**

```bash
git -C .worktrees/feature/polish-device-setting add context.md
git -C .worktrees/feature/polish-device-setting commit -m "docs: describe two-pane device settings dialog"
```

---

## Self-Review

**Spec coverage:**
- Two-pane architecture → Task 1
- Sidebar status card + run toggle → Tasks 1, 2
- Hybrid apply (live vs staged) → Tasks 3, 4
- Staged callouts + nav badges → Tasks 1, 3
- Staged-loss guards → Tasks 4, 5
- Visual tokens + focus states → Tasks 1, 5
- Dead code removal (`applyCloseBtn_`, `resetDeviceBtn_`, impact banner) → Tasks 1, 3, 5
- 2s live refresh → Task 2
- Admin gating → Tasks 2, 3 (gating preserved via `editable_`)
- context.md → Task 6
- Manual verification → every task's build step + Task 6 checklist

**Type consistency:** field keys (`pixelFormat`, `width`, `height`, `offsetX`, `offsetY`, `exposureTimeBase`, `exposureTimeRaw`, `chunkMode`, `chunks`, `exposureTimeAbs`, `enableAcquisitionFps`, `fps`) are defined once in Task 3 and reused consistently; member names introduced in Task 1 match all later uses; `applyStagedChanges()`/`onCancelClicked()`/`onNavChanged()` declared in Task 1 Step 1, defined in Tasks 1/4.


