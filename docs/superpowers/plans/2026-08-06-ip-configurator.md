# Embedded IP Configurator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Embed a Basler-style IP configurator (discovery table, Static/DHCP/AutoIP apply, card sync) as a sub-tab of the Camera Configuration window.

**Architecture:** New `IpConfiguratorPanel` widget (discovery table + edit area) sits in a sub-tab of ConfigDialog's Camera Configuration tab. It emits `applyRequested`; ConfigDialog stops acquisition, calls the new `CameraManager::configureIpConfiguration(mac, mode, ip, mask, gw)`, restarts acquisition, syncs the matching `CameraCard` by normalized MAC, and reports back via `setApplyResult`. CameraManager gains mode-aware enumeration fields and a mode-aware IP config function (static path = existing direct device API + broadcast fallback; DHCP/AutoIP = broadcast + restart, per the reference `Utility_IpConfig` sample).

**Tech Stack:** C++17, Qt 5.15 Widgets, Pylon 6.2.0 GigE transport layer (`IGigETransportLayer`), CMake, Docker build.

## Global Constraints

- C++17, 4-space indent, K&R braces, `nullptr` not `NULL` (AGENTS.md).
- Class names `PascalCase`, members `snake_case_`, constants `UPPER_SNAKE_CASE`.
- Qt MOC: any new `Q_OBJECT` class header MUST be added to `HEADERS` in CMakeLists.txt.
- Commit format: `type: description` (feat, fix, docs, style, refactor, test, chore, docker).
- Worktree: `.worktrees/feature/change-ip-basler/`; branch `feature/change-ip-basler`. Main checkout untouched.
- No unit-test infrastructure exists (hardware-dependent Qt/pylon app, Docker builds). Verification per task = successful docker compile (`docker-rebuild-app.sh` runs `cmake --build` inside the container) + manual UI checks listed in Task 5.
- Pylon API: `IGigETransportLayer::BroadcastIpConfiguration(mac, isStatic, isDhcp, ip, mask, gw, userDefinedName)`; MAC strings delimiter-free uppercase (e.g. `0030531596CF`).
- All IP config writes happen only while acquisition is stopped (see Task 4).

---

### Task 1: CameraManager — mode-aware discovery + `configureIpConfiguration`

**Files:**
- Modify: `src/core/CameraManager.h` (GigEDeviceInfo struct + class declarations)
- Modify: `src/core/CameraManager.cpp` (enumerateGigEDevices, new configureIpConfiguration, applyIpConfiguration delegation)

**Interfaces:**
- Consumes: existing `normalizeMacAddress` (file-local, anonymous namespace in CameraManager.cpp), `IGigETransportLayer` helpers already in use.
- Produces:
  - `struct GigEDeviceInfo` gains: `std::string ipConfigMode;` (`"Static"`/`"DHCP"`/`"AutoIP"`), `bool supportsPersistentIp = false;`, `bool supportsDhcp = false;`, `bool supportsAutoIp = false;`
  - `static bool CameraManager::configureIpConfiguration(const std::string& mac, const std::string& mode, const std::string& ip, const std::string& mask, const std::string& gateway);` — `mode` is `"Static"` | `"DHCP"` | `"AutoIP"`; returns true on success.
  - `applyIpConfiguration(mac, ip, mask, gw)` behavior unchanged (static), implemented as a wrapper.

- [ ] **Step 1: Extend `GigEDeviceInfo`**

In `src/core/CameraManager.h`, after `userDefinedName` add:

```cpp
    std::string ipConfigMode;        // "Static", "DHCP", or "AutoIP"
    bool supportsPersistentIp = false;
    bool supportsDhcp = false;
    bool supportsAutoIp = false;
```

- [ ] **Step 2: Populate new fields in `enumerateGigEDevices()`**

In `src/core/CameraManager.cpp` inside the `for (const auto& dev : lstDevices)` loop of `enumerateGigEDevices()`, after the `GetPropertyValue` block, add:

```cpp
            if (dev.IsPersistentIpActive())      info.ipConfigMode = "Static";
            else if (dev.IsDhcpActive())         info.ipConfigMode = "DHCP";
            else                                 info.ipConfigMode = "AutoIP";
            info.supportsPersistentIp = dev.IsPersistentIpSupported();
            info.supportsDhcp         = dev.IsDhcpSupported();
            info.supportsAutoIp       = dev.IsAutoIpSupported();
```

- [ ] **Step 3: Add `configureIpConfiguration`**

In `src/core/CameraManager.cpp`, replace the body of `applyIpConfiguration` with a delegation and add the new function. The new function contains the current `applyIpConfiguration` body plus a mode switch:

