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

def render_frame(idx):
    target = DEFECT_MEAN if DEFECT_START <= idx <= DEFECT_END else BASE_MEAN
    rnd = random.Random(idx)
    frame = bytearray(W * H)
    # coarse blocks keep generation fast; per-pixel jitter adds texture
    for y in range(H):
        base_row = max(0, min(255, target + rnd.randint(-6, 6)))
        row_start = y * W
        x = 0
        while x < W:
            v = max(0, min(255, base_row + rnd.randint(-8, 8)))
            run = rnd.randint(16, 48)
            frame[row_start + x: row_start + min(W, x + run)] = bytes([v]) * min(run, W - x)
            x += run
    # small dark blob (visual only, negligible mean impact)
    if idx % 3 == 0:
        cx, cy, rx, ry = W // 2, H // 2, 12, 6
        for dy in range(-ry, ry + 1):
            for dx in range(-rx, rx + 1):
                if dx * dx / (rx * rx) + dy * dy / (ry * ry) <= 1.0:
                    xx, yy = cx + dx, cy + dy
                    j = yy * W + xx
                    if 0 <= j < len(frame):
                        frame[j] = 40
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
    print(f"expected detector hits at frames {DEFECT_START} and {DEFECT_END + 1}")

if __name__ == "__main__":
    main()
