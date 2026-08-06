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
│   ├── AnalysisView.h/cpp            # Event playback with annotations
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
│       ├── CameraDeviceSettingsDialog.h/cpp # Per-camera device settings
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

### OPC UA Integration
- `OpcUaClientService` connects to an OPC UA server via QtOpcUa 5.15.2 (open62541 backend)
- ConfigDialog auto-detects discoverable endpoints when opened; manual "Detect Server" scan; falls back to manual endpoint entry
- Optional username/password authentication
- **Trigger tags**: multiple boolean tags with per-tag edge detection (rising/falling/both), active state, and minimum-interval debounce (default 1500 ms)
- **Speed tag**: numeric value with scale/offset, unit, and stale timeout (default 2000 ms); position direction sign
- Trigger events carry speed snapshot for event metadata
- Configurable publish interval (default 250 ms) and auto-reconnect interval (default 3000 ms)

### Configuration
- `CameraConfig` is the single source of truth for all settings
- Camera config: ID, source (Emulated/Real/Disabled), name, location, IP, MAC, FPS, AOI, exposure, pixel format, chunk data
- Theme system: S/M/L presets for typography, accent color, per-view styling
- OPC UA settings: endpoint URL, auth, publish/reconnect intervals, trigger tags, speed tag
- Event storage path configurable with validation
- Config persisted via `CameraConfig` static methods

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