```cpp
bool CameraManager::applyIpConfiguration(const std::string& mac, const std::string& ip,
                                         const std::string& mask, const std::string& gateway) {
    return configureIpConfiguration(mac, "Static", ip, mask, gateway);
}

bool CameraManager::configureIpConfiguration(const std::string& mac, const std::string& mode,
                                             const std::string& ip, const std::string& mask,
                                             const std::string& gateway) {
    const bool isStatic = (mode == "Static");
    const bool isDhcp   = (mode == "DHCP");
    const bool isAuto   = (mode == "AutoIP");
    if (!isStatic && !isDhcp && !isAuto) {
        std::cerr << "[CameraManager] Unknown IP config mode: " << mode << std::endl;
        return false;
    }
    try {
        Pylon::CTlFactory& TlFactory = Pylon::CTlFactory::GetInstance();
        Pylon::IGigETransportLayer* pTl = dynamic_cast<Pylon::IGigETransportLayer*>(TlFactory.CreateTl(Pylon::BaslerGigEDeviceClass));
        if (pTl == nullptr) {
            std::cerr << "[CameraManager] Error: No GigE transport layer installed." << std::endl;
            return false;
        }

        Pylon::DeviceInfoList_t lstDevices;
        pTl->EnumerateAllDevices(lstDevices);
        const std::string targetMac = normalizeMacAddress(mac);
        std::string userDefinedName = "";
        std::string currentIp;
        Pylon::CDeviceInfo matchedDeviceInfo;
        bool found = false;
        for (const auto& dev : lstDevices) {
            const std::string enumeratedMac = normalizeMacAddress(dev.GetMacAddress().c_str());
            if (enumeratedMac == targetMac) {
                found = true;
                matchedDeviceInfo = dev;
                userDefinedName = dev.GetUserDefinedName().c_str();
                Pylon::String_t val;
                if (dev.GetPropertyValue("IpAddress", val)) currentIp = val.c_str();
                break;
            }
        }

        if (!found) {
            std::cerr << "[CameraManager] Cannot apply IP config: target MAC " << mac
                      << " was not found in the current GigE device discovery list." << std::endl;
            TlFactory.ReleaseTl(pTl);
            return false;
        }

        std::cout << "[CameraManager] Applying GigE IP config: MAC=" << targetMac
                  << " mode=" << mode << " currentIp=" << (currentIp.empty() ? "<unknown>" : currentIp)
                  << " targetIp=" << ip << " mask=" << mask << " gateway=" << gateway << std::endl;

        // Static: prefer the direct device API when the camera is currently reachable.
        if (isStatic) {
            try {
                std::unique_ptr<Pylon::IPylonDevice> device(TlFactory.CreateDevice(matchedDeviceInfo));
                Pylon::CBaslerGigEInstantCamera camera(device.release());
                camera.Open();
                camera.SetPersistentIpAddress(ip.c_str(), mask.c_str(), gateway.c_str());
                camera.ChangeIpConfiguration(true, false);
                camera.Close();
                TlFactory.ReleaseTl(pTl);
                std::cout << "[CameraManager] Successfully changed persistent IP for MAC " << targetMac
                          << " to " << ip << " using direct GigE device API." << std::endl;
                return true;
            } catch (const Pylon::GenericException& e) {
                std::cerr << "[CameraManager] Direct GigE IP configuration failed for MAC " << targetMac
                          << ": " << e.GetDescription() << ". Falling back to broadcast IP configuration." << std::endl;
            }
        }

        // DHCP / AutoIP / static-fallback: broadcast configuration, then restart.
        bool setOk = pTl->BroadcastIpConfiguration(
            targetMac.c_str(), isStatic, isDhcp,
            isStatic ? ip.c_str() : "0.0.0.0",
            isStatic ? mask.c_str() : "0.0.0.0",
            isStatic ? gateway.c_str() : "0.0.0.0",
            userDefinedName.c_str());

        if (setOk) {
            pTl->RestartIpConfiguration(targetMac.c_str());
            std::cout << "[CameraManager] Successfully changed IP config for MAC " << targetMac
                      << " mode=" << mode << (isStatic ? (" to " + ip) : "") << std::endl;
        } else {
            std::cerr << "[CameraManager] Failed to change IP config for MAC " << targetMac
                      << " mode=" << mode << " (input=" << mac << ")" << std::endl;
        }

        TlFactory.ReleaseTl(pTl);
        return setOk;
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Error applying IP config: " << e.GetDescription() << std::endl;
        return false;
    }
}
```

