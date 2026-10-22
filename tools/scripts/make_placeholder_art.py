#!/usr/bin/env python3
"""Generates the placeholder textures in assets/source/textures/.

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


TILE = 8


def wall_tile():
    tile = []
    for y in range(TILE):
        line = ""
        for x in range(TILE):
            seam_x = 0 if y < 4 else 4
            if y % 4 == 3 or x == seam_x:
                line += "m"
            elif y % 4 == 0:
                line += "B"
            else:
                line += "b"
        tile.append(line)
    return tile


def ground_tile():
    tile = []
    for y in range(TILE):
        line = ""
        for x in range(TILE):
            if y == 0:
                line += "G" if x % 3 != 1 else "g"
            elif y == 1:
                line += "g"
            elif y == 2:
                line += "g" if x % 3 == 0 else "d"
            else:
                line += "D" if (x * 5 + y * 3) % 7 == 0 else "d"
        tile.append(line)
    return tile


def background_tile():
    tile = []
    for y in range(TILE):
        line = ""
        for x in range(TILE):
            seam_x = 7 if y < 4 else 3
            line += "S" if y % 4 == 3 or x == seam_x else "s"
        tile.append(line)
    return tile


def empty_tile():
    return ["." * TILE for _ in range(TILE)]


# Faces right; the game mirrors it when moving left.
CREATURE = [
    ".k....k.",
    "kwk..kwk",
    "kwwkkwwk",
    "kwwwkwkk",
    "kwwwwwwk",
    "kwpwwwpk",
    ".kwwwwk.",
    ".kk..kk.",
]


def main():
    # Tile ids match the game: 0 empty, 1 wall, 2 grass-topped ground, 3 background.
    tiles = [empty_tile(), wall_tile(), ground_tile(), background_tile()]
    for tile in tiles:
        assert len(tile) == TILE and all(len(row) == TILE for row in tile)
    tileset_rows = ["".join(tile[y] for tile in tiles) for y in range(TILE)]
    write_png(OUT_DIR / "tileset.png", TILE * len(tiles), TILE, to_rgba_rows(tileset_rows))

    assert len(CREATURE) == TILE and all(len(row) == TILE for row in CREATURE)
    write_png(OUT_DIR / "creature.png", TILE, TILE, to_rgba_rows(CREATURE))
    print(f"wrote textures to {OUT_DIR}")


if __name__ == "__main__":
    main()
