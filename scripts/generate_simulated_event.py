#!/usr/bin/env python3
"""Generate a synthetic multi-camera event for the alignment PoC.

The event shows a paper-like texture with a single dark defect blob whose
machine position advances at a chosen speed. Per-camera pass times follow the
app's alignment model:

    framesPerMm = fps * 60.0 / (speed * 1000.0)
    pass_i = triggerIndex + round((pos_i - pos_1) * framesPerMm)

so pressing Align on the generated event reproduces exactly the offsets the
footage was built with, and all cameras display the defect at the same master
frame and x-position.

Usage:
    python3 scripts/generate_simulated_event.py --speed 120 [--outdir data]

Writes:
    <outdir>/event_<ts>.json
    <outdir>/event_<ts>_cam1.bin
    <outdir>/event_<ts>_cam2.bin   (one per camera position)

RAW layout (see src/core/RawFormat.h):
    header   1024 B  magic "PAPR", version, width, height, pixelFormat(0=Mono8),
                     fps(double), totalFrames, triggerIndex, reserved
    per frame: width*height bytes Mono8 + 64 B FrameMetadata(timestamp ns u64,
               frameId u64, flags u32, reserved 44)
"""

import argparse
import datetime
import math
import os
import struct
import sys


def build_header(width, height, fps, total_frames, trigger_index):
    hdr = bytearray(1024)
    hdr[0:4] = b"PAPR"
    struct.pack_into("<IIII", hdr, 4, 1, width, height, 0)  # version, w, h, Mono8
    struct.pack_into("<d", hdr, 24, fps)
    struct.pack_into("<II", hdr, 32, total_frames, trigger_index)
    return bytes(hdr)


def frame_metadata(timestamp_ns, frame_id, flags):
    return struct.pack("<QQI", timestamp_ns, frame_id, flags) + b"\x00" * 44


def render_frame(width, height, noise, streak_mask, blob_x, blob_y):
    """Mono8 frame: tiled noise + faint streaks + defect blob at (blob_x, blob_y)."""
    frame = bytearray(noise.translate(_NOISE_TABLE))
    # Faint horizontal paper streaks every 7 rows.
    for row in range(0, height, 7):
        start = row * width
        end = start + width
        for i in range(start, end):
            frame[i] = max(0, frame[i] - streak_mask[i - start])
    if blob_x is None:
        return bytes(frame)
    # Dark ellipse with a bright center speckle (easy to mark precisely).
    cx, cy = int(blob_x), int(blob_y)
    rx, ry = 10, 5
    for dy in range(-ry, ry + 1):
        for dx in range(-rx, rx + 1):
            if dx * dx / (rx * rx) + dy * dy / (ry * ry) > 1.0:
                continue
            x, y = cx + dx, cy + dy
            if 0 <= x < width and 0 <= y < height:
                frame[y * width + x] = 48
    # Bright center speckle marks the exact defect position.
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            x, y = cx + dx, cy + dy
            if 0 <= x < width and 0 <= y < height:
                frame[y * width + x] = 240
    return bytes(frame)


_NOISE_TABLE = bytes(190 + i // 13 for i in range(256))  # 190..209


def generate_event(outdir, speed, fps, positions, trigger, frames, width, height,
                   px_per_frame, streak_every):
    now = datetime.datetime.now()
    ts = now.strftime("%Y%m%d_%H%M%S_") + now.strftime("%f")[:3]  # yyyyMMdd_HHmmss_zzz
    while os.path.exists(os.path.join(outdir, f"event_{ts}.json")):
        now = now + datetime.timedelta(milliseconds=1)
        ts = now.strftime("%Y%m%d_%H%M%S_") + now.strftime("%f")[:3]

    frames_per_mm = fps * 60.0 / (speed * 1000.0)
    pass_frames = [trigger + round((pos - positions[0]) * frames_per_mm)
                   for pos in positions]

    base_ns = int(now.timestamp() * 1e9)
    frame_step_ns = int(1e9 / fps)
    cx0 = width / 2.0
    cy = height / 2.0
    streak = bytearray(width)
    for i in range(width):
        streak[i] = 4 + (i * 7) % 5

    for cam_idx, (pos, pass_frame) in enumerate(zip(positions, pass_frames), start=1):
        bin_path = os.path.join(outdir, f"event_{ts}_cam{cam_idx}.bin")
        with open(bin_path, "wb") as f:
            f.write(build_header(width, height, fps, frames, trigger))
            for frame_idx in range(frames):
                noise = os.urandom(width * height)
                x = cx0 + (frame_idx - pass_frame) * px_per_frame
                blob_x = x if -30 <= x <= width + 30 else None
                blob_y = cy + math.sin(frame_idx * 0.23) * 8.0
                frame = render_frame(width, height, noise, streak, blob_x, blob_y)
                f.write(frame)
                f.write(frame_metadata(
                    base_ns + frame_idx * frame_step_ns,
                    frame_idx + 1,
                    1 if frame_idx == trigger else 0))
        print(f"[gen] {bin_path} pass_frame={pass_frame}")

    json_path = os.path.join(outdir, f"event_{ts}.json")
    meta = {
        "timestamp": ts,
        "videoPath": f"/app/data/event_{ts}_cam1.bin",
        "triggerIndex": trigger,
        "totalFrames": frames,
        "fps": fps,
        "width": width,
        "height": height,
        "cameraLabels": [f"SIM CAM {i + 1}" for i in range(len(positions))],
        "cameraPositionsMm": positions,
        "triggerReason": "Simulated",
        "triggerSource": "simulated",
        "triggerTagName": "",
        "triggerTagNodeId": "",
        "speedTagName": "Machine Speed",
        "speedTagNodeId": "",
        "speedValue": float(speed),
        "speedUnit": "m/min",
        "speedSampleTimeUtc": "",
        "speedStale": False,
        "positionDirectionSign": 1,
        "triggerGroup": -1,
        "permanent": False,
    }
    import json
    with open(json_path, "w") as f:
        json.dump(meta, f, indent=4)
    print(f"[gen] {json_path} speed={speed} m/min "
          f"framesPerMm={frames_per_mm:.6f} pass_frames={pass_frames} "
          f"expected_offsets={[p - pass_frames[0] for p in pass_frames]}")
    return ts


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--speed", type=float, required=True, help="Machine speed in m/min")
    ap.add_argument("--fps", type=float, default=10.0)
    ap.add_argument("--positions", default="16600,17200",
                    help="Comma-separated camera positions in mm")
    ap.add_argument("--trigger", type=int, default=99)
    ap.add_argument("--frames", type=int, default=150)
    ap.add_argument("--width", type=int, default=780)
    ap.add_argument("--height", type=int, default=580)
    ap.add_argument("--px-per-frame", type=float, default=3.0,
                    help="Defect horizontal speed in px/frame")
    ap.add_argument("--outdir", default=None)
    args = ap.parse_args()

    outdir = args.outdir or os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data")
    os.makedirs(outdir, exist_ok=True)
    positions = [int(p) for p in args.positions.split(",") if p.strip()]
    if len(positions) < 2:
        print("Need at least two camera positions.", file=sys.stderr)
        sys.exit(1)
    generate_event(outdir, args.speed, args.fps, positions, args.trigger,
                   args.frames, args.width, args.height, args.px_per_frame,
                   streak_every=7)


if __name__ == "__main__":
    main()