Add the declaration to `src/core/CameraManager.h` next to `applyIpConfiguration`:

```cpp
    static bool applyIpConfiguration(const std::string& mac, const std::string& ip, const std::string& mask, const std::string& gateway);
    static bool configureIpConfiguration(const std::string& mac, const std::string& mode, const std::string& ip, const std::string& mask, const std::string& gateway);
```

- [ ] **Step 4: Compile check + commit**

Run the docker rebuild (compiles everything): `bash ./docker-rebuild-app.sh` from the worktree root, then verify `docker logs paper_vision_node` shows `[100%] Built target PaperVision_App`. (NOTE: compose mounts the MAIN checkout — see Task 5 Step 3 for the worktree build command; for this check a main-checkout build suffices since it shares the same code until Task 2 lands. If main is behind, build inside the worktree container instead: see Task 5 Step 3.)

Commit:

```bash
git add src/core/CameraManager.h src/core/CameraManager.cpp
git commit -m "feat: add mode-aware GigE discovery and IP configuration"
```

---

### Task 2: `IpConfiguratorPanel` widget

**Files:**
- Create: `src/gui/widgets/IpConfiguratorPanel.h`
- Create: `src/gui/widgets/IpConfiguratorPanel.cpp`
- Modify: `CMakeLists.txt` (SOURCES + HEADERS)

**Interfaces:**
- Consumes: `CameraManager::enumerateGigEDevices()` (Task 1), `GigEDeviceInfo` fields (Task 1).
- Produces:
  - `class IpConfiguratorPanel : public QWidget` with `Q_OBJECT`
  - `void refresh()` — public; re-enumerates and repopulates the table.
  - `void setAdminMode(bool isAdmin)` — public.
  - `void setApplyResult(bool ok, const QString& message)` — public; feedback + delayed refresh.
  - Signal `void applyRequested(const QString& mac, const QString& mode, const QString& ip, const QString& mask, const QString& gateway);`
  - Signal `void statusMessage(const QString& message);`
- Style: follow ConfigDialog widget styling patterns (QGroupBox section style, `QTableWidget` dark theme, primary-color buttons). Reuse `IconManager::instance().refresh()`-style icons if available, else plain text buttons.

- [ ] **Step 1: Header**

`src/gui/widgets/IpConfiguratorPanel.h`:

```cpp
#pragma once

#include <QWidget>
#include <QString>
#include <vector>
#include "../../core/CameraManager.h"

class QTableWidget;
class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QGridLayout;

class IpConfiguratorPanel : public QWidget {
    Q_OBJECT

public:
    explicit IpConfiguratorPanel(QWidget* parent = nullptr);

    void refresh();
    void setAdminMode(bool isAdmin);
    void setApplyResult(bool ok, const QString& message);

signals:
    void applyRequested(const QString& mac, const QString& mode, const QString& ip,
                        const QString& mask, const QString& gateway);
    void statusMessage(const QString& message);

private slots:
    void onRefreshClicked();
    void onTableSelectionChanged();
    void onModeChanged(int index);
    void onApplyClicked();

private:
    void setupUI();
    void populateTable(const std::vector<GigEDeviceInfo>& devices);
    void loadRowIntoEditor(int row);
    static bool isValidIpv4(const QString& text);

    QTableWidget* table_;
    QLabel* statusLabel_;
    QComboBox* modeCombo_;
    QLineEdit* ipEdit_;
    QLineEdit* maskEdit_;
    QLineEdit* gatewayEdit_;
    QPushButton* applyBtn_;
    QPushButton* refreshBtn_;

    std::vector<GigEDeviceInfo> devices_;
    bool adminMode_ = true;
    bool applyInFlight_ = false;
};
```

- [ ] **Step 2: Implementation**

`src/gui/widgets/IpConfiguratorPanel.cpp` — full implementation:

