# Machine Reference — Paper Machine (web_vision_pro)

Reference of the physical paper machine used by the vision system: drive groups,
speeds, sheet-break/trigger sensors, and camera positions. All positions are on
one common **machine ruler in millimetres** (mm), measured from a fixed origin
(see §1). This is what allows all cameras to be time- and space-synchronised
when a defect or paper break is detected.

> Fill in the tables below with your plant's actual values. Each field notes
> where it is configured in the app.

---

## 1. Machine Ruler Convention

| Item | Value |
|---|---|
| Origin (0 mm) | <!-- e.g. headbox centreline / wire pit --> |
| Direction of increasing mm | <!-- e.g. paper travel direction (wire -> reel) --> |
| Total machine length | <!-- e.g. ~120000 mm --> |

Every camera (`CameraInfo.machinePosition`), every trigger sensor
(`OpcUaTriggerTagSettings.positionMm`) and every drive speed tag
(`OpcUaSpeedTagSettings.positionMm`) uses this same ruler. Position 0 means
"unknown" — spatial alignment is disabled for that element.

## 2. Drive Groups & Speed Anchors

A paper machine's drive groups run at slightly different speeds (**draw**), so a
single speed value misplaces defects on the mm ruler. Each drive reports its
**actual local speed** (m/min) at its machine position; these are snapshotted as
*speed anchors* onto every event at trigger time. Between anchors the local
speed is interpolated linearly (`src/core/SpeedProfile.h`).

Machine sections (fixed in code, `src/gui/CameraInfo.h` → `CameraGroup`):

| # | Section | Constant |
|---|---|---|
| 0 | Wire (forming, before press) | `kWire` |
| 1 | Press-Part | `kPressPart` |
| 2 | Pre-Dryer | `kPreDryer` |
| 3 | After-Dryer | `kAfterDryer` |
| 4 | Calender-Reel | `kCalenderReel` |

### Actual Drive List (measured at the machine)

All drives in sheet-travel order, with draw (%) and speed (m/min). Fill in the
two right-hand columns when the speed tags are wired up (Config Dialog →
OPC UA → Speed Tags); Scale/Offset default to 1.0 / 0.0, unit m/min.

| # | Drive | Draw | Speed (m/min) | OPC UA Node ID | Position (mm) |
|---|---|---|---|---|---|
| 1 | WIRE TURNING | — | 664.9 | | |
| 2 | COUCH ROLL | — | 664.5 | | |
| 3 | PRESS ROLL | 1.00 % | 670.9 | | |
| 4 | PICKUP ROLL | — | 671.1 | | |
| 5 | 1P BOTTOM | — | 670.9 | | |
| 6 | 2P ROLL | — | 670.4 | | |
| 7 | 3P PAPER ROLL | 0.88 % | 676.5 | | |
| 8 | 4P BOTTOM ROLL | 0.82 % | 676.2 | | |
| 9 | 4P PAPER ROLL | 1.45 % | 687.0 | | |
| 10 | SMOOTHER BOT | 1.63 % | 685.3 | | |
| 11 | SMOOTHER TOP | — | 688.0 | | |
| 12 | 1DT-3 | 0.55 % | 690.7 | | |
| 13 | 1DT-2 | — | 691.0 | | |
| 14 | 1DT-1 | — | 691.5 | | |
| 15 | 2DT | 0.27 % | 692.6 | | |
| 16 | 2DB | 0.19 % | 693.3 | | |
| 17 | 3DT-2 | -0.65 % | 688.0 | | |
| 18 | 3DT-1 | 0.00 % | 688.5 | | |
| 19 | 3DB-2 | 0.02 % | 687.5 | | |
| 20 | 3DB-1 | 0.00 % | 688.8 | | |
| 21 | 3D PAPER ROLL | 0.19 % | 689.0 | | |
| 22 | SIZE TOP ROLL | 0.44 % | 691.6 | | |
| 23 | SIZE BOTTOM ROLL | 0.00 % | 690.6 | | |
| 24 | LEAD-IN PAPER | -0.28 % | 689.1 | | |
| 25 | EXPANDER ROLL | -0.02 % | 691.1 | | |
| 26 | 4DB-1 | -0.60 % | 688.4 | | |
| 27 | 4DT-1 | 0.06 % | 692.7 | | |
| 28 | 4DT-2 | 0.64 % | 693.6 | | |
| 29 | 4DB-2 | -0.38 % | 689.7 | | |
| 30 | 5DT | -0.19 % | 691.9 | | |
| 31 | 5DB | 0.18 % | 691.0 | | |
| 32 | 5D PAPER ROLL | — | 693.2 | | |
| 33 | CALENDER | 0.11 % | 692.0 | | |
| 34 | REEL | 0.09 % | 693.9 | | |
| 35 | SPREADER ROLL | 0.09 % | 693.3 | | |
| 36 | SPOOL STARTER | — | 29.8 | | |

Notes:
- The speeds are one operating snapshot: ~665 m/min at the wire rising to
  ~694 m/min at the reel through cumulative draw. Live values are captured as
  speed anchors per event at trigger time — this table is the reference of
  *which* drives exist and their nominal relationship, not a constant to use.
- SPOOL STARTER (29.8 m/min) runs at reel-spool change speed and is outside
  the normal sheet path.
- Negative draw (e.g. 3DT-2 at -0.65 %) means that drive intentionally lags
  its predecessor.

