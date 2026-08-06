# Embedded IP Configurator — Design

Date: 2026-08-06
Branch: `feature/change-ip-basler`

## Problem

The app currently has no in-app workflow for changing Basler camera IP addresses.
Users must launch the standalone pylon `IpConfigurator` app. The app's only IP
write path is the per-camera-card "Apply IP to Camera" button, which is
static-IP-only and hidden per-card. `onOpenIpConfiguratorClicked` in
ConfigDialog launches `/opt/pylon/bin/IpConfigurator` but is not connected to
any button (dead code).

Reference: `Utility_IpConfig` sample from
`github.com/xlibraboy/sample_code-pylon-6.2.0-basler`, which demonstrates
GigE device enumeration with per-device configuration mode and
`IGigETransportLayer::BroadcastIpConfiguration` + `RestartIpConfiguration`.

## Goals

- Embed an IP configurator workflow in the Camera Configuration window of
  ConfigDialog, matching the standalone Basler IPConfigurator experience:
  discovery table of all GigE cameras on the network, per-camera mode
  (Static / DHCP / AutoIP), edit + apply.
- Support all three configuration modes (Static, DHCP, AutoIP).
- Auto-sync the matching camera card (same normalized MAC) after a successful
  apply so app config always matches the camera.
- Remove the dead standalone-app launcher slot.

## Design Decisions (user-approved)

1. **Placement:** sub-tab "IP Configurator" inside the Camera Configuration
   tab of ConfigDialog, alongside the existing "Camera Cards" tab.
2. **Modes:** Static + DHCP + AutoIP (full parity with standalone tool).
3. **Card sync:** auto-update camera card fields (IP/mask/gateway) matched by
   normalized MAC after successful apply, and persist them.

## Components

### New widget: `src/gui/widgets/IpConfiguratorPanel.{h,cpp}`

`QWidget`, `Q_OBJECT` (must be added to CMake `HEADERS` for MOC).

- Toolbar row: **Refresh** button + status label ("N devices found",
  timestamp, last error).
- `QTableWidget` columns: Friendly Name, User-Defined Name, MAC, IP Address,
  Subnet Mask, Gateway, Mode. Read-only rows; one row per discovered GigE
  device.
