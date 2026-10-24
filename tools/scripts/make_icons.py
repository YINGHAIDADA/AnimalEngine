#!/usr/bin/env python3
"""Writes the application icons (placeholder pixel art): assets/package/AnimalGame.icns (macOS),
AnimalGame.ico (Windows) and icon.png. Deterministic; pure standard library.

    python3 tools/scripts/make_icons.py
"""

import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from make_placeholder_art import CREATURE_FRAMES, PALETTE, write_png  # noqa: E402

OUT = pathlib.Path(__file__).resolve().parents[2] / "assets" / "package"
GRID = 16  # icon pixels; the creature's 8×8 idle frame sits in the middle at twice its size
BACKGROUND = (24, 20, 42, 255)
GLOW = (52, 44, 92, 255)


def icon_pixels():
    rows = [[BACKGROUND] * GRID for _ in range(GRID)]
    centre = (GRID - 1) / 2.0
    for y in range(GRID):
        for x in range(GRID):
            if (x - centre) ** 2 + (y - centre) ** 2 < 6.2 ** 2:
                rows[y][x] = GLOW
    # Rounded corners.
    for (x, y) in [(0, 0), (1, 0), (0, 1), (GRID - 1, 0), (GRID - 2, 0), (GRID - 1, 1), (0, GRID - 1), (1, GRID - 1),
                   (0, GRID - 2), (GRID - 1, GRID - 1), (GRID - 2, GRID - 1), (GRID - 1, GRID - 2)]:
        rows[y][x] = (0, 0, 0, 0)
    frame = CREATURE_FRAMES[0]
    for y, line in enumerate(frame):
        for x, ch in enumerate(line):
            if ch != ".":
                rows[4 + y][4 + x] = PALETTE[ch][0]
    return rows


def scaled_png(size, path):
    pixels = icon_pixels()
    step = size // GRID
    data = []
    for y in range(size):
        row = []
        for x in range(size):
            row.extend(pixels[min(y // step, GRID - 1)][min(x // step, GRID - 1)])
        data.append(row)
    write_png(path, size, size, data)
    return path.read_bytes()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    tmp = OUT / "_tmp.png"
    pngs = {size: scaled_png(size, tmp) for size in (16, 32, 48, 64, 128, 256, 512, 1024)}
    tmp.unlink()
    (OUT / "icon.png").write_bytes(pngs[512])

    # .icns: PNG entries by type code.
    entries = b""
    for code, size in ((b"icp4", 16), (b"icp5", 32), (b"icp6", 64), (b"ic07", 128), (b"ic08", 256), (b"ic09", 512),
                       (b"ic10", 1024)):
        entries += code + struct.pack(">I", 8 + len(pngs[size])) + pngs[size]
    (OUT / "AnimalGame.icns").write_bytes(b"icns" + struct.pack(">I", 8 + len(entries)) + entries)

    # .ico: PNG-compressed images (Windows Vista and later).
    sizes = (16, 32, 48, 256)
    header = struct.pack("<HHH", 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    directory = b""
    images = b""
    for size in sizes:
        png = pngs[size]
        directory += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(png), offset + len(images))
        images += png
    (OUT / "AnimalGame.ico").write_bytes(header + directory + images)
    print(f"wrote icons to {OUT}")


if __name__ == "__main__":
    main()
