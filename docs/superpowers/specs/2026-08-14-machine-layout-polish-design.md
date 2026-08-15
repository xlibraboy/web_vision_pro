# Machine Layout Visual Polish — Design Spec

**Date:** 2026-08-14

## Goal

Make the existing `MachineLayoutPanel` an **at-a-glance 2D schematic of the paper
machine** so engineers can answer two questions in one look:

1. Where on the machine is each camera physically mounted (mm position, side, floor)?
2. What defects have been marked on this machine, and where did they occur?

The current panel already paints per-floor camera lanes plus a defect strip on a
shared mm axis — that architecture is correct and stays. This round replaces
the timeline-only look with a recognisable **machine schematic** (section bar,
paper web, floor lanes, defect strip) and adds a small click-to-highlight
interaction. No new data, no new backend signals, no behavior change beyond the
new highlight.

## Scope

**In scope — touched files:**

- `src/gui/widgets/MachineLayoutPanel.h`
- `src/gui/widgets/MachineLayoutPanel.cpp`

**Out of scope — explicitly not touched:**

- `src/gui/CameraInfo.h` — `CameraFloor` and `CameraGroup` enums already exist
- `src/config/CameraConfig.cpp` — floor / group / position persistence already works
- `src/gui/widgets/CameraCard.cpp` — Floor field already wired
- `src/gui/ConfigDialog.{h,cpp}` — sidebar entry already wired
- `src/gui/widgets/MachineGroupsPanel.{h,cpp}` — separate read-only registry,
  unchanged
- `AnalysisView`, `OpcUaClientService`, `EventDatabase`, `EventController` —
  defect projection math already correct

## Visual Design

The Machine Layout panel is rendered top-to-bottom in this order:

1. **Header row** (existing): `DEFECTS FROM EVENT:` label + combo box on the
   left, optional empty stretch on the right.
2. **Section bar** (new): a single horizontal colored bar across the full
   content width, divided into 4 segments by group: Press-Part, Pre-Dryer,
   After-Dryer, Calender-Reel. **Adaptive widths** — each segment's width is
   proportional to the mm range covered by its assigned cameras. Sections
   with zero cameras collapse to a 2 px divider with the group name shown
   on hover only.
3. **Paper web** (new): a thin horizontal strip below the section bar,
   drawn in a paper-tan fill (`#F5F1E6`) with a `→ DRIVE → OPERATOR →`
   direction arrow at the right edge.
4. **Three floor lanes** (refined): one horizontal lane per floor
   (1st / 2nd / 3rd), stacked vertically. Each lane is **vertically
   split into two sub-rows** so DRIVE SIDE and OPERATOR SIDE are
   unmistakable at a glance:
   - The upper sub-row (top half of the lane) holds **OPERATOR SIDE**
     cameras, with a subtle background tint `#2A3239`.
   - The lower sub-row (bottom half of the lane) holds **DRIVE SIDE**
     cameras, with a subtle background tint `#1F2429` (one shade
     lighter than the canvas surface, so the split is visible without
     stealing focus).
   - Left-edge labels read `1ST · OPERATOR` and `1ST · DRIVE` (one
     label per sub-row) — the side is obvious without relying on
     marker shape alone.
   - The horizontal mm-axis line is drawn once per floor, centered
     between the two sub-rows, with tick marks every `step` mm.
   - Tick labels are shown only on the 3rd-floor lane (bottom) to
     avoid duplication; the defects strip inherits them implicitly.
   - Camera markers are still placed at their `mm` x position, but
     their y position is now in the corresponding sub-row
     (DRIVE → lower, OPERATOR → upper). The shape encoding
     (rounded vs triangle) is kept as a redundant cue.
5. **Defects strip** (kept): a thin strip below the 3rd-floor lane,
   sharing the same mm axis. Defects project as colored diamonds,
   colored per-event.
6. **Legends** (new): below the canvas, three small inline legends:
   - **Camera groups** — the 4 group colors with names.
   - **Side split** — single inline line `▭ DRIVE SIDE  ▬  △ OPERATOR
     SIDE` shown so the per-lane split is reinforced in the legend.
7. **Summary line** (kept): a single muted-text line counting cameras
   per floor and defects shown.

### Camera marker rendering (kept, refined)