```cpp
#include "IpConfiguratorPanel.h"
#include <QTableWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QTimer>
#include <QDateTime>
#include <QHostAddress>

IpConfiguratorPanel::IpConfiguratorPanel(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void IpConfiguratorPanel::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // Toolbar
    auto* toolbar = new QHBoxLayout();
    refreshBtn_ = new QPushButton("Refresh", this);
    connect(refreshBtn_, &QPushButton::clicked, this, &IpConfiguratorPanel::onRefreshClicked);
    toolbar->addWidget(refreshBtn_);
    statusLabel_ = new QLabel("No scan performed yet.", this);
    statusLabel_->setWordWrap(true);
    toolbar->addWidget(statusLabel_, 1);
    layout->addLayout(toolbar);

    // Discovery table
    table_ = new QTableWidget(this);
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels(QStringList()
        << "Friendly Name" << "User-Defined Name" << "MAC" << "IP Address"
        << "Subnet Mask" << "Gateway" << "Mode");
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    connect(table_, &QTableWidget::itemSelectionChanged,
            this, &IpConfiguratorPanel::onTableSelectionChanged);
    layout->addWidget(table_, 1);

    // Edit area
    auto* editGroup = new QGroupBox("Apply IP Configuration", this);
    auto* editLayout = new QGridLayout(editGroup);
    editLayout->setSpacing(8);

    modeCombo_ = new QComboBox(editGroup);
    modeCombo_->addItem("Static", QStringLiteral("Static"));
    modeCombo_->addItem("DHCP", QStringLiteral("DHCP"));
    modeCombo_->addItem("AutoIP", QStringLiteral("AutoIP"));
    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &IpConfiguratorPanel::onModeChanged);
    editLayout->addWidget(new QLabel("Mode:", editGroup), 0, 0);
    editLayout->addWidget(modeCombo_, 0, 1);

    ipEdit_ = new QLineEdit(editGroup);
    maskEdit_ = new QLineEdit(editGroup);
    gatewayEdit_ = new QLineEdit(editGroup);
    editLayout->addWidget(new QLabel("IP Address:", editGroup), 1, 0);
    editLayout->addWidget(ipEdit_, 1, 1);
    editLayout->addWidget(new QLabel("Subnet Mask:", editGroup), 2, 0);
    editLayout->addWidget(maskEdit_, 2, 1);
    editLayout->addWidget(new QLabel("Gateway:", editGroup), 3, 0);
    editLayout->addWidget(gatewayEdit_, 3, 1);

    applyBtn_ = new QPushButton("Apply to Camera", editGroup);
    applyBtn_->setEnabled(false);
    connect(applyBtn_, &QPushButton::clicked, this, &IpConfiguratorPanel::onApplyClicked);
    editLayout->addWidget(applyBtn_, 4, 0, 1, 2);

    layout->addWidget(editGroup);
    onModeChanged(0);
}

void IpConfiguratorPanel::refresh() {
    devices_ = CameraManager::enumerateGigEDevices();
    populateTable(devices_);
    if (devices_.empty()) {
        statusLabel_->setText("No GigE devices found. Check the network connection and click Refresh.");
    } else {
        statusLabel_->setText(QString("%1 device(s) found at %2.")
            .arg(devices_.size())
            .arg(QDateTime::currentDateTime().toString("HH:mm:ss")));
    }
    applyBtn_->setEnabled(false);
    emit statusMessage(QString("IP Configurator: scan found %1 GigE device(s).").arg(devices_.size()));
}

void IpConfiguratorPanel::populateTable(const std::vector<GigEDeviceInfo>& devices) {
    table_->setRowCount(0);
    table_->setRowCount(static_cast<int>(devices.size()));
    for (int row = 0; row < static_cast<int>(devices.size()); ++row) {
        const GigEDeviceInfo& dev = devices[static_cast<size_t>(row)];
        const QStringList values = {
            QString::fromStdString(dev.friendlyName),
            QString::fromStdString(dev.userDefinedName),
            QString::fromStdString(dev.macAddress),
            QString::fromStdString(dev.ipAddress),
            QString::fromStdString(dev.subnetMask),
            QString::fromStdString(dev.defaultGateway),
            QString::fromStdString(dev.ipConfigMode),
        };
        for (int col = 0; col < values.size(); ++col) {
            auto* item = new QTableWidgetItem(values.at(col));
            if (col == 2) item->setFont(QFont("Monospace"));
            table_->setItem(row, col, item);
        }
    }
}

void IpConfiguratorPanel::onRefreshClicked() {
    refresh();
}

void IpConfiguratorPanel::onTableSelectionChanged() {
    const int row = table_->currentRow();
    applyBtn_->setEnabled(row >= 0 && adminMode_ && !applyInFlight_);
    if (row >= 0) loadRowIntoEditor(row);
}

void IpConfiguratorPanel::loadRowIntoEditor(int row) {
    if (row < 0 || row >= static_cast<int>(devices_.size())) return;
    const GigEDeviceInfo& dev = devices_[static_cast<size_t>(row)];
    const int modeIndex = modeCombo_->findData(QString::fromStdString(dev.ipConfigMode));
    modeCombo_->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
    ipEdit_->setText(QString::fromStdString(dev.ipAddress));
    maskEdit_->setText(QString::fromStdString(dev.subnetMask));
    gatewayEdit_->setText(QString::fromStdString(dev.defaultGateway));
}

void IpConfiguratorPanel::onModeChanged(int) {
    const bool staticMode = modeCombo_->currentData().toString() == QStringLiteral("Static");
    ipEdit_->setEnabled(staticMode);
    maskEdit_->setEnabled(staticMode);
    gatewayEdit_->setEnabled(staticMode);
}

void IpConfiguratorPanel::onApplyClicked() {
    const int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(devices_.size())) return;
    const QString mac = QString::fromStdString(devices_[static_cast<size_t>(row)].macAddress);
    const QString mode = modeCombo_->currentData().toString();
    const QString ip = ipEdit_->text().trimmed();
    const QString mask = maskEdit_->text().trimmed();
    const QString gateway = gatewayEdit_->text().trimmed();

    if (mode == QStringLiteral("Static")) {
        if (!isValidIpv4(ip) || !isValidIpv4(mask) || !isValidIpv4(gateway)) {
            QMessageBox::warning(this, "Apply IP",
                "IP address, subnet mask, and gateway must be valid IPv4 addresses (e.g. 192.168.1.10).");
            return;
        }
    }

    applyInFlight_ = true;
    applyBtn_->setEnabled(false);
    statusLabel_->setText(QString("Applying %1 configuration to %2 ...").arg(mode, mac));
    emit applyRequested(mac, mode, ip, mask, gateway);
}

void IpConfiguratorPanel::setApplyResult(bool ok, const QString& message) {
    applyInFlight_ = false;
    applyBtn_->setEnabled(table_->currentRow() >= 0 && adminMode_);
    statusLabel_->setText(message);
    if (ok) {
        QTimer::singleShot(3000, this, &IpConfiguratorPanel::refresh);
    } else {
        QMessageBox::critical(this, "Apply IP", message);
    }
}

void IpConfiguratorPanel::setAdminMode(bool isAdmin) {
    adminMode_ = isAdmin;
    refreshBtn_->setEnabled(true);
    table_->setEnabled(isAdmin);
    modeCombo_->setEnabled(isAdmin);
    applyBtn_->setEnabled(isAdmin && table_->currentRow() >= 0 && !applyInFlight_);
    onModeChanged(0); // re-apply field enable state
}

bool IpConfiguratorPanel::isValidIpv4(const QString& text) {
    QHostAddress addr;
    return addr.setAddress(text.trimmed()) && addr.protocol() == QAbstractSocket::IPv4Protocol;
}
```

