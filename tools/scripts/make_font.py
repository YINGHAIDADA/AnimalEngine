#!/usr/bin/env python3
"""Writes the UI font (ADR-0041): a glyph sheet (fonts/ui.png) and its description (fonts/ui.font.json)
into assets/source and tests/data, from Fusion Pixel Font 12px (proportional, zh_hans; SIL OFL 1.1).

The font's licences go to assets/package/fonts (gathered into THIRD_PARTY_NOTICES.txt by
make_notices.py). Glyphs: printable ASCII, every GB2312 symbol and hanzi (so text typed into levels and string tables
in simplified Chinese is covered), and the font's missing-glyph box first. Each glyph sits in a
12 x 16 cell with its baseline on row 13; the description lists the characters in sheet order and
their advances. The BDF release is downloaded into build/font_cache on first use.

    python3 tools/scripts/make_font.py
"""

import json
import pathlib
import sys
import urllib.request
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from make_placeholder_art import write_png  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
VERSION = "2026.10.16"
ZIP_NAME = f"fusion-pixel-font-12px-proportional-bdf-v{VERSION}.zip"
URL = f"https://github.com/TakWolf/fusion-pixel-font/releases/download/{VERSION}/{ZIP_NAME}"
CACHE = ROOT / "build" / "font_cache"
BDF_NAME = "fusion-pixel-12px-proportional-zh_hans.bdf"
OUTPUTS = [ROOT / "assets" / "source" / "fonts", ROOT / "tests" / "data" / "fonts"]
LICENCES = ROOT / "assets" / "package" / "fonts"
# The release's licence files: Fusion Pixel and the fonts it draws its glyphs from.
LICENCE_FILES = {
    "fusion-pixel.txt": "OFL.txt",
    "ark-pixel.txt": "LICENSES/ark-pixel/OFL.txt",
    "cubic-11.txt": "LICENSES/cubic-11/OFL.txt",
    "galmuri.txt": "LICENSES/galmuri/LICENSE.txt",
}

CELL_W, CELL_H = 12, 16
BASELINE = 13  # rows above the baseline (the font's ascent)
COLUMNS = 128
MISSING = 0xFFFE  # the font's .notdef box


def bdf_path():
    path = CACHE / "fp" / BDF_NAME
    if not path.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        archive = CACHE / ZIP_NAME
        if not archive.exists():
            print(f"downloading {URL}")
            urllib.request.urlretrieve(URL, archive)
        with zipfile.ZipFile(archive) as z:
            z.extractall(CACHE / "fp")
    return path


def read_bdf(path):
    glyphs = {}
    lines = path.read_text(encoding="utf-8").splitlines()
    i = 0
    while i < len(lines):
        if not lines[i].startswith("STARTCHAR"):
            i += 1
            continue
        code = advance = None
        bbx = None
        rows = []
        i += 1
        while not lines[i].startswith("ENDCHAR"):
            key, _, value = lines[i].partition(" ")
            if key == "ENCODING":
                code = int(value)
            elif key == "DWIDTH":
                advance = int(value.split()[0])
            elif key == "BBX":
                bbx = [int(v) for v in value.split()]
            elif key == "BITMAP":
                i += 1
                while not lines[i].startswith("ENDCHAR"):
                    hexits = lines[i].strip()
                    rows.append((int(hexits, 16), len(hexits) * 4))  # (bits, bit count), MSB = left
                    i += 1
                break
            i += 1
        glyphs[code] = (advance, bbx, rows)
        i += 1
    return glyphs


def wanted_characters():
    chars = [chr(c) for c in range(0x20, 0x7F)]
    for high in range(0xA1, 0xF8):
        for low in range(0xA1, 0xFF):
            try:
                chars.append(bytes([high, low]).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    seen = set()
    return [c for c in chars if not (c in seen or seen.add(c))]


def main():
    glyphs = read_bdf(bdf_path())
    order = [chr(MISSING)] + [c for c in wanted_characters() if ord(c) in glyphs]
    skipped = len(wanted_characters()) - (len(order) - 1)
    rows_count = (len(order) + COLUMNS - 1) // COLUMNS
    width, height = COLUMNS * CELL_W, rows_count * CELL_H
    pixels = [[0] * (width * 4) for _ in range(height)]
    advances = []
    clipped = 0
    for index, char in enumerate(order):
        advance, (w, h, xoff, yoff), bitmap = glyphs[ord(char)]
        advances.append(min(advance, CELL_W))
        cx, cy = (index % COLUMNS) * CELL_W, (index // COLUMNS) * CELL_H
        top = BASELINE - (yoff + h)
        for r, (bits, nbits) in enumerate(bitmap):
            for col in range(w):
                if not (bits >> (nbits - 1 - col)) & 1:
                    continue
                x, y = xoff + col, top + r
                if not (0 <= x < CELL_W and 0 <= y < CELL_H):
                    clipped += 1
                    continue
                p = pixels[cy + y]
                p[(cx + x) * 4:(cx + x) * 4 + 4] = [255, 255, 255, 255]
    description = {
        "format": "animal-font",
        "version": 1,
        "image": "ui.png",
        "cellWidth": CELL_W,
        "cellHeight": CELL_H,
        "baseline": BASELINE,
        "columns": COLUMNS,
        "missing": 0,
        "source": f"Fusion Pixel Font 12px proportional zh_hans {VERSION} (SIL OFL 1.1)",
        "characters": "".join(order[1:]),
        "advances": advances,
    }
    LICENCES.mkdir(parents=True, exist_ok=True)
    for name, source in LICENCE_FILES.items():
        (LICENCES / name).write_text((CACHE / "fp" / source).read_text(encoding="utf-8"), encoding="utf-8")
    for out in OUTPUTS:
        write_png(out / "ui.png", width, height, pixels)
        (out / "ui.font.json").write_text(json.dumps(description, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"{len(order)} glyphs ({skipped} wanted characters not in the font, {clipped} pixels clipped), "
          f"{width}x{height}")


if __name__ == "__main__":
    main()
