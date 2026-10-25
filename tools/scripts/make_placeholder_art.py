#!/usr/bin/env python3
"""Generates the placeholder sprite sheets in assets/source/textures/.

Each sheet is written as a material (docs/ART_PIPELINE.md):
  <name>.png    albedo
  <name>_n.png  normal map, derived from per-color heights
  <name>_e.png  emissive map
Pure standard library (zlib + struct). Re-run after editing; the PNGs are committed.
"""

import json
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
    "u": ((150, 104, 196, 255), 0.8, (36, 22, 52)),     # beetle shell
    "U": ((214, 176, 240, 255), 1.0, (110, 80, 150)),   # beetle shell highlight (faint glow: readable in the dark)
    "r": ((255, 90, 70, 255), 1.0, (255, 70, 50)),       # beetle eye (glows)
    "e": ((70, 210, 180, 255), 0.8, (40, 170, 140)),     # gem
    "E": ((200, 255, 238, 255), 1.0, (160, 255, 225)),   # gem highlight
    "l": ((92, 76, 62, 255), 0.6, None),      # lantern frame
    "L": ((150, 126, 96, 255), 1.0, None),    # lantern frame highlight
    "y": ((120, 84, 48, 255), 0.9, (100, 62, 26)),       # lantern glass (dark albedo: its own light would wash it out)
    "Y": ((160, 150, 120, 255), 1.0, (255, 232, 180)),   # lantern flame
    "x": ((150, 96, 96, 255), 0.7, (70, 12, 8)),        # spike metal (a faint red glow: hazards read in the dark)
    "X": ((236, 170, 160, 255), 1.0, (170, 40, 24)),    # spike tip
    "o": ((88, 80, 92, 255), 0.7, None),      # platform stone
    "O": ((140, 130, 138, 255), 1.0, None),   # platform stone highlight
    "q": ((52, 50, 62, 255), 0.6, None),      # door iron
    "Q": ((96, 94, 110, 255), 1.0, None),     # door iron highlight
    "a": ((170, 140, 90, 255), 0.9, None),    # switch lever
    "n": ((120, 40, 36, 255), 0.8, None),     # switch light, off
    "N": ((120, 255, 140, 255), 1.0, (80, 230, 110)),   # switch light, on
    "i": ((60, 120, 220, 255), 0.8, (40, 90, 200)),     # ability orb
    "I": ((200, 230, 255, 255), 1.0, (170, 215, 255)),  # ability orb core
    "j": ((236, 48, 70, 255), 1.0, (220, 34, 56)),      # heart (glows: the HUD hearts are lit like the scene)
    "J": ((255, 200, 210, 255), 1.0, (255, 160, 176)),  # heart shine
    "z": ((74, 30, 44, 255), 0.6, (26, 4, 10)),         # empty heart
    "t": ((196, 44, 38, 255), 0.8, (120, 22, 16)),      # dynamite (glows a little: readable in the dark and on the HUD)
    "T": ((240, 112, 92, 255), 1.0, (200, 70, 50)),     # dynamite highlight
    "W": ((236, 228, 210, 255), 0.9, (110, 104, 92)),   # dynamite label
    "M": ((206, 206, 206, 255), 1.0, None),            # grappling hook metal
    "A": ((224, 176, 64, 255), 0.4, None),             # grappling hook gold
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

# Creature animation strip, facing right (the game mirrors it when moving left).
# Cells: 0-1 idle (1 = blink), 2-5 run, 6 jump, 7 fall, 8-9 climb, 10 land.
_BODY = [
    ".k....k.",
    "kwk..kwk",
    "kwwkkwwk",
    "kwwwkwkk",
    "kwwwwwwk",
    "kwpwwwpk",
    ".kwwwwk.",
]


def _with(rows, **changes):
    out = list(rows)
    for index, row in changes.items():
        out[int(index[1:])] = row
    return out