Include `<QAbstractSocket>` and `<QFont>` headers in the cpp (QHostAddress/QAbstractSocket live in QtNetwork — already linked: `Qt5::Network`).

- [ ] **Step 3: Register in CMakeLists.txt**

In `CMakeLists.txt`:
- `SOURCES`: add `src/gui/widgets/IpConfiguratorPanel.cpp` (next to the other widgets sources).
- `HEADERS`: add `src/gui/widgets/IpConfiguratorPanel.h` (required for MOC).

- [ ] **Step 4: Commit**

```bash
git add src/gui/widgets/IpConfiguratorPanel.h src/gui/widgets/IpConfiguratorPanel.cpp CMakeLists.txt
git commit -m "feat: add embedded IP configurator panel widget"
```

---

### Task 3: `CameraCard::setNetworkConfig`

**Files:**
- Modify: `src/gui/widgets/CameraCard.h`
- Modify: `src/gui/widgets/CameraCard.cpp`

**Interfaces:**
- Consumes: existing members `ipLabel_`, `subnetEdit_`, `gatewayEdit_`, `cameraInfo_`.
- Produces: `void CameraCard::setNetworkConfig(const QString& ip, const QString& mask, const QString& gateway);` — updates the configured-IP label, mask/gateway edits, and `cameraInfo_` so getters (`ipAddress()`, `subnetMask()`, `gateway()`) and persistence reflect the new values. MAC is NOT changed.

- [ ] **Step 1: Header declaration**

In `src/gui/widgets/CameraCard.h` public section, next to `setDetectedIp`:

```cpp
    void setNetworkConfig(const QString& ip, const QString& mask, const QString& gateway);
```

- [ ] **Step 2: Implementation**

In `src/gui/widgets/CameraCard.cpp`, after `setDetectedIp`:

```cpp
void CameraCard::setNetworkConfig(const QString& ip, const QString& mask, const QString& gateway) {
    ipLabel_->setText(ip);
    subnetEdit_->setText(mask);
    gatewayEdit_->setText(gateway);
    cameraInfo_.ipAddress = ip;
    cameraInfo_.subnetMask = mask;
    cameraInfo_.defaultGateway = gateway;
}
```

