# Changelog

All notable changes to PaperVision are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html). Releases
are listed newest first, grouped as Added / Changed / Fixed / Removed.

The app renders this file in **Help > Changelog**: `## version` starts a release row,
`### group` starts a section, and `- ` lines are its bullets (one bullet per line), so
the format matters. Keep entries user-visible only — internal refactors do not belong
here.

## [Unreleased]

### Changed

- Analysis View event log: an OPC UA trigger now shows only the sensor name in the Reason column (no more "OPC UA:" prefix).

## [1.1.0] - 2026-09-19

### Added

- In-app changelog: Help > Changelog shows the full release history of the app, one row per version.

### Changed

- Help > About reports the app version from a single source (`src/config/AppVersion.h`) instead of a hardcoded string.

## [1.0.0] - 2026-09-19

Initial release.

### Added

- Live View: camera grid with per-tile name, IP, link speed, temperature and PTP badges plus drop counters.
- Detail view: single-camera feed with gain, exposure, gamma and contrast controls, AOI tuning on the live frame, and a polygon detection region.
- Detached Live View Window: video-only grid or single camera, with no settings surface.
- Event capture: OPC UA tag triggers (push-hold, with a per-tag group and recorded sections) and the manual trigger, each recording a pre/post-trigger window from every camera in scope.
- Per-camera event files storing RAW video, sensor and host frame timestamps, frame counters, and the machine speed/position snapshot taken at trigger time.
- Analysis View: event replay with the TRACKS dashboard (thumbnail strip plus brightness and signal tracks), scrubbing and keyboard playback, and per-camera review tabs.
- Defect review: mark a defect on several cameras and align them, using mark-based sync with a speed/position fallback; annotations are saved in the event sidecar.
- System Configuration (admin only): camera cards, fixed IP list, machine groups and layout, GigE IP configurator, OPC UA connection and tag setup, recording & storage settings, and theme presets.
- Diagnostics: per-camera drop-rate sparkline and link-speed monitor, temperature and PTP offset alerts, a low-disk status-bar badge, and an emulation-mode badge.
- In-app Documentation (Docs > Documentation), the PTP / frame-timestamp explanation, and the machine reference document.