- Marker fill = group color (e.g. Press-Part → `#FF9900`).
- Marker **shape** encodes side: rounded rect = DRIVE SIDE, triangle
  (apex up) = OPERATOR SIDE. Same as today.
- Marker border = 1.5 px white (existing) plus a 1 px outer `#30363D`
  outline for contrast against light backgrounds.
- Vertical label `CAM-NN` rendered above the marker (bottom-to-top
  text rotation) so dense lanes don't overlap horizontally.
- Subtle vertical guide line drawn from the marker straight up to the
  section bar, in the same color as the marker but at 30% alpha, so
  users can trace a camera back to its section without hunting.
- Markers without a machine position (mm == 0) stack vertically on the
  left edge with a hollow fill at 35% alpha — already handled today.

### Color tokens (all project tokens, no new tokens)

| Token            | Value     | Used for                            |
|------------------|-----------|-------------------------------------|
| Surface          | `#24292E` | canvas background                   |
| Border           | `#30363D` | axis lines, dividers, marker outline|
| Text / Muted     | `#E3E3E3` / `#8B949E` | floor labels, ticks, summary |
| Accent           | `#00E5FF` | focus, hover highlight, direction arrow |
| Paper            | `#F5F1E6` | paper web strip                     |
| Group Press      | `#FF9900` | Press-Part segment + markers        |
| Group Pre-Dryer  | `#0A84FF` | Pre-Dryer segment + markers         |
| Group Calender   | `#B233FF` | Calender-Reel segment + markers     |
| Side: Drive tint | `#1F2429` | lower sub-row of every floor lane   |
| Side: Op tint    | `#2A3239` | upper sub-row of every floor lane   |
| Cursor           | `#00E5FF` | draggable vertical position line    |

### Anti-patterns we are NOT introducing

- No 3D isometric or perspective transforms (variant C was rejected).
- No hue-only encoding — marker shape carries side information too.
- No decorative-only gradients or animations.
- No emoji as icons (the project rule).
- No new public API, signals, or persisted config keys. Canvas state
  grows by `cursorMm_` / `cursorVisible_` / `cursorDragging_` for the
  draggable position cursor — strictly transient per-session state on
  the inner Canvas widget.

## Interaction Model

### Existing behavior (kept)

- Hover a camera marker → tooltip with `Camera N — name / Floor / Side /
  Group / IP / Machine position`.
- Hover a defect → tooltip with event time, camera, marked frame, aligned
  master frame, projected mm.
- Change `DEFECTS FROM EVENT` combo → defect strip updates immediately.

### New: click-to-highlight

- **Click a camera marker** → that camera becomes the *selected* camera.
  - All other cameras render at 35% alpha (group color, dimmed).
  - All defects on the same event render at 35% alpha.
  - The selected camera's tooltip pins open until selection is cleared.
- **Click a defect diamond** → that defect becomes the *selected* defect.
  - All other defects render at 35% alpha.
  - All cameras that contributed a mark for that event render at 35% alpha.
  - The defect's tooltip pins open until selection is cleared.
- **Click anywhere else on the canvas** → clears the selection.
- **Press <kbd>Esc</kbd>** → clears the selection.
- **Change the defects-from-event combo** → clears the selection (avoids
  carrying state into a different event).
- Only one selected item at a time (camera OR defect, whichever was last
  clicked).

### New: draggable vertical position cursor

A vertical cursor line is rendered across the entire canvas (from the
section bar through every floor lane, through the defects strip, and
into the summary area) at the current cursor mm position.

- **Hover anywhere on the canvas** → cursor line appears at the
  pointer's x, snapped to the nearest `step` mm. A small floating
  pill above the section bar reads e.g. `12 450 mm` and shows which
  section(s) the line currently passes through (`Pre-Dryer`).
- **Press-and-drag** → the line follows the pointer continuously; the
  pill updates on every move. The cursor is a tool for reading the
  exact mm of a position relative to cameras and defects, not a
  selection tool — clicking on a marker still triggers
  click-to-highlight as before.
- **Cursor style:** 1 px solid `#00E5FF` (accent) at 75% alpha across
  the canvas; a 6 × 6 px square handle sits on the section bar so the
  user can grab it.
