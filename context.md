# PaperVision System — Project Context

## Overview

Industrial vision system for paper machine inspection. Captures live video from up to 8 Basler GigE cameras, buffers frames in RAM, and records event-triggered clips (e.g. paper breaks) to disk with full metadata. Runs inside Docker on Ubuntu 20.04 with X11 forwarding for the Qt GUI.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  MainWindow (Qt5 QMainWindow, maximized, Fusion style)  │
│  ┌──────────────┬──────────────────┬──────────────────┐ │
│  │ LiveDashboard│   DetailView     │  AnalysisView    │ │
│  │ (grid of     │   (single-cam    │  (event playback │ │
│  │  CameraWidget│    fullscreen)   │   + annotations) │ │
│  │  tiles)      │                  │                  │ │
│  └──────┬───────┴────────┬─────────┴────────┬─────────┘ │
│         │                │                  │           │
│  ┌──────┴────────────────┴──────────────────┴─────────┐ │
│  │                  Core Layer                        │ │
│  │  CameraManager  EventController  EventDatabase     │ │
│  │  ImageBuffer    DefectDetector   VideoEncoder      │ │
│  └──────────────────────┬─────────────────────────────┘ │
│                         │                               │
│  ┌──────────────────────┴─────────────────────────────┐ │
│  │  ConfigDialog (tabbed: camera, triggers, OPC UA,   │  │
│  │   UI themes, diagnostics)                         │  │
│  │  OpcUaClientService (QtOpcUa + open62541 backend)  │  │
│  └────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

## Tech Stack

| Component | Version / Detail |
|-----------|-----------------|
| Language | C++17 |
| Build | CMake 3.16+ |
| GUI | Qt 5.15.2 (beineri PPA) — Core, Widgets, Gui, Concurrent, Svg, Network, OpcUa |
| Camera SDK | Basler Pylon 6.2.0 (GigE) |
| Image Processing | OpenCV 4.x |
| OPC UA | QtOpcUa 5.15.2 + open62541 v1.5.4 backend |
| Container | Docker (Ubuntu 20.04 base), `network_mode: host`, X11 socket mount |
| Video Encoding | Pylon supplementary MPEG-4 package |

## Source Layout

```
src/
├── main.cpp                          # Entry point, single-instance lock, Pylon init
├── core/
│   ├── CameraManager.h/cpp           # Pylon camera lifecycle, acquisition threads, temp monitoring
│   ├── EventController.h/cpp         # Circular buffer per camera, event recording to .bin
│   ├── EventDatabase.h/cpp           # Event metadata registry, JSON persistence, retention
│   ├── VideoStreamReader.h/cpp       # Reads .bin raw frames for playback
│   ├── SpeedProfile.h/cpp            # Local machine-speed interpolation between anchors
│   ├── BufferPool.h                  # Pre-allocated cv::Mat pool
│   ├── RawFormat.h                   # Custom .bin frame format
│   └── TemperatureStatus.h           # Basler GigE temp thresholds (Ok/Critical/Error)
├── processing/
│   ├── ImageBuffer.h/cpp             # Frame buffer for analysis
│   ├── DefectDetector.h/cpp          # OpenCV-based defect detection
│   └── VideoEncoder.h/cpp            # Encodes event frames to video
├── gui/
│   ├── MainWindow.h/cpp              # Top-level window, view switching, frame routing
│   ├── LiveDashboard.h/cpp           # Camera grid (configurable rows/cols)
│   ├── AnalysisView.h/cpp            # Event playback, TOOLS panel (marker/zoom/defect align)
│   ├── DetailView.h/cpp              # Single-camera detail view
│   ├── ConfigDialog.h/cpp            # Camera config, OPC UA, UI theme settings
│   ├── CameraInfo.h                  # Camera metadata struct
│   └── widgets/
│       ├── CameraWidget.h/cpp        # Individual camera tile
│       ├── CameraCard.h/cpp          # Config card per camera
│       ├── AnalysisVideoWidget.h/cpp # Video playback widget
│       ├── ToggleSwitch.h/cpp        # Custom toggle switch
│       ├── IconManager.h/cpp         # SVG icon management
│       ├── NetworkSummaryHeader.h/cpp# Network status header
│       ├── CameraDeviceSettingsDialog.h/cpp # Per-camera device settings (two-pane: status sidebar + grouped pages, live/staged apply)
│       ├── DeleteConfirmationDialog.h/cpp   # Delete confirmation
│       └── IpConfiguratorPanel.h/cpp        # GigE IP configurator (Static/DHCP/AutoIP)
├── config/
│   └── CameraConfig.h/cpp            # Centralized config: cameras, themes, OPC UA settings
└── communication/
    └── OpcUaClientService.h/cpp      # OPC UA client: trigger tags + speed tag monitoring
```

