#!/usr/bin/env python3
"""Generates the M0 placeholder textures in assets/source/textures/.

Pure standard library (zlib + struct) so it runs anywhere Python 3 does.
Re-run after editing; the PNGs are committed as source assets.
"""

import pathlib
import struct
import zlib

OUT_DIR = pathlib.Path(__file__).resolve().parents[2] / "assets" / "source" / "textures"

PALETTE = {
    ".": (0, 0, 0, 0),
    "k": (24, 20, 37, 255),     # outline
    "w": (236, 232, 218, 255),  # creature body
    "p": (232, 124, 146, 255),  # cheeks
    "m": (52, 36, 44, 255),     # mortar
    "b": (110, 66, 56, 255),    # brick
    "B": (150, 92, 72, 255),    # brick highlight
    "g": (76, 150, 70, 255),    # grass
    "G": (136, 198, 92, 255),   # grass highlight
    "d": (104, 72, 50, 255),    # dirt
    "D": (78, 52, 38, 255),     # dirt dark
    "s": (38, 42, 62, 255),     # background stone
    "S": (30, 32, 48, 255),     # background stone seams
}


def write_png(path, width, height, rows):
    raw = b"".join(b"\x00" + bytes(row) for row in rows)

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def to_rgba_rows(char_rows):
    rows = []
    for line in char_rows:
        row = []
        for ch in line:
            row.extend(PALETTE[ch])
        rows.append(row)
    return rows


def brick_tile():
    tile = []
    for y in range(16):
        line = ""
        for x in range(16):
            band = y // 4
            seam_x = 0 if band % 2 == 0 else 8
            if y % 4 == 3 or x % 16 == seam_x or (x - seam_x) % 16 == 0:
                line += "m"
            elif y % 4 == 0:
                line += "B"
            else:
                line += "b"
        tile.append(line)
    return tile


def grass_tile():
    tile = []
    for y in range(16):
        line = ""
        for x in range(16):
            if y == 0:
                line += "G" if x % 5 != 2 else "g"
            elif y < 3:
                line += "g"
            elif y == 3:
                line += "g" if x % 4 in (0, 1) else "d"
            else:
                line += "D" if (x * 7 + y * 13) % 11 == 0 else "d"
        tile.append(line)
    return tile


def background_tile():
    tile = []
    for y in range(16):
        line = ""
        for x in range(16):
            seam = y % 8 == 7 or (x + (4 if (y // 8) % 2 else 0)) % 8 == 7
            line += "S" if seam else "s"
        tile.append(line)
    return tile


def empty_tile():
    return ["." * 16 for _ in range(16)]


CREATURE = [
    "................",
    "....kk....kk....",
    "...kwwk..kwwk...",
    "...kwwwkkwwwk...",
    "..kwwwwwwwwwwk..",
    ".kwwwwwwwwwwwwk.",
    ".kwwwkwwwwkwwwk.",
    ".kwwwkwwwwkwwwk.",
    ".kwpwwwwwwwwpwk.",
    ".kwwwwwkkwwwwwk.",
    ".kwwwwwwwwwwwwk.",
    "..kwwwwwwwwwwk..",
    "..kwwwwwwwwwwk..",
    "...kwwkkkkwwk...",
    "...kwk....kwk...",
    "....k......k....",
]


def main():
    tiles = [empty_tile(), brick_tile(), grass_tile(), background_tile()]
    for tile in tiles:
        assert len(tile) == 16 and all(len(row) == 16 for row in tile)
    tileset_rows = ["".join(tile[y] for tile in tiles) for y in range(16)]
    write_png(OUT_DIR / "tileset.png", 16 * len(tiles), 16, to_rgba_rows(tileset_rows))

    assert len(CREATURE) == 16 and all(len(row) == 16 for row in CREATURE)
    write_png(OUT_DIR / "creature.png", 16, 16, to_rgba_rows(CREATURE))
    print(f"wrote textures to {OUT_DIR}")


if __name__ == "__main__":
    main()