CREATURE_FRAMES = [
    _BODY + [".kk..kk."],                                   # idle
    _with(_BODY, r3="kwwwwwwk") + [".kk..kk."],             # idle, blink
    _BODY + [".kk..kk."],                                   # run: passing
    _BODY + ["kk....k."],                                   # run: stride
    _BODY + ["..kkkk.."],                                   # run: gather
    _BODY + [".k....kk"],                                   # run: stride (other leg)
    _with(_BODY, r6=".kwwwwk.") + ["..kkkk.."],             # jump: legs tucked
    _with(_BODY, r0="........", r1=".k....k.") + [".k....k."],  # fall: ears back, legs dangling
    _with(_BODY, r0="k......k", r1="kk....kk") + [".k...k.."],  # climb: paws up
    _with(_BODY, r0=".k....k.", r1="kk....kk") + ["..k...k."],  # climb: other side
    ["........"] + _with(_BODY, r6=".kkkkkk.")[:6] + [".kkkkkk."],  # land: squash
]

# Floor vent grate (bottom rows of the cell, resting on the floor).
VENT = [
    "........",
    "........",
    "........",
    "........",
    "........",
    "hhhhhhhh",
    "hkhkhkhk",
    "hhhhhhhh",
]

# Patrolling beetle, facing right; two walking frames.
_BEETLE = [
    "........",
    "........",
    "..kkkk..",
    ".kuUUuk.",
    "kuuuuuuk",
    "kuuuuurk",
    ".kkkkkk.",
]
CRAWLER_FRAMES = [_BEETLE + [".k.k.k.."], _BEETLE + ["..k.k.k."]]

GEM = [
    "........",
    "...E....",
    "..eEe...",
    ".eeEee..",
    "..eee...",
    "...e....",
    "........",
    "........",
]

# Hangs from the ring at the top centre (the game draws the chain above it).
LANTERN = [
    "...ll...",
    "..lLLl..",
    ".lyyyyl.",
    ".lyYYyl.",
    ".lyYYyl.",
    ".lyyyyl.",
    "..lLLl..",
    "...ll...",
]

SPIKES_UP = [
    "........",
    "........",
    "........",
    "........",
    ".X...X..",
    ".x..XxX.",
    "XxX.xxx.",
    "xxxxxxxx",
]
SPIKES_DOWN = list(reversed(SPIKES_UP))

# Moving / falling platform: repeated along its width.
PLATFORM = [
    "OOOOOOOO",
    "oOoooOoo",
    "oooooooo",
    "oooooooo",
    "oooooooo",
    "oooooooo",
    "oooooooo",
    "kkkkkkkk",
]

# Doors and gates: repeated down their height.
DOOR = [
    "qQqqqQqq",
    "qQqqqQqq",
    "qQqqqQqq",
    "kkkkkkkk",
    "qQqqqQqq",
    "qQqqqQqq",
    "qQqqqQqq",
    "qQqqqQqq",
]
GATE = [
    "qQqqqQqq",
    "qQqeqQqq",
    "qQeEeQqq",
    "qQqeqQqq",
    "qQqqqQqq",
    "kkkkkkkk",
    "qQqqqQqq",
    "qQqqqQqq",
]

# Floor switch, bottom-centre on the floor: lever up (off) and down (on).
SWITCH_OFF = [
    "........",
    "..a.....",
    "...a....",
    "....a...",
    "....a...",
    "..hnnh..",
    ".hhhhhh.",
    "hhhhhhhh",
]
SWITCH_ON = [
    "........",
    "........",
    "........",
    "......a.",
    ".....a..",
    "..hNNa..",
    ".hhhhhh.",
    "hhhhhhhh",
]

ORB = [
    "..iiii..",
    ".iIIiii.",
    "iIIiiiii",
    "iIiiiiii",
    "iiiiiiii",
    "iiiiiiIi",
    ".iiiiIi.",
    "..iiii..",
]

# HUD hearts, drawn in the canvas; quarters are cut from these (left / right half, rows 0-2 / 3-7).
HEART_FULL = [
    ".kk..kk.",
    "kJjkkjjk",
    "kjjjjjjk",
    "kjjjjjjk",
    ".kjjjjk.",
    "..kjjk..",
    "...kk...",
    "........",
]
HEART_EMPTY = [row.replace("j", "z").replace("J", "z") for row in HEART_FULL]