- [ ] **Step 3: Commit**

```bash
git add src/gui/widgets/CameraCard.h src/gui/widgets/CameraCard.cpp
git commit -m "feat: add camera card network config sync method"
```

---

### Task 4: ConfigDialog integration

**Files:**
- Modify: `src/gui/ConfigDialog.h`
- Modify: `src/gui/ConfigDialog.cpp`
- Modify: `context.md`

**Interfaces:**
- Consumes: `IpConfiguratorPanel` (Task 2), `configureIpConfiguration` (Task 1), `CameraCard::setNetworkConfig` (Task 3), existing file-local `normalizeMac`, `normalizeIp`, `persistCameraNetworkSelection(cameraId, source, ip, mac, mask, gateway)` (anonymous namespace, ConfigDialog.cpp top), `cameraCards_`, `currentGigEDevices_`, `connectionLogsBrowser_`, `isAdminMode_`.
- Produces: member `IpConfiguratorPanel* ipConfiguratorPanel_;`, slot `void onIpConfiguratorApplyRequested(const QString& mac, const QString& mode, const QString& ip, const QString& mask, const QString& gateway);`. Removes `onOpenIpConfiguratorClicked` (declaration + definition).

- [ ] **Step 1: Header changes**

`src/gui/ConfigDialog.h`:
- Add forward declaration `class IpConfiguratorPanel;` in the private forward-declaration area.
- Remove `void onOpenIpConfiguratorClicked();` (line ~61).
- Add slot `void onIpConfiguratorApplyRequested(const QString& mac, const QString& mode, const QString& ip, const QString& mask, const QString& gateway);` near the other private slots.
- Add member `IpConfiguratorPanel* ipConfiguratorPanel_ = nullptr;`.

- [ ] **Step 2: Sub-tab restructure of the Camera Configuration tab**

In `ConfigDialog::setupUI()` (`src/gui/ConfigDialog.cpp`, camera tab block ~lines 572-632): wrap the camera-cards scroll area and the Save button row in a `QTabWidget` with two tabs. Replace:

```cpp
    camSetupLayout->addWidget(cameraScrollArea_, 1);

    QHBoxLayout* cameraActionsLayout = new QHBoxLayout();
    cameraActionsLayout->addStretch();

    cameraSaveBtn_ = new QPushButton("Save Camera Configuration", camSetupGroup);
    cameraSaveBtn_->setIcon(IconManager::instance().save(16));
    cameraSaveBtn_->setDefault(true);
    stylePrimaryActionButton(cameraSaveBtn_, tc);
    connect(cameraSaveBtn_, &QPushButton::clicked, this, &ConfigDialog::saveCameraConfiguration);
    cameraActionsLayout->addWidget(cameraSaveBtn_);
    camSetupLayout->addLayout(cameraActionsLayout);
```

with:

```cpp
    QTabWidget* cameraSubTabs = new QTabWidget(camSetupGroup);
    cameraSubTabs->setDocumentMode(true);
    cameraSubTabs->setStyleSheet(
        "QTabWidget::pane { border: 1px solid " + tc.border + "; border-radius: 10px; top: -1px; background-color: rgba(255, 255, 255, 0.01); padding: 2px; } "
        "QTabBar::tab { background-color: " + tc.btnBg + "; color: " + tc.text + "; border: 1px solid " + tc.border + "; border-bottom: none; padding: 6px 14px; min-width: 110px; border-top-left-radius: 8px; border-top-right-radius: 8px; font-weight: 600; font-size: 12px; } "
        "QTabBar::tab:selected { color: " + tc.primary + "; background-color: rgba(255, 255, 255, 0.04); margin-bottom: -1px; } "
        "QTabBar::tab:!selected { margin-top: 3px; color: " + tc.text + "; } "
        "QTabBar::tab:hover { color: " + tc.primary + "; }"
    );

    // Tab 1: Camera Cards (existing scroll area + save button)
    QWidget* cameraCardsPage = new QWidget(cameraSubTabs);
    QVBoxLayout* cameraCardsPageLayout = new QVBoxLayout(cameraCardsPage);
    cameraCardsPageLayout->setContentsMargins(0, 8, 0, 0);
    cameraCardsPageLayout->setSpacing(kSectionSpacing);
    cameraCardsPageLayout->addWidget(cameraScrollArea_, 1);

    QHBoxLayout* cameraActionsLayout = new QHBoxLayout();
    cameraActionsLayout->addStretch();
    cameraSaveBtn_ = new QPushButton("Save Camera Configuration", cameraCardsPage);
    cameraSaveBtn_->setIcon(IconManager::instance().save(16));
    cameraSaveBtn_->setDefault(true);
    stylePrimaryActionButton(cameraSaveBtn_, tc);
    connect(cameraSaveBtn_, &QPushButton::clicked, this, &ConfigDialog::saveCameraConfiguration);
    cameraActionsLayout->addWidget(cameraSaveBtn_);
    cameraCardsPageLayout->addLayout(cameraActionsLayout);
    cameraSubTabs->addTab(cameraCardsPage, "Camera Cards");

    // Tab 2: IP Configurator
    ipConfiguratorPanel_ = new IpConfiguratorPanel(cameraSubTabs);
    cameraSubTabs->addTab(ipConfiguratorPanel_, "IP Configurator");

    camSetupLayout->addWidget(cameraSubTabs, 1);
```

