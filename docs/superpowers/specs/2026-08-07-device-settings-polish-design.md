# Device Settings Dialog Polish — Design Spec

**Date:** 2026-08-07
**Branch:** `feature/polish-device-setting`
**Status:** Approved (sections 1–5 reviewed by user)

## Problem

The per-camera `CameraDeviceSettingsDialog` (`src/gui/widgets/CameraDeviceSettingsDialog.{h,cpp}`, ~750 lines) is functionally complete but rough around the edges:

- 5-tab layout buries camera run state in the Service tab; no live camera summary
- Two "Close" buttons exist (`applyBtn_` + `applyCloseBtn_`); one is always hidden (dead code)
- "Reset Device..." button permanently disabled — no reset capability exists anywhere in the codebase
- Stop-required parameters (pixel format, AOI, exposure base/raw, chunk) are **locked while the camera runs** — an engineer cannot prepare changes before stopping
- Stop-required changes are silently skipped when the camera is running (no staging, no feedback)
- Impact banner is plain text with no severity coding; status text is dense
- No visible focus state on form controls (keyboard navigation is invisible)
- Inline stylesheets duplicated per widget; no shared tokens
- No unsaved/staged-changes guard on close

## Audience

Engineers during camera setup/commissioning. Density and precision matter more than speed; camera state must be unmistakable.

## Goals

1. Two-pane layout: live camera status + run control always visible; settings grouped in a navigable sidebar
2. Hybrid apply semantics: live parameters write immediately; stop-required parameters stage while running and apply explicitly after stop
3. Consistent visual system on the app's existing dark theme; visible focus states; no dead UI
4. Guards against losing staged work

## Non-Goals

- No real device-reset functionality (would be a separate feature; requires new `CameraManager` API)
- No camera hardware behavior changes — write paths (`applyCameraDeviceSettings`, `setCameraExposure`, `setCameraFrameRate`) are reused as-is
- No test framework introduction (project convention: manual verification)

## Design

### Architecture

Two-pane `QDialog`, ~980×680 (resizable), replacing the tabbed layout:

```
┌────────────────────────────────────────────────────────┐
│ Title bar: camera icon + "Camera 1 · Device Settings"  │ live status chip │
├──────────────┬─────────────────────────────────────────┤
│ Sidebar      │ Detail pane (QStackedWidget)            │
│ • status card│  ┌───────────────────────────────────┐  │
│ • run toggle │  │ Group title + one-line hint       │  │
│ • nav groups │  │ form fields                       │  │
│   w/ badges  │  └───────────────────────────────────┘  │
│              │  staged callout (amber) when applicable │
├──────────────┴─────────────────────────────────────────┤
│ Footer: Cancel            [Apply Staged]  [Close]      │
└────────────────────────────────────────────────────────┘
```

- Sidebar: `QListWidget` nav with existing `IconManager` icons; detail: `QStackedWidget`, one page per group
- Groups: **Image Format · AOI · Exposure & Rate · Chunk Data · Device Info · Service**
- Nav items show an amber dot when that group has staged changes

### Sidebar

```
┌──────────────┐
│ ◉ Camera 1   │  status card — icon colored by state
│ Basler scA780│  (green running / amber stopped / red offline)
│ 172.20.2.1   │
│ 42.5 °C      │
│ [■ Stop]     │  run toggle (Stop/Start) — moved from Service tab,
└──────────────┘  always visible; disabled when camera unreachable
│ ● Image Format │
│ ● AOI          │  nav: icon + label
│ ● Exposure & R.│  amber dot = staged changes in that group
│ ● Chunk Data   │  active item = accent underline + tinted background
│ ● Device Info  │
│ ● Service      │
└──────────────┘
```

- Status card refreshes live: model, IP, temperature (exists in `CameraInfo`), run state
- Keyboard: Up/Down moves between groups (QListWidget native)

### Detail pane & apply semantics

Each group page: **title + one-line hint**, then the form. Three field classes:

| Class | Fields | Behavior |
|---|---|---|
| **Live** | Exposure (Abs), Framerate, Enable Framerate | write to camera on change; green ✓ "Applied" feedback inline |
| **Staged** | Pixel Format, AOI (W/H/Offsets), Exposure Time Base/Raw, Chunk Mode + items | editable anytime (even while running); staged locally; amber callout per page: *"N changes staged — stop camera to apply"* |
| **Read-only** | Sensor/Max dims, Resulting framerate, Device Info | info chips as today |

Flow:
1. Engineer edits staged fields while camera runs — badges appear on nav + callout
2. Stops the camera (sidebar toggle or callout's "Stop & Apply" shortcut)
3. Footer **Apply Staged** (enabled only when camera stopped and staged changes exist) writes everything, clears badges, green confirmation
4. **Close** = done; **Cancel** = discard only the **staged** (unapplied) changes — live-applied writes are already on the camera and are never reverted (confirm dialog appears only when staged changes are pending)

Immediate changes keep today's live-write path (`applyImmediateChanges`); stop-required params move to an explicit staged write instead of the current silent-skip-when-running behavior.

### Visual system

Shared stylesheet blocks per widget type (no per-instance duplication):

| Token | Value | Use |
|---|---|---|
| Surface / inset | `#24292E` / `#1C2128` | dialog / inputs, sidebar |
| Border | `#30363D` | default strokes |
| Text / muted | `#E3E3E3` / `#8B949E` | labels, hints |
| Accent | `#00E5FF` | focus, active nav, primary action |
| Success / Warn / Danger | `#2EA043` / `#E0A800` / `#FF5A5A` | applied ✓, staged badge, offline/reset |

- Uniform control height (~30px), 8px radius, 8px padding
- **Focus = 1px accent border** on inputs/combos/spins (currently no visible focus)
- Title bar gets the live status chip (Running/Stopped/Offline, color-coded + icon) — replaces the plain-text impact banner; the staged/live badges take over its job

### Dead code removed

- `applyCloseBtn_` — hidden duplicate "Close" button
- `resetDeviceBtn_` — permanently disabled; no reset capability in `CameraManager`
- Old impact-banner label/status-text logic folded into the chip + callouts

### Behavior & guards

- **Validation:** keep `validateInputs` (W/H > 0, non-negative fps/exposure); runs on Apply Staged, Close, and Cancel-with-pending
- **Staged-loss guard:** on Cancel/Close with unapplied staged changes → dialog *"N changes staged, not yet applied"* with **Apply & Close** (enabled when stopped) / **Discard** / **Cancel**
- **Live refresh:** 2s timer while dialog open — temperature, IP, run state; sidebar reflects external stop/start
- **Field gating:** admin-only stays (`editable_`); live fields editable when camera reachable; staged fields editable when reachable; Apply Staged requires stopped; unreachable camera → read-only form + offline chip
- **Write failures:** inline red error text in the page for non-blocking failures; QMessageBox only for destructive/ambiguous actions

### Files touched

- `src/gui/widgets/CameraDeviceSettingsDialog.h` — restructure members (remove `applyCloseBtn_`, `resetDeviceBtn_`, impact banner labels; add sidebar/nav/stacked members, timer, staged-change bookkeeping)
- `src/gui/widgets/CameraDeviceSettingsDialog.cpp` — rewrite `setupUi`, add `updateSidebarStatus`, `applyStagedChanges`, staged-change tracking, guards
- `src/gui/ConfigDialog.cpp` / `.h` — unchanged API surface (`settingsApplied` signal, `updatedInfo()`, `requiresRestart()` preserved)
- `context.md` — update dialog description

## Verification

- Docker rebuild (`bash ./docker-rebuild-app.sh`), launch on DISPLAY=:0
- User visually verifies: sidebar layout, live status card, staged flow (edit while running → stop → Apply Staged), focus states, guards
- No automated tests (project convention: manual verification only)

## Risks

- Two-pane restructure is a large single-file rewrite of `setupUi` (~300 lines) — keep form-building helpers shared to limit churn
- Staged semantics change `updateControlAvailability` behavior — must not regress admin gating
- Timer-based refresh must not fight user edits (refresh only read-only fields and run state, never form values being edited)