## Key Concepts

### Camera Lifecycle
- `CameraManager` owns Pylon `CInstantCamera` instances (up to 8 slots)
- Each camera has its own acquisition thread pushing frames via `FrameCallback`
- Supports hot-rebuild: reconfigure without blanking surviving cameras
- Temperature monitoring with Basler GigE thresholds → alerts via callback
- Software image processing pipeline: gain, gamma, contrast (LUT-cached)

### Event Recording (Paper Break Detection)
- `EventController` maintains per-camera circular buffers in RAM (~10s at 55fps)
- Trigger sources: manual button, OPC UA trigger tags (edge-detect, debounce)
- On trigger: captures pre-trigger buffer + post-trigger frames (configurable)
- Saves as custom `.bin` raw format (speed) + `.json` metadata
- `EventDatabase` indexes events, manages retention (permanent/non-permanent)

### Sheet-Break (Trigger) Sensor Semantics
- Sheet-break sensors are **through-beam IR pairs** (transmitter on one side, receiver across the web). The running sheet blocks the beam, so normal running reads **False** ("sheet on"); when no sheet is at the beam the receiver sees the transmitter and the tag reads **True** — TRUE means *sheet absent at this beam location*, not "the web tore".
- The system starts recording on the **first True while idle**: `EventController::triggerEvent` refuses to start while an event is already recording (the `triggering_` guard) and each tag debounces with its own `minimumIntervalMs`, so one break produces one event from the sensor that saw it first. The fired sensor's `positionMm` becomes the event's `triggerPositionMm` (used to center capture windows).
- A fixed-point detector only observes its own spot: a break **upstream** of the beam is detected only when the tail (trailing edge of the still-running downstream sheet) clears the beam — delayed by (sensor position − break point) / machine speed. The pre-trigger RAM buffer (~10 s) bounds how far upstream a break can still be captured completely.
- Coverage rule: at least one sensor per machine section, mounted near where that section actually breaks. The Wire group has **0** sensors today — a wire break is a blind zone, caught only when the tail reaches the Press-Part sensor, by which time upstream cameras may have rolled past it in their pre-trigger buffers.

### OPC UA Integration
- `OpcUaClientService` connects to an OPC UA server via QtOpcUa 5.15.2 (open62541 backend)
- ConfigDialog auto-detects discoverable endpoints when opened; manual "Detect Server" scan; falls back to manual endpoint entry
- Optional username/password authentication
- **Trigger tags**: multiple boolean tags with per-tag edge detection (rising/falling/both), active state, and minimum-interval debounce (default 1500 ms)
- **Speed tags**: numeric values with scale/offset, unit, machine position (mm), and stale timeout (default 2000 ms); position direction sign. Every fresh positioned tag is snapshotted onto each event as a *speed anchor*; a tag with position 0 acts as a single global speed.
- Trigger events carry the speed snapshot + full anchor list for event metadata
- Configurable publish interval (default 250 ms) and auto-reconnect interval (default 3000 ms)

### Defect Sync & Camera Alignment (Analysis View)
- All machine positions share one common ruler in millimetres (camera positions, trigger sensor positions, drive speed-tag positions) — reference: `docs/machine-reference.md`. Position 0 means "unknown" and disables spatial alignment for that element.
- **Mark-based sync (primary, ground truth)**: in review mode, scrub to the defect on a camera and press **Mark Defect** (TOOLS panel → DEFECT ALIGN). Mark the same defect on ≥2 cameras, then press **Align** — per-camera offsets are computed purely from the marks (mean of k-th mark-pair differences, timestamp-mapped to the shared timeline). No machine context (speeds, positions) is required for this path.
- **Speed/position fallback** (used for unmarked cameras, or when <2 cameras carry marks): offset = (Δmm between camera and reference camera) × frames-per-mm, using the local speed interpolated from the event's speed anchors. Requires camera positions (mm) and at least one valid speed snapshot per event.
- **Speed anchors**: each fresh OPC UA speed tag with a position becomes an anchor `{positionMm, speed}` at trigger time; `SpeedProfile::speedAt` interpolates linearly between anchors, so drive draw is compensated. 0 anchors → single global speed (draw ignored); ~1 anchor per drive section (2–5 total) gives good fallback accuracy — the full 36-drive reference list is not required.
- **Trigger sensor positions** are used only at capture time (`EventController::triggerPositionMm` — the position of the sensor whose first-True started the event): when set (>0) with a valid speed, each camera's recording window is centered on when the defect passes it. They do not feed review-time alignment (see Sheet-Break (Trigger) Sensor Semantics above).
- **Mark Defect feedback**: every click shows a transient banner (marked / already marked / not available); the button is visibly disabled outside review mode or without a selected camera, and its enabled state refreshes on camera changes.

