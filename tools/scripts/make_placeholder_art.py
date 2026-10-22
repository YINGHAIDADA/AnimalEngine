#!/usr/bin/env python3
"""Generates the placeholder sprite sheets in assets/source/textures/.

Each sheet is written as a material (docs/ART_PIPELINE.md):
  <name>.png    albedo
  <name>_n.png  normal map, derived from per-color heights
  <name>_e.png  emissive map
Pure standard library (zlib + struct). Re-run after editing; the PNGs are committed.
"""

import math
import pathlib
import struct
import zlib

OUT_DIR = pathlib.Path(__file__).resolve().parents[2] / "assets" / "source" / "textures"
TILE = 8
NORMAL_STRENGTH = 2.5

# char: (albedo RGBA, height 0..1, emissive RGB or None)
PALETTE = {
    ".": ((0, 0, 0, 0), 0.0, None),
    "k": ((24, 20, 37, 255), 0.5, None),      # outline
    "w": ((236, 232, 218, 255), 1.0, None),   # creature body
    "p": ((232, 124, 146, 255), 1.0, None),   # cheeks
    "m": ((52, 36, 44, 255), 0.0, None),      # mortar
    "b": ((110, 66, 56, 255), 0.9, None),     # brick
    "B": ((150, 92, 72, 255), 1.0, None),     # brick highlight
    "g": ((76, 150, 70, 255), 0.85, None),    # grass
    "G": ((136, 198, 92, 255), 1.0, None),    # grass highlight
    "d": ((104, 72, 50, 255), 0.6, None),     # dirt
    "D": ((78, 52, 38, 255), 0.4, None),      # dirt dark
    "s": ((38, 42, 62, 255), 0.6, None),      # background stone
    "S": ((30, 32, 48, 255), 0.15, None),     # background seams
    "c": ((70, 170, 200, 255), 0.8, (30, 110, 150)),    # crystal
    "C": ((180, 245, 255, 255), 1.0, (140, 230, 255)),  # crystal core
    "f": ((255, 170, 60, 255), 1.0, (255, 130, 30)),    # flame
    "F": ((255, 240, 180, 255), 1.0, (255, 225, 160)),  # flame core
    "v": ((230, 215, 180, 255), 0.85, None),  # wax
    "V": ((196, 176, 146, 255), 0.7, None),   # wax shade
    "h": ((70, 64, 80, 255), 0.5, None),      # candle holder
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


# --- Tiles ---------------------------------------------------------------------------

def wall_tile():
    rows = []
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
        rows.append(line)
    return rows


def ground_tile():
    rows = []
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
        rows.append(line)
    return rows


def background_tile():
    rows = []
    for y in range(TILE):
        seam_x = 7 if y < 4 else 3
        rows.append("".join("S" if y % 4 == 3 or x == seam_x else "s" for x in range(TILE)))
    return rows


def overlay(base, top):
    return ["".join(t if t != "." else b for b, t in zip(brow, trow)) for brow, trow in zip(base, top)]


CRYSTAL = [
    "........",
    "....C...",
    "...cC...",
    ".C.cCc..",
    ".CccCc..",
    "cCc.cCc.",
    "cCcccCcC",
    "ccCcccCc",
]

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

CANDLE = [
    "...F....",
    "..fFf...",
    "...f....",
    "..vvv...",
    "..vvv...",
    "..vVv...",
    ".hhhhh..",
    "..hhh...",
]


# --- Material generation -----------------------------------------------------------------

def heights_from_palette(rows):
    return [[PALETTE[ch][1] for ch in row] for row in rows]


def dome_heights(rows):
    """Height grows with the distance to the nearest transparent pixel (rounded silhouette)."""
    h, w = len(rows), len(rows[0])
    dist = [[0 if rows[y][x] == "." else 99 for x in range(w)] for y in range(h)]
    changed = True
    while changed:
        changed = False
        for y in range(h):
            for x in range(w):
                if dist[y][x] == 0:
                    continue
                best = min(
                    dist[ny][nx] if 0 <= nx < w and 0 <= ny < h else 0
                    for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1))
                ) + 1
                if best < dist[y][x]:
                    dist[y][x] = best
                    changed = True
    return [[min(d, 3) / 3.0 for d in row] for row in dist]


def normal_rows(rows, heights, wrap):
    """+x right, +y down, +z out of the screen; encoded as n * 0.5 + 0.5."""
    h, w = len(rows), len(rows[0])

    def sample(x, y):
        if wrap:
            return heights[y % h][x % w]
        if 0 <= x < w and 0 <= y < h:
            return heights[y][x]
        return 0.0

    out = []
    for y in range(h):
        row = []
        for x in range(w):
            alpha = PALETTE[rows[y][x]][0][3]
            dx = (sample(x + 1, y) - sample(x - 1, y)) * 0.5
            dy = (sample(x, y + 1) - sample(x, y - 1)) * 0.5
            nx, ny, nz = -dx * NORMAL_STRENGTH, -dy * NORMAL_STRENGTH, 1.0
            length = math.sqrt(nx * nx + ny * ny + nz * nz)
            row.extend(round((c / length * 0.5 + 0.5) * 255) for c in (nx, ny, nz))
            row.append(alpha)
        out.append(row)
    return out


def albedo_rows(rows):
    return [[c for ch in row for c in PALETTE[ch][0]] for row in rows]


def emissive_rows(rows):
    out = []
    for row in rows:
        line = []
        for ch in row:
            emission = PALETTE[ch][2]
            line.extend(emission if emission else (0, 0, 0))
            line.append(255)
        out.append(line)
    return out


def write_material(name, cells, height_fn, wrap):
    """Writes a horizontal strip of TILE×TILE cells as albedo / normal / emissive PNGs."""
    for cell in cells:
        assert len(cell) == TILE and all(len(row) == TILE for row in cell), name
    width = TILE * len(cells)
    albedo = [[] for _ in range(TILE)]
    normal = [[] for _ in range(TILE)]
    emissive = [[] for _ in range(TILE)]
    has_emission = False
    for cell in cells:
        for y, (a, n, e) in enumerate(zip(albedo_rows(cell), normal_rows(cell, height_fn(cell), wrap),
                                          emissive_rows(cell))):
            albedo[y] += a
            normal[y] += n
            emissive[y] += e
        has_emission = has_emission or any(PALETTE[ch][2] for row in cell for ch in row)
    write_png(OUT_DIR / f"{name}.png", width, TILE, albedo)
    write_png(OUT_DIR / f"{name}_n.png", width, TILE, normal)
    if has_emission:
        write_png(OUT_DIR / f"{name}_e.png", width, TILE, emissive)


def main():
    # Tile ids match the game: 0 empty, 1 wall, 2 grass-topped ground, 3 background, 4 crystal.
    empty = ["." * TILE for _ in range(TILE)]
    background = background_tile()
    tiles = [empty, wall_tile(), ground_tile(), background, overlay(background, CRYSTAL)]
    write_material("tileset", tiles, heights_from_palette, wrap=True)
    write_material("creature", [CREATURE], dome_heights, wrap=False)
    write_material("props", [CANDLE], heights_from_palette, wrap=False)
    print(f"wrote textures to {OUT_DIR}")


if __name__ == "__main__":
    main()
