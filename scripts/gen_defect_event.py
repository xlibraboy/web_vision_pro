#!/usr/bin/env python3
"""Simulates a paper-break-like global brightness step so DefectDetector
(mean-brightness jump > 30) fires on known frames — used to verify the
Camera-tab event dashboard captures defects. Companion to
generate_simulated_event.py (which makes a local blob defect that this
detector intentionally does NOT flag).
"""
"""Simulate an event with a global-brightness defect for dashboard testing.

Scene: paper-like noise at mean ~70. Frames DEFECT_START..DEFECT_END jump to
mean ~160 (simulated flash / web-out) -> DefectDetector (|dMean|>30) must fire
at the two transition frames. A small dark blob near mid-frame is added purely
for visual interest in thumbnails.
"""
import datetime
import json
import os
import math
import random
import struct

import argparse
_ap = argparse.ArgumentParser(description="Generate an event with a global-brightness defect for dashboard/detector testing.")
_ap.add_argument("--outdir", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data"))
_outdir = _ap.parse_args().outdir
OUT_DIR = os.path.abspath(_outdir)
W, H = 780, 580
FPS = 10.0
TOTAL = 150
TRIGGER = 99
DEFECT_START, DEFECT_END = 41, 60   # inclusive
BASE_MEAN, DEFECT_MEAN = 70, 160
TS = datetime.datetime.now().strftime("%Y%m%d_%H%M%S") + "_001"

def build_header():
    hdr = bytearray(1024)
    hdr[0:4] = b"PAPR"
    struct.pack_into("<IIII", hdr, 4, 1, W, H, 0)
    struct.pack_into("<d", hdr, 24, FPS)
    struct.pack_into("<II", hdr, 32, TOTAL, TRIGGER)
    return bytes(hdr)

_BASE_TEX = None
def _base_texture():
    """Smooth, temporally stable scene: low-frequency shading + fine noise.

    Real webs have smooth brightness fields; sharp local features are then
    genuine anomalies, which is what the SPOTS % lane measures.
    """
    global _BASE_TEX
    if _BASE_TEX is None:
        rnd = random.Random(1234)
        buf = bytearray(W * H)
        phase_x = rnd.uniform(0, 6.28)
        phase_y = rnd.uniform(0, 6.28)
        for y in range(H):
            shade_y = 15.0 * math.sin(y * 0.045 + phase_y)
            for x in range(W):
                v = (BASE_MEAN
                     + 14.0 * math.sin(x * 0.037 + phase_x)
                     + shade_y
                     + rnd.uniform(-6, 6))
                buf[y * W + x] = max(0, min(255, int(v)))
        _BASE_TEX = buf
    return _BASE_TEX

def render_frame(idx):
    frame = bytearray(_base_texture())
    # Bright plateau: global flash (brightness rule; also a full-frame local change).
    if DEFECT_START <= idx <= DEFECT_END:
        for i in range(W * H):
            v = frame[i] + (DEFECT_MEAN - BASE_MEAN)
            frame[i] = 255 if v > 255 else v
    # Flat segment: frames 110-119 uniform fill (stddev ~0 -> low-contrast rule).
    if 110 <= idx <= 119:
        return bytes([90]) * (W * H)
    # Local anomaly: bright rectangle on frames 70-72 (>1% of pixels changed
    # vs baseline) -> exercises the dashboard's local-anomaly rule.
    if 70 <= idx <= 72:
        for yy in range(200, 261):
            row = yy * W
            frame[row + 100: row + 300] = bytes([230]) * 200
    # Web-riding dark hole: passes the FOV every 3rd frame (absent from
    # frame 0 so the baseline is clean). Strong local contrast -> SPOTS rule.
    if idx != 0 and idx % 3 == 0:
        cx, cy, rx, ry = W // 2, H // 2, 14, 8
        for dy in range(-ry, ry + 1):
            for dx in range(-rx, rx + 1):
                if dx * dx / (rx * rx) + dy * dy / (ry * ry) <= 1.0:
                    xx, yy = cx + dx, cy + dy
                    j = yy * W + xx
                    if 0 <= j < len(frame):
                        frame[j] = 15
    return bytes(frame)

def main():
    BIN_NAME = f"event_{TS}_cam1.bin"
    bin_path = os.path.join(OUT_DIR, BIN_NAME)
    t0_ns = int(datetime.datetime.now().timestamp() * 1e9)
    step_ns = int(1e9 / FPS)
    with open(bin_path, "wb") as out:
        out.write(build_header())
        for i in range(TOTAL):
            out.write(render_frame(i))
            flags = 1 if i == TRIGGER else 0
            out.write(struct.pack("<QQI", t0_ns + i * step_ns, i, flags) + b"\x00" * 44)

    meta = {
        "cameraLabels": ["CAM-01: SIM-DEFECT"],
        "cameraPositionsMm": [16600],
        "fps": FPS,
        "height": H,
        "permanent": False,
        "positionDirectionSign": 1,
        "speedAnchors": [],
        "speedSampleTimeUtc": "",
        "speedStale": False,
        "speedTagName": "",
        "speedTagNodeId": "",
        "speedUnit": "",
        "timestamp": TS,
        "totalFrames": TOTAL,
        "triggerGroup": -1,
        "triggerIndex": TRIGGER,
        "triggerPositionMm": 0,
        "triggerReason": "Simulated Defect",
        "triggerSource": "simulation",
        "triggerTagName": "",
        "triggerTagNodeId": "",
        "videoPath": "/app/data/" + BIN_NAME,
        "width": W,
    }
    with open(os.path.join(OUT_DIR, f"event_{TS}.json"), "w") as jf:
        json.dump(meta, jf, indent=4)
    print("wrote", bin_path)
    print("metadata ts:", TS)
    print(f"expected brightness hits at {DEFECT_START}/{DEFECT_END + 1}, local-anomaly hits at 70/71/72")

if __name__ == "__main__":
    main()