- Edit area below the table:
  - Mode combo: Static / DHCP / AutoIP (defaults to device's current mode).
  - IP / Subnet Mask / Gateway line edits — enabled only when Mode = Static.
  - **Apply** button (disabled when no row selected or not admin).
- Signals:
  - `applyRequested(const QString& mac, const QString& mode, const QString& ip,
    const QString& mask, const QString& gateway)` — emitted when Apply is
    clicked; ConfigDialog performs the write so it can stop/restart acquisition.
  - `statusMessage(const QString& message)` — for the Diagnostics connection log.
- Public method:
  - `setApplyResult(bool ok, const QString& message)` — called by ConfigDialog
    after the write completes; shows feedback in the status label, re-enables
    Apply, and on success schedules a delayed `refresh()` ~3 s later (camera
    restarts its network stack after `RestartIpConfiguration`).

Behavior:

- `refresh()` calls `CameraManager::enumerateGigEDevices()` and repopulates
  the table. Empty result → status "No GigE devices found" (not an error).
- Row selection loads device fields into the edit area.
- Apply validates locally: MAC non-empty and present in the discovery list;
  when Mode = Static, IP/mask/gateway must parse as dotted-quad via
  `QHostAddress::setAddress` (reject on failure). MAC normalization uses a
  small local helper in the panel (strip non-hex chars, uppercase) — a 3-line
  duplicate of ConfigDialog's `normalizeMac`, kept local to avoid cross-widget
  coupling.
- On apply: emit `applyRequested` and disable Apply until `setApplyResult`
  arrives. On success, schedule a delayed `refresh()` ~3 s later.
- Admin mode: `setAdminMode(bool)` disables table editing, mode combo, and
  Apply (Refresh stays enabled).

### CameraManager changes (`src/core/CameraManager.{h,cpp}`)

- Extend `GigEDeviceInfo`:
  - `std::string ipConfigMode;` — `"Static"` / `"DHCP"` / `"AutoIP"` derived
    from `IsPersistentIpActive()` / `IsDhcpActive()` / `IsAutoIpActive()`.
  - `bool supportsPersistentIp = false;` from `IsPersistentIpSupported()`.
  - `bool supportsDhcp = false;` from `IsDhcpSupported()`.
  - `bool supportsAutoIp = false;` from `IsAutoIpSupported()`.
- `enumerateGigEDevices()` populates the new fields (per `Utility_IpConfig`).
- New: `static bool configureIpConfiguration(const std::string& mac,
  const std::string& mode, const std::string& ip, const std::string& mask,
  const std::string& gateway);`
  - `mode == "Static"` → existing direct-device-API path
    (`SetPersistentIpAddress` + `ChangeIpConfiguration(true,false)`) with
    broadcast fallback — i.e. the body of today's `applyIpConfiguration`.
  - `mode == "DHCP"` → `BroadcastIpConfiguration(mac, false, true, "0.0.0.0",
    "0.0.0.0", "0.0.0.0", userDefinedName)` + `RestartIpConfiguration(mac)`.
  - `mode == "AutoIP"` → `BroadcastIpConfiguration(mac, false, false,
    "0.0.0.0", "0.0.0.0", "0.0.0.0", userDefinedName)` +
    `RestartIpConfiguration(mac)`.
  - Unknown mode → `false`.
- Keep `applyIpConfiguration(mac, ip, mask, gw)` as a static-mode wrapper
  around `configureIpConfiguration` so the per-card button keeps working.

### ConfigDialog changes (`src/gui/ConfigDialog.{h,cpp}`)

- Camera Configuration tab content moves under a sub-`QTabWidget`:
  - Tab "Camera Cards": existing camera-cards layout (unchanged).
  - Tab "IP Configurator": `IpConfiguratorPanel`.
  - `NetworkSummaryHeader` stays above the sub-tabs.
- Connect `applyRequested`:
  1. Stop acquisition if running (mirror existing `onCameraCardWriteIpClicked`
     behavior).
  2. Call `CameraManager::configureIpConfiguration(mac, mode, ip, mask, gw)`.
  3. Call `panel->setApplyResult(ok, message)`; restart acquisition.
  4. On success: find `CameraCard` with matching normalized MAC; update its
     IP/mask/gateway fields; call `persistCameraNetworkSelection(...)`.
  5. `refreshNetworkStatus()`, append to Diagnostics connection log, update
     `currentGigEDevices_`.
- Admin gating: `setAdminMode` also forwards to the panel.
- Remove `onOpenIpConfiguratorClicked` (dead slot) and its header declaration.

### CMakeLists.txt

Add `src/gui/widgets/IpConfiguratorPanel.cpp` to `SOURCES` and
`src/gui/widgets/IpConfiguratorPanel.h` to `HEADERS` (MOC).

### Docs

Update `context.md`: source layout (new widget), Key Concepts (IP configurator
workflow), remove any mention of launching the standalone IpConfigurator if
present.

## Data Flow

1. Tab shown / Refresh → `enumerateGigEDevices()` → table rows.
2. Row selected → edit area populated with device values + current mode.
3. Apply → local validation → panel emits `applyRequested`.
4. ConfigDialog: stop acquisition (if running) →
   `configureIpConfiguration(mac, mode, ip, mask, gw)` → `setApplyResult` →
   restart acquisition.
5. On success: card sync (normalized MAC match) + persist + Diagnostics log +
   `refreshNetworkStatus()`; panel schedules delayed table refresh.

## Error Handling

- MAC missing/invalid in panel validation → warning box from the panel (same
  wording as existing card flow).
- Write failure → panel shows `QMessageBox::critical` via `setApplyResult`;
  acquisition restarted, no card sync.
- DHCP/AutoIP: IP fields ignored by `BroadcastIpConfiguration`; UI disables
  them so no validation needed in those modes.
- No devices found → status label, empty table, no error popup.
- Direct static API throws → broadcast fallback (existing pattern in
  `applyIpConfiguration`).

## Testing & Verification

- No automated test infrastructure exists in this repo (hardware-dependent
  Qt/pylon app); verification is manual + build:
  - Docker build via `docker-rebuild-app.sh` (rebuild-only, no image build).
  - Launch app: IP Configurator sub-tab renders in Camera Configuration tab.
  - With cameras on the network: table lists devices with correct modes;
    static apply updates camera and syncs the card; DHCP/AutoIP broadcast
    paths return success/failure messages.
  - Without cameras: empty table + "No GigE devices found" status.
  - Admin off: edit/apply disabled; Refresh still works.
- Note: live IP write can only be fully verified with a physical camera on
  the network; otherwise verify UI paths and error handling only.

## Non-Goals

- No camera-card layout changes beyond the sub-tab move.
- No auto-discovery of IP conflicts beyond existing
  `validateConfiguration` behavior.
- No multicast/monitor-mode features from the reference repo.