# Grappling hook, chain at the upper left and the hook opening toward the lower right.
# A is the gold body (height 0.4); M is the raised metal edge (height 1).
GRAPPLE = [
    "........",
    ".AAA....",
    ".AA.....",
    ".A.AMM..",
    "...MAMM.",
    "...MMAM.",
    "....MMA.",
    "........",
]

# A stick of dynamite standing on its end; the fuse's tip (column 5, row 0) is where the spark glows.
DYNAMITE = [
    ".....h..",
    "....h...",
    "....h...",
    "...tTt..",
    "...tTt..",
    "...WWW..",
    "...tTt..",
    "...ttt..",
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


# The creature's animations as Aseprite's sprite sheet export would describe them (ADR-0030):
# (tag, cells, milliseconds per frame, plays once). A held frame is repeated, as Aseprite does;
# durations are exact fractions so the clips keep the game's frame rates.
CREATURE_TAGS = [
    ("idle", [0, 0, 0, 0, 0, 1], 1000 / 3, False),  # 1 = blink
    ("run", [2, 3, 4, 5], 100, False),
    ("jump", [6], 1000, False),
    ("fall", [7], 1000, False),
    ("climb", [8, 9], 1000 / 6, False),
    ("land", [10], 1000 / 12, True),
]


def write_aseprite_json(name, cell_count, tags):
    frames = []
    frame_tags = []
    for tag, cells, duration, once in tags:
        first = len(frames)
        for cell in cells:
            frames.append({
                "filename": f"{name} {len(frames)}.aseprite",
                "frame": {"x": cell * TILE, "y": 0, "w": TILE, "h": TILE},
                "rotated": False, "trimmed": False,
                "spriteSourceSize": {"x": 0, "y": 0, "w": TILE, "h": TILE},
                "sourceSize": {"w": TILE, "h": TILE},
                "duration": duration,
            })
        entry = {"name": tag, "from": first, "to": len(frames) - 1, "direction": "forward", "color": "#000000ff"}
        if once:
            entry["repeat"] = "1"
        frame_tags.append(entry)
    sheet = {
        "frames": frames,
        "meta": {
            "app": "https://www.aseprite.org/", "version": "1.3", "image": f"{name}.png", "format": "RGBA8888",
            "size": {"w": cell_count * TILE, "h": TILE}, "scale": "1", "frameTags": frame_tags,
            "layers": [{"name": "Layer 1", "opacity": 255, "blendMode": "normal"}], "slices": [],
        },
    }
    (OUT_DIR / f"{name}.json").write_text(json.dumps(sheet, indent=1) + "\n")


def main():
    # Tile ids match the game: 0 empty, 1 wall, 2 grass-topped ground, 3 background, 4 crystal,
    # 5 / 6 spikes pointing up / down (in front of the background).
    empty = ["." * TILE for _ in range(TILE)]
    background = background_tile()
    tiles = [empty, wall_tile(), ground_tile(), background, overlay(background, CRYSTAL),
             overlay(background, SPIKES_UP), overlay(background, SPIKES_DOWN)]
    write_material("tileset", tiles, heights_from_palette, wrap=True)
    write_material("creature", CREATURE_FRAMES, dome_heights, wrap=False)
    write_aseprite_json("creature", len(CREATURE_FRAMES), CREATURE_TAGS)
    write_material("props", [CANDLE, VENT, GEM, LANTERN, PLATFORM, DOOR, SWITCH_OFF, SWITCH_ON, ORB, GATE,
                             HEART_FULL, HEART_EMPTY, DYNAMITE, GRAPPLE],
                   heights_from_palette, wrap=False)
    write_material("crawler", CRAWLER_FRAMES, dome_heights, wrap=False)
    print(f"wrote textures to {OUT_DIR}")


if __name__ == "__main__":
    main()