- **Cursor state is per-session** (not persisted). When the user
  changes the defects-from-event combo, the cursor stays where it
  was — only the highlight selection clears.
- **No conflict with click-to-highlight:** clicking a marker selects;
  pressing on the canvas (not on a marker) just moves the cursor to
  that x without selecting anything.

### Why click-to-highlight (not zoom or focus)

A larger power-user feature would add Zoom-to-region, focus a single
floor, toggle visibility, etc. That is a different proposal and was
explicitly out of scope this round. Click-to-highlight and the
position cursor are the smallest useful additions that make the panel
feel like a tool rather than a display.

## Architecture & Data Flow

The existing data pipeline is unchanged. New code lives in:

```
MachineLayoutPanel::MachineLayoutPanel  → builds header + canvas (unchanged)
MachineLayoutPanel::setCameras(...)    → refresh() (unchanged)
MachineLayoutPanel::refresh()           → rebuild + paint (unchanged)
MachineLayoutPanel::rebuildData()       → collect cameras + defects (unchanged)
                                          + NEW: rebuildSectionRanges()
MachineLayoutPanel::applyEventFilter()  → rebuildScale() (unchanged)

NEW:
MachineLayoutPanel::rebuildSectionRanges()  // min/max mm per section
MachineLayoutPanel::Canvas::paintEvent(...)  // split into draw* helpers
  + drawSectionBar(painter, ...)
  + drawPaperWeb(painter, ...)
  + drawFloorLanes(painter, ...)  // per-floor sub-row backgrounds
  + drawCameraMarkers(painter, ...)  // side-aware sub-row placement
  + drawDefectStrip(painter, ...)
  + drawPositionCursor(painter, ...)  // draggable vertical line + pill
  + drawLegends(painter, ...)
  + drawSummary(painter, ...)
MachineLayoutPanel::Canvas::mousePressEvent(QMouseEvent*)
MachineLayoutPanel::Canvas::mouseMoveEvent(QMouseEvent*)  // cursor drag
MachineLayoutPanel::Canvas::mouseReleaseEvent(QMouseEvent*)
MachineLayoutPanel::Canvas::keyPressEvent(QKeyEvent*)
MachineLayoutPanel::Canvas::leaveEvent(QEvent*)
MachineLayoutPanel::Canvas::paintEvent(QPaintEvent*)

NEW state on Canvas:
  int selectedCamera_ = -1;       // -1 = none
  int selectedDefect_ = -1;       // -1 = none
  bool cursorVisible_ = false;    // pointer is over the canvas
  bool cursorDragging_ = false;   // user is holding LMB on canvas
  double cursorMm_ = 0.0;         // current cursor position in mm

### Section-bar widths

`rebuildSectionRanges()` walks the camera list and computes
`[minMm, maxMm]` per group (excluding cameras without a position). The
`bar divides the full content width by intersecting the four ranges and
emitting each segment in paint order. A section with zero cameras
contributes a 2 px divider with the group name as a hover tooltip on
that divider strip (no always-on text in the bar itself when width is
narrow).


### Highlight pass in paint

Both `drawCameraMarkers` and `drawDefectStrip` accept the selected index
and apply an alpha-0.35 overlay to all non-selected items. Selected item
is drawn last with full alpha and a 1 px accent border (`#00E5FF`) for
visibility against the same-color surroundings.

### Tooltips during selection

`mouseMoveEvent` continues to update hover tooltips for whichever marker
the mouse is over. The pinned selection-tooltip is implemented by
calling `QToolTip::showText()` once per selection click, anchored at the
marker in screen coordinates.

### mm-to-x conversion (kept)

`MachineLayoutPanel::mmToX(double)` already handles the shared mm axis
across cameras and defects. `drawSectionBar` and `drawPaperWeb` use a
slightly different helper `mmSectionToX(double)` because the section bar
is divided differently — but they share the same `minMm_`/`maxMm_` range
so visual continuity is preserved.

### Defect projection (kept)

`loadDefects()` already reads sidecar JSON for each event and projects
defects onto mm via `framesPerMm` and direction sign. No change.

## Verification

1. **Build:** `docker-rebuild-app.sh` (per project convention).
2. **Smoke test in Docker container:** open the running app, navigate
   to Config → Machine Layout.