Suggested mapping to the fixed camera sections (`CameraGroup` in
`src/gui/CameraInfo.h`) — **verify against the actual layout**:

| CameraGroup | Drives |
|---|---|
| Wire (0) | WIRE TURNING, COUCH ROLL |
| Press-Part (1) | PRESS ROLL … SMOOTHER TOP |
| Pre-Dryer (2) | 1DT-3 … 3D PAPER ROLL |
| After-Dryer (3) | SIZE TOP/BOTTOM ROLL, LEAD-IN PAPER, EXPANDER ROLL, 4DB-1 … 5D PAPER ROLL |
| Calender-Reel (4) | CALENDER, REEL, SPREADER ROLL, SPOOL STARTER |

Notes:
- One speed tag per drive gives accurate defect projection across draw zones.
- A tag with position 0 acts as a *global* speed (legacy single-tag behaviour).
- Stale samples (older than `staleTimeoutMs`, default 2000 ms) are rejected.

## 3. Sheet-Break / Trigger Sensors

Triggers start the circular-buffer recording (pre/post-trigger window). A trigger
wired to a section records only that section's cameras; unassigned triggers
record all cameras. Configured in Config Dialog → OPC UA → Trigger Tags.

### Planned Sheet-Break Sensors (per group)

Totals per camera section — **positions not yet measured** (`Position mm` to be
filled in later once the real mounting positions are known). The Wire group has
no sheet-break sensor configured.

| Group | Sensor Count |
|---|---|
| Wire (0) | 0 |
| Press-Part (1) | 1 |
| Pre-Dryer (2) | 3 |
| After-Dryer (3) | 3 |
| Calender-Reel (4) | 2 |
| **Total** | **9** |

Sensor list (Node IDs and positions TBD):

| Sensor / Trigger | Node ID (OPC UA) | Position (mm) | Records Group | Min Interval (ms) | Enabled |
|---|---|---|---|---|---|
| PRESS-PART SB-01 | ns=... <!-- TBD --> | <!-- TBD --> | Press-Part (1) | 1500 | ☐ |
| PRE-DRYER SB-01 | ns=... <!-- TBD --> | <!-- TBD --> | Pre-Dryer (2) | 1500 | ☐ |
| PRE-DRYER SB-02 | ns=... <!-- TBD --> | <!-- TBD --> | Pre-Dryer (2) | 1500 | ☐ |
| PRE-DRYER SB-03 | ns=... <!-- TBD --> | <!-- TBD --> | Pre-Dryer (2) | 1500 | ☐ |
| AFTER-DRYER SB-01 | ns=... <!-- TBD --> | <!-- TBD --> | After-Dryer (3) | 1500 | ☐ |
| AFTER-DRYER SB-02 | ns=... <!-- TBD --> | <!-- TBD --> | After-Dryer (3) | 1500 | ☐ |
| AFTER-DRYER SB-03 | ns=... <!-- TBD --> | <!-- TBD --> | After-Dryer (3) | 1500 | ☐ |
| CALENDER-REEL SB-01 | ns=... <!-- TBD --> | <!-- TBD --> | Calender-Reel (4) | 1500 | ☐ |
| CALENDER-REEL SB-02 | ns=... <!-- TBD --> | <!-- TBD --> | Calender-Reel (4) | 1500 | ☐ |

While a trigger stays active (or the manual button is held), it re-fires every
`minimumIntervalMs`.

## 4. Camera Positions

Cameras carry: machine position (mm), section/group, floor (1st–3rd), and side
(DRIVE/OPERATOR). Configured on each Camera Card in Live View / Machine Layout.

| Cam ID | Name | Section (group) | Floor | Side | Position (mm) | IP |
|---|---|---|---|---|---|---|
| 1 | <!-- e.g. WIRE 1 --> | Wire (0) | 1st | <!-- DRIVE/OPERATOR --> | | 172.20.2.x |
| 2 | | Press-Part (1) | | | | |
| 3 | | Pre-Dryer (2) | | | | |
| 4 | | After-Dryer (3) | | | | |
| 5 | | Calender-Reel (4) | | | | |

## 5. How Sync Works (summary)

1. A trigger fires (sheet-break sensor via OPC UA, or manual push-hold).
2. At trigger time the system snapshots: pre/post window lengths, each
   recording camera's machine position, and **all fresh speed anchors**
   (`EventDatabase::SpeedAnchorSnapshot`).
3. During analysis, every frame is projected onto the mm ruler:
   local speed between anchors is interpolated (`SpeedProfile::speedAt`) and the
   sheet displacement `(P_cam − P_detect) / v_local` places each camera's view
   of the defect on the common ruler.
4. Result: defects/breaks seen by different cameras line up spatially despite
   drive draw and different frame timings.

## 6. Where Values Live in Code

| Concept | Code |
|---|---|
| Speed anchor interpolation | `src/core/SpeedProfile.{h,cpp}` |
| Anchor/event storage | `src/core/EventDatabase.h` (`SpeedAnchorSnapshot`, `cameraPositionsMm`) |
| Trigger & speed acquisition | `src/communication/OpcUaClientService.{h,cpp}` |
| Trigger/speed tag config | `src/config/CameraConfig.h` (`OpcUaTriggerTagSettings`, `OpcUaSpeedTagSettings`, `OpcUaSettings`) |
| Sections, floors, camera metadata | `src/gui/CameraInfo.h` |