(NOTE: `tc` is the `ThemeColors` already in scope in `setupUI()`; `kSectionSpacing` too.)

- [ ] **Step 3: Wire panel signals**

In the constructor after `setupUI();` (or at the end of `setupUI()`), add:

```cpp
    connect(ipConfiguratorPanel_, &IpConfiguratorPanel::applyRequested,
            this, &ConfigDialog::onIpConfiguratorApplyRequested);
    connect(ipConfiguratorPanel_, &IpConfiguratorPanel::statusMessage,
            this, [this](const QString& message) {
        if (connectionLogsBrowser_) connectionLogsBrowser_->append(message);
    });
    ipConfiguratorPanel_->refresh();
```

- [ ] **Step 4: Implement `onIpConfiguratorApplyRequested`**

Add to `src/gui/ConfigDialog.cpp` (replace the dead `onOpenIpConfiguratorClicked` definition at ~line 2960):

```cpp
void ConfigDialog::onIpConfiguratorApplyRequested(const QString& mac, const QString& mode,
                                                  const QString& ip, const QString& mask,
                                                  const QString& gateway) {
    const QString normalizedMac = normalizeMac(mac);
    if (normalizedMac.isEmpty()) {
        ipConfiguratorPanel_->setApplyResult(false, "Invalid MAC address.");
        return;
    }

    // Stop acquisition while the camera network stack is reconfigured.
    bool wasRunning = false;
    if (cameraManager_) {
        cameraManager_->stopAcquisition();
        wasRunning = true;
    }

    const bool ok = CameraManager::configureIpConfiguration(
        normalizedMac.toStdString(), mode.toStdString(),
        ip.toStdString(), mask.toStdString(), gateway.toStdString());

    if (wasRunning && cameraManager_) {
        cameraManager_->startAcquisition();
    }

    if (!ok) {
        ipConfiguratorPanel_->setApplyResult(false,
            "Failed to apply " + mode + " configuration to camera " + mac + ".\n"
            "Check the connection and that the camera supports this mode.");
        return;
    }

    // Sync the matching camera card (by normalized MAC) and persist.
    CameraCard* matchedCard = nullptr;
    for (CameraCard* card : cameraCards_) {
        if (normalizeMac(card->macAddress()) == normalizedMac) {
            matchedCard = card;
            break;
        }
    }
    if (matchedCard) {
        matchedCard->setNetworkConfig(ip, mask, gateway);
        persistCameraNetworkSelection(matchedCard->cameraId(), matchedCard->sourceType(),
                                      ip, normalizedMac, mask, gateway);
    }

    currentGigEDevices_ = CameraManager::enumerateGigEDevices();
    refreshNetworkStatus();
    if (connectionLogsBrowser_) {
        connectionLogsBrowser_->append(QString("[%1] IP config applied: MAC=%2 mode=%3 IP=%4")
            .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"), mac, mode, ip));
    }

    QString message = QString("Successfully applied %1 configuration to %2.").arg(mode, mac);
    if (mode == QStringLiteral("Static")) {
        message += QString(" Camera is expected at %1 after it reconnects.").arg(ip);
    }
    ipConfiguratorPanel_->setApplyResult(true, message);
}
```

Ensure `#include "widgets/IpConfiguratorPanel.h"` is added at the top of ConfigDialog.cpp.

- [ ] **Step 5: Admin gating**

In `ConfigDialog::setAdminMode(bool isAdmin)` (`src/gui/ConfigDialog.cpp` ~line 2834), after the existing card gating, add:

```cpp
    if (ipConfiguratorPanel_) ipConfiguratorPanel_->setAdminMode(isAdmin);
```

- [ ] **Step 6: Remove dead launcher**

