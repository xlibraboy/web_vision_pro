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

### Added

- Detail View: a visible "← Grid" control (and the Esc key) returns to the camera grid; double-clicking the frame still works.
- Analysis View: the TOOLS, IMAGE and TRACKS handles are clickable, keyboard-focusable buttons that pin their panel open (hover still previews), the TRACKS panel toggles with the T key, and Escape closes a pinned panel.
- Analysis View: the signal lanes draw the detector's own limit as a dashed line on a fixed scale ("limit 0.05" on SPOTS %, "floor 10.0" on CONTRAST), so different events are directly comparable.
- Machine Layout: defect marks carry a severity from the event's signal scan (filled = warning, ring = critical, outline = ok); the tooltip shows the verdict and the values behind it, and the summary counts the marks by severity.
- Status bar: a live FPS readout that turns red while an event recording is running.
- Live View: unassigned grid slots are labelled "No camera assigned" instead of rendering as bare dark cells.
- A Light theme alongside the dark one, and a Theme picker that offers Light, Dark, or following the desktop's colour scheme. Follow-system is re-checked while the app runs, so a desktop that switches to dark is followed without a restart.

### Changed

- The Theme picker applies the theme the moment it is selected (the whole app re-colors immediately); Save or Apply keeps it, and leaving the page without saving returns to the saved theme.
- The Theme picker offers Light, Dark and Follow system instead of the accent presets; an installation that had one of those presets saved now runs the dark theme.
- Status colors app-wide now carry one meaning per color (healthy green, caution amber, error coral, informational blue) instead of five near-duplicate palettes, so the same state reads the same in every panel.
- Machine-section colors and the Analysis event-dashboard series are now distinguishable under red-green color blindness.
- Typography: every font size now comes from one named scale (Display 17 · Title 14 · Body 12 · Meta 11 · Micro 10, plus the 8px on-image overlay register); the previous ad-hoc 9/13/14/15/16/20px sizes and the UI Preferences defaults converged onto it.
- Typography: the base UI font is Noto Sans at the body step, and the Analysis HUD/timestamp mono text no longer names an unavailable font (Noto Sans Mono, DejaVu Sans Mono fallback).
- Help > About now reports the pylon runtime version the app is linked against.
- Administrator login/logout is now a clickable pill in the status-bar badge cluster (Admin: Off / Admin: On, with a padlock icon) next to the emulation and low-disk badges; the old tab-bar Login/Logout button is gone, and the pill follows the selected theme with hover, pressed and keyboard-focus states.

### Fixed

- Theme switches now also reach the chrome that is built once and kept: the Analysis View Diagnostic tab (title, Auto box, Refresh button, camera-health table), its metadata-header divider, the dashboard loading label, the annotation dots on the playback slider, and the Detail View Region controls (Draw Region / Whole Frame / Clear / Finish) no longer keep the theme they were built with.
- Theme switches now re-draw the icons that are rendered from the theme when they are asked for: the System Configuration sidebar rows, its Save / Detect Server / Refresh / Clear Logs buttons, the network-summary header glyphs and the camera-card expand, delete and device-settings glyphs kept the previous theme's tint until the app was restarted. The System Configuration tab label and the "Show Permanent Storage" arrow now follow the theme too.
- Switching the theme now reaches every panel that is kept open for the session: the System Configuration dialog and its camera cards, the network summary, the machine-layout header, the IP list and machine-group panels, the OPC UA live-status window, the Help dialogs, and the Analysis View spinner, marker ink and track chip previously kept the theme they were built with until restart.
- The remaining hardcoded colors now follow the theme: camera-card hover washes (they stayed default cyan/red on every theme), disabled-camera rows in the diagnostics table, machine-layout lane fills and its hollow-marker warning, the machine-layout reference guide line, the administrator login eye icon, and the System Configuration tab label.
- Spin-box steppers and the Analysis View playback and marker glyphs are re-rendered in the theme's ink instead of the fixed grey they shipped with.
- Theme changes now reach the network summary header, camera cards, status-bar badges and the Analysis event dashboard, which previously stayed hardcoded dark/cyan regardless of the selected theme.
- Status text and badges meet stronger contrast targets (the previous critical red measured well below the readability bar on the default theme).
- The low-disk status-bar badge keeps a visible keyboard focus ring instead of erasing it.
- Detail View: the camera parameter column scrolls on short windows (e.g. 1366×768) instead of squeezing the spin boxes until their values render blank.
- Analysis View: the Enable Event Deletion switch announces its label to screen readers and draws a visible keyboard-focus ring.
- Analysis and Machine Layout: the event dashboard and the machine schematic expose a plain-text summary (frames, defect hits, marks by severity, largest spot areas) to screen readers instead of reading as silent painted surfaces.
- OPC UA speed grid: two enabled anchors at the same position now block the save (the speed interpolated between them would be ambiguous), out-of-order rows get an inline note under the grid, and the Unit column is an editable combo that shows its m/min default instead of a blank field that silently saved one.
- The Light theme keeps its ink readable: secondary text, dividers, disabled labels and the sidebar's muted states now come from theme tokens instead of being derived by lightening the primary ink (which turned them white on a light background), and the semantic status colours have per-theme variants so alarm, health and information text still reads on light chrome.
- Ink that is painted onto video (camera-tile overlays and the PTP badge, the playback HUD and sync badge, the region rubber band, the event and camera-key banners, the TOOLS/IMAGE/TRACKS handles, the machine-layout and event-dashboard palettes) no longer follows the theme: it follows the surface it is drawn on, which stays dark or light per the card/playback style.
- Re-theming now also reaches chrome that was built once and kept: the Analysis View transport dividers, the TOOLS/IMAGE panel titles, captions, section labels and dividers, the marker-shape icons, the ROI status line, the temperature and frame-rate readings, the network summary status line and the machine-layout event marks; previously they kept the theme they were created under.
- The toggle switch's focus ring and thumb, the vertical slider grip, the deleted-row selection ink and the "N/A" placeholder cells no longer rely on a fixed brightness (they were unreadable or invisible on the light theme), and the playback HUD bar now matches the polarity of the text it carries (it painted dark text on a black bar with a light playback surface).
- SVG assets that draw with a stroke instead of a fill (Jump to Trigger, the marker shapes, the TOOLS panel glyph) are re-coloured with the theme like the rest of the glyphs.

## [1.2.0] - 2026-10-14

### Added

- Native Ubuntu 22.04 package (.deb) published on GitHub Releases: `sudo apt install ./papervision_<version>_amd64.deb` installs, the same command with a newer file upgrades in place, `sudo apt remove papervision` uninstalls (settings and recordings are kept). Ubuntu 20.04 keeps using the Docker image.
- The event list now adopts existing recordings whose stored paths point at a previous storage root (e.g. after moving from the Docker `/app/data` volume to a native install).

### Changed

- Configuration and event storage paths resolve per environment now: `PAPERVISION_CONFIG_DIR` / `PAPERVISION_DATA_DIR` overrides, `/app/data` inside Docker, XDG directories (`~/.config`, `~/.local/share`) on a native install; camera parameter files moved next to the recordings.
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