### Configuration
- `CameraConfig` is the single source of truth for all settings
- Camera config: ID, source (Emulated/Real/Disabled), name, location, IP, MAC, FPS, AOI, exposure, pixel format, chunk data
- Theme system: S/M/L presets for typography, accent color, per-view styling
- OPC UA settings: endpoint URL, auth, publish/reconnect intervals, trigger tags, speed tag
- Event storage path configurable with validation
- Config persisted via `CameraConfig` static methods

### Device Settings Dialog
- Two-pane layout: left sidebar = live status card (model, IP, temperature, run state) + camera run toggle + group nav with amber badges on groups holding staged changes; right = stacked detail pages for Image Format, AOI, Exposure & Rate, Chunk Data, Device Info, Service
- Hybrid apply semantics: exposure (Abs, base, raw) and framerate write live to the camera while it runs — no restart needed; format/AOI/chunk changes stage while running and apply via Apply Staged (or the Stop & Apply callout after stopping)
- Dialog shows live camera values (read from the device when reachable) rather than saved config
- Staged-loss guards on Cancel/Close confirm before discarding staged changes (note: title-bar X currently bypasses the guard — known minor, tracked for final review)
- 2s live status refresh timer
- Admin gating: `editable_ == false` → whole dialog read-only

### IP Configurator (embedded)
- Sub-tab "IP Configurator" inside the Camera Configuration tab of ConfigDialog
- Discovery table of all GigE cameras on the network: friendly name, user-defined name, MAC, IP, mask, gateway, current mode
- Apply Static / DHCP / AutoIP via `CameraManager::configureIpConfiguration` (direct device API for all modes, with broadcast fallback; direct path retried while the camera restarts its network stack)
- Successful applies auto-sync the matching camera card (by normalized MAC) and persist its network fields
- Acquisition is stopped during reconfiguration; admin-mode gated

### Data Format
- Event files: `data/event_{timestamp}_{index}.json` + `data/event_{timestamp}_{index}_cam{N}.bin`
- Annotations: `data/event_{timestamp}_{index}_annotations.json`
- Snapshots: `data/snapshots/`

## Build & Run

### Docker (primary workflow)
```bash
# Build image (one-time, requires pylon .deb in .docker/)
docker build -t web-vision-pro:1.0 -f .docker/Dockerfile .

# Run via compose (builds app + launches GUI)
docker compose -f .docker/docker-compose.yml up -d

# Quick rebuild (recompiles app inside running container)
./docker-rebuild-app.sh
```

### Container Requirements
- `privileged: true` for GigE camera access
- `network_mode: host` for camera discovery
- X11 socket + XAUTHORITY mount for GUI display
- Pylon runtime at `/opt/pylon`

### CMake Build (inside container)
```bash
mkdir -p build && cd build
cmake ..
cmake --build . --target PaperVision_App -- -j$(nproc)
```

## Conventions

- Classes: `PascalCase`, members: `snake_case_`, constants: `UPPER_SNAKE_CASE`
- Indentation: 4 spaces, K&R braces
- `nullptr` not `NULL`
- Singletons: `EventController`, `EventDatabase` (thread-safe)
- Qt MOC: all `Q_OBJECT` classes listed in `HEADERS` in CMakeLists.txt
- Commit format: `type: description` (feat, fix, docs, style, refactor, test, chore, docker)

## Single-Instance Guard
- `QLockFile` + `QLocalServer` prevents duplicate app windows
- Second launch sends "raise" to existing instance via local socket

## Key Dependencies (system packages in Docker)
- `qt515base`, `qt515svg`, `qt515tools`, `qt515declarative`, `qt515networkauth-no-lgpl`
- `libopencv-dev`
- `open62541` (built from source v1.5.4)
- `qtopcua` (built from source v5.15.2)
- Pylon 6.2.0 (`.deb` install)
- Pylon supplementary MPEG-4 package