Delete `void ConfigDialog::onOpenIpConfiguratorClicked() { QProcess::startDetached(...); }` (replaced by Step 4's new slot; also removed from the header in Step 1). Check `#include <QProcess>` is not needed elsewhere; remove the include if now unused.

- [ ] **Step 7: Update `context.md`**

- Source layout: add `IpConfiguratorPanel.h/cpp` under `src/gui/widgets/`.
- Key Concepts: add short "IP Configurator" subsection (sub-tab in Camera Configuration, discovery table, Static/DHCP/AutoIP via `configureIpConfiguration`, card auto-sync by MAC).
- Remove/update any mention of launching the standalone IpConfigurator app.

- [ ] **Step 8: Compile check**

Docker rebuild from the WORKTREE (see Task 5 Step 3 for exact command), verify `[100%] Built target PaperVision_App`, then commit:

```bash
git add src/gui/ConfigDialog.h src/gui/ConfigDialog.cpp context.md
git commit -m "feat: embed IP configurator in camera configuration tab"
```

---

### Task 5: Build, run, and manual verification

**Files:** none (verification only)

- [ ] **Step 1: Full rebuild from the worktree**

The compose file mounts the MAIN checkout at `/app`. To build the worktree code, run a one-off container with the worktree mounted (no image build/pull — image `web-vision-pro:1.0` exists):

```bash
docker run --rm -d --name paper_vision_worktree_build \
  -v /home/autoinst578/web_vision_pro/.worktrees/feature/change-ip-basler:/app \
  -w /app --entrypoint /bin/bash web-vision-pro:1.0 -lc \
  "mkdir -p /app/build && cd /app/build && cmake .. && cmake --build . --target PaperVision_App -- -j\$(nproc) && cp /app/build/PaperVision_App /app/build/PaperVision_App.run && chmod 755 /app/build/PaperVision_App.run"
```

Wait for `Built target PaperVision_App` in `docker logs paper_vision_worktree_build`, then remove the container. Expected: compile succeeds with no new warnings/errors.

- [ ] **Step 2: Run the app with the new code**

Stop the old container, then launch with the worktree mounted (same env/mounts as compose: X11, XAUTHORITY, privileged, host network):

```bash
docker rm -f paper_vision_node
docker run -d --name paper_vision_node --privileged --network host \
  -e DISPLAY=:0 -e XAUTHORITY=/tmp/.docker.xauth -e QT_X11_NO_MITSHM=1 \
  -e PYLON_ROOT=/opt/pylon \
  -v /home/autoinst578/web_vision_pro/.worktrees/feature/change-ip-basler:/app \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -v /run/user/1000/.mutter-Xwaylandauth.M4Y6S3:/tmp/.docker.xauth:ro \
  -v /home/autoinst578/web_vision_pro/data:/app/data \
  -w /app web-vision-pro:1.0 /bin/bash -lc \
  "cd /app/build && exec ./PaperVision_App.run"
```

Check `docker logs paper_vision_node`: no `could not connect to display`; `[EventDatabase] Initializing with path: /app/data`.

- [ ] **Step 3: Manual UI verification (with the app on DISPLAY=:0)**

1. Open Config → Camera Configuration: two sub-tabs "Camera Cards" and "IP Configurator" visible; network summary header still on top.
2. Open IP Configurator: table populates with discovered GigE devices (or shows "No GigE devices found" status when none — NOT an error popup).
3. Select a row: edit area shows device values + current mode; IP fields enabled only for Static.
4. With admin off: Apply disabled, Refresh enabled.
5. Apply Static with valid IPv4s → status "Successfully applied..." → after ~3 s table auto-refreshes; matching camera card (same MAC) shows updated IP/mask/gateway; Diagnostics log has the entry.
6. Apply with invalid IPv4 → warning box, nothing sent.
7. DHCP/AutoIP apply → broadcast path returns success/failure message.
8. If no physical camera is available: verify 2, 3, 4, 6 and the graceful empty state; live write (5, 7) requires hardware — report as not verified.

- [ ] **Step 4: Final commit (if verification found fixes) + report**

Any fixes found during verification get committed with `fix:` messages. Report results per step 8.

---

## Self-Review Notes (run before execution)

- Spec coverage: placement (Task 4 Step 2), modes (Task 1), card sync (Task 4 Step 4), dead slot removal (Task 4 Step 6), CMake MOC (Task 2 Step 3), context.md (Task 4 Step 7), error handling (panel validation + setApplyResult), delayed refresh (Task 2 Step 2).
- Type consistency: `applyRequested(mac, mode, ip, mask, gateway)` signal ↔ slot signature identical; `configureIpConfiguration` 5 args; `setApplyResult(bool, QString)`.
- No placeholders: every step has concrete code or commands.