3. **Visual checks** with the existing fixture data (8 cameras across
   3 floors spanning all 4 sections):
   - [ ] Section bar segments have non-uniform widths proportional to
         the camera mm range covered in each section.
   - [ ] A section with zero cameras collapses to a thin divider with
         its name in a hover tooltip only.
   - [ ] Paper web is rendered as a thin tan strip under the section
         bar with a direction arrow.
   - [ ] Three floor lanes stacked vertically with consistent
         mm-tick spacing shared across all lanes.
   - [ ] Each floor lane is visually split into an OPERATOR sub-row
         (top, `#2A3239`) and a DRIVE sub-row (bottom, `#1F2429`),
         with a per-sub-row side label on the left edge.
   - [ ] Cameras are placed in the correct sub-row of their floor
         lane (DRIVE → bottom, OPERATOR → top).
   - [ ] Camera markers render at their correct mm position.
   - [ ] Marker shape encodes side (rounded = DRIVE, triangle = OPERATOR).
   - [ ] Marker fill color encodes group (Press / Pre-Dryer /
         After-Dryer / Calender-Reel).
   - [ ] A faint vertical guide line connects each camera marker up
         to the section bar in the same color, 30% alpha.
   - [ ] Defect strip below the 3rd floor lane renders diamonds at the
         correct mm position, aligned to the camera mm axis.
   - [ ] Hovering the canvas shows a 1 px `#00E5FF` vertical cursor
         line with a small `12 450 mm` pill above the section bar.
4. **Interaction checks:**
   - [ ] Hover a camera → tooltip appears with all 6 fields.
   - [ ] Hover a defect → tooltip appears with event, camera, frame, mm.
   - [ ] Click a camera → other cameras dim; tooltip pins.
   - [ ] Click a defect → other defects dim; tooltip pins.
   - [ ] Click empty canvas → selection clears.
   - [ ] Press Esc → selection clears.
   - [ ] Change defects-from-event combo → selection clears (cursor
         stays in place).
   - [ ] Hover the canvas → cursor line appears, snapped to step mm.
   - [ ] Press and drag the cursor → line follows the pointer, pill
         updates continuously with the current mm value.
   - [ ] Cursor pill shows the section(s) under the line
         (e.g. `12 450 mm · Pre-Dryer`).
5. **Regression checks** (unchanged behavior):
   - [ ] `MachineGroupsPanel` still shows the same camera groupings.
   - [ ] `AnalysisView` triggering and marking still work.
   - [ ] OPC UA triggers still resolve group → cameras correctly.
   - [ ] Saving a Camera Card updates `floor` and the layout repaints
         on the next `showEvent` / refresh.
6. **Build hygiene:**
   - [ ] `cmake --build build` exits 0.
   - [ ] No new warnings in compiler output that weren't there before.

## Risks & Mitigations

| Risk                                                | Mitigation                                                                                  |
|-----------------------------------------------------|---------------------------------------------------------------------------------------------|
| Section bar with all 4 sections on equal mm range  | Falls back to equal-width segments (no adaptive weighting).                                 |
| A section with all-unassigned cameras               | Section bar still shows that segment with the group color and a "(no cameras)" tooltip.    |
| Camera markers colliding at dense positions         | Stacking on the left edge at 0 mm position (already handled today).                        |
| Defects from many events rendering on top of each other | Already colored per event + tooltip on hover identifies them; click-to-highlight isolates one. |
| Adaptive bar widths on very narrow canvases         | Minimum segment width 36 px; below that the label moves to a tooltip only.                  |
| Cursor collides with click-to-highlight              | Click on a marker → highlight. Click on empty canvas → just move cursor, no selection.     |
| Cursor pill clipped on narrow canvas                | Pill anchored above the section bar with `Qt::AlignHCenter`; right-clipped instead of overflowing. |

## What we explicitly are NOT doing (this round)

- No zoom-to-region, no focus-a-floor, no per-section toggle. (Variant B
  trade-offs; could be a follow-up.)
- No 3D / isometric rendering. (Variant C trade-offs; rejected.)
- No card-style per-camera detail rail. (Variant B; rejected.)
- No new dashboard widgets, no new tabs, no new config keys.
- No changes to the Camera Card, AnalysisView, OPC UA, or persistence.
