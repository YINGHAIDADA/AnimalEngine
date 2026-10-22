#!/usr/bin/env python3
"""Render regression: runs fixed deterministic scenes on every backend and compares the 320x180
final canvas between backends (must be identical) and against the golden images in tests/golden/.

usage: render_regression.py [--binary PATH] [--backends opengl,vulkan] [--only NAME,...] [--update]

  --update   rewrite the golden images from the first backend (after an intended visual change)
Exit code 0 when everything matches and neither the Vulkan validation layers nor the engine's RHI
validation (Debug builds) report errors, 1 otherwise.
Pure standard library; needs a display and a GPU (the game opens a window).
"""

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compare_images import read_png_rgba  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
GOLDEN = ROOT / "tests" / "golden"

# name, frames, extra arguments. Every run adds --deterministic --no-vsync --hide-overlay.
SCENES = [
    ("demo_entrance", 60, ["--demo"]),               # climbing the long vine, skylight, moss
    ("demo_gallery", 260, ["--demo"]),               # lantern shadows, rim light, crystals, flowers
    ("demo_pit", 520, ["--demo"]),                   # smoke, water, waterfall, reeds, ember look
    ("pit_waterfall", 300, ["--start-room=smoke_pit"]),
    ("gallery_idle", 120, ["--start-room=crystal_gallery"]),
    ("stress_lights", 200, ["--demo", "--stress-lights=256"]),
]


def default_binary():
    if platform.system() == "Windows":
        return ROOT / "build" / "windows-ninja-debug" / "bin" / "Debug" / "AnimalGame.exe"
    return ROOT / "build" / "macos-ninja-debug" / "bin" / "Debug" / "AnimalGame"


def run_scene(binary, backend, name, frames, extra, out_dir):
    out = out_dir / f"{name}_{backend}.png"
    command = [str(binary), f"--backend={backend}", "--deterministic", "--no-vsync", "--hide-overlay",
               f"--frames={frames}", f"--canvas-screenshot={out}", *extra]
    result = subprocess.run(command, capture_output=True, text=True, timeout=300)
    log = result.stdout + result.stderr
    errors = 0
    for pattern in (r"Vulkan shutdown: (\d+) validation error", r"RHI validation: (\d+) error"):
        match = re.search(pattern, log)
        if match:
            errors += int(match.group(1))
    if result.returncode != 0 or not out.exists():
        return None, errors, log
    return out, errors, log


def pixels(path):
    width, height, data = read_png_rgba(str(path))
    return (width, height), data


def differing_pixels(a, b):
    (size_a, pa), (size_b, pb) = a, b
    if size_a != size_b:
        return -1
    return sum(1 for i in range(0, len(pa), 4) if pa[i:i + 4] != pb[i:i + 4])


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", type=Path, default=default_binary())
    parser.add_argument("--backends", default="opengl,vulkan")
    parser.add_argument("--only", default="", help="comma-separated scene names")
    parser.add_argument("--update", action="store_true", help="rewrite the golden images")
    args = parser.parse_args()

    if not args.binary.exists():
        print(f"game binary not found: {args.binary} (build it first, or pass --binary)")
        return 1
    backends = [b for b in args.backends.split(",") if b]
    wanted = {n for n in args.only.split(",") if n}
    scenes = [s for s in SCENES if not wanted or s[0] in wanted]
    GOLDEN.mkdir(parents=True, exist_ok=True)

    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        out_dir = Path(tmp)
        for name, frames, extra in scenes:
            captures = {}
            notes = []
            for backend in backends:
                path, errors, log = run_scene(args.binary, backend, name, frames, extra, out_dir)
                if path is None:
                    failures.append(f"{name}/{backend}: the game failed")
                    notes.append(f"{backend} FAILED")
                    sys.stderr.write(log[-2000:])
                    continue
                if errors:
                    failures.append(f"{name}/{backend}: {errors} validation error(s)")
                    notes.append(f"{backend} {errors} validation errors")
                captures[backend] = path

            first = next(iter(captures.values()), None)
            images = {b: pixels(p) for b, p in captures.items()}
            for backend in list(images)[1:]:
                diff = differing_pixels(images[backends[0]], images[backend])
                if diff:
                    failures.append(f"{name}: {backends[0]} vs {backend} differ in {diff} pixels")
                    notes.append(f"{backend} != {backends[0]} ({diff} px)")

            golden = GOLDEN / f"{name}.png"
            if first is not None:
                if args.update:
                    shutil.copyfile(first, golden)
                    notes.append("golden updated")
                elif not golden.exists():
                    failures.append(f"{name}: no golden image (run with --update)")
                    notes.append("no golden")
                else:
                    diff = differing_pixels(pixels(golden), images[next(iter(images))])
                    if diff:
                        keep = ROOT / "screenshots" / "regression"
                        keep.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(first, keep / f"{name}_actual.png")
                        failures.append(f"{name}: differs from the golden image in {diff} pixels "
                                        f"(actual saved to screenshots/regression/)")
                        notes.append(f"golden mismatch ({diff} px)")
            print(f"{name:16} {'OK' if not notes else '; '.join(notes)}")

    if failures:
        print("\nFAILED:")
        for f in failures:
            print(f"  {f}")
        return 1
    print(f"\nAll {len(scenes)} scenes match on {', '.join(backends)}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
