#!/usr/bin/env python3
"""Writes the UI font (ADR-0041): a glyph sheet (fonts/ui.png) and its description (fonts/ui.font.json)
into assets/source and tests/data, from a BDF pixel font: by default Fusion Pixel Font (proportional,
zh_hans; SIL OFL 1.1) at --size pixels, downloaded into build/font_cache on first use, or any BDF file
given with --bdf.

Glyphs: printable ASCII and every GB2312 symbol and hanzi the font has (so text typed into levels and
string tables in simplified Chinese is covered), after the font's missing-glyph box (its DEFAULT_CHAR, else "?"). The cell is the
font's bounding box width by its ascent + descent, with the baseline below the ascent; the description
lists the characters in sheet order and their advances.

The font's licences (the licence file next to the BDF, and LICENSES/<font>/* for the fonts it draws
glyphs from) replace assets/package/fonts, listed in licences.json for make_notices.py; run it after.

    python3 tools/scripts/make_font.py               # Fusion Pixel 10px
    python3 tools/scripts/make_font.py --size 12
    python3 tools/scripts/make_font.py --bdf path/to/font.bdf
"""

import argparse
import json
import pathlib
import sys
import urllib.request
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from make_placeholder_art import write_png  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
FUSION_VERSION = "2026.10.16"
CACHE = ROOT / "build" / "font_cache"
OUTPUTS = [ROOT / "assets" / "source" / "fonts", ROOT / "tests" / "data" / "fonts"]
LICENCES = ROOT / "assets" / "package" / "fonts"
COLUMNS = 128


def fusion_pixel(size):
    name = f"fusion-pixel-font-{size}px-proportional-bdf-v{FUSION_VERSION}"
    folder = CACHE / name
    bdf = folder / f"fusion-pixel-{size}px-proportional-zh_hans.bdf"
    if not bdf.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        archive = CACHE / f"{name}.zip"
        if not archive.exists():
            url = f"https://github.com/TakWolf/fusion-pixel-font/releases/download/{FUSION_VERSION}/{name}.zip"
            print(f"downloading {url}")
            urllib.request.urlretrieve(url, archive)
        with zipfile.ZipFile(archive) as z:
            z.extractall(folder)
    return bdf


def read_bdf(path):
    """(properties, glyphs): glyphs by code point = (advance, [w, h, xoff, yoff], [(bits, bit count)])."""
    properties = {}
    glyphs = {}
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    i = 0
    while i < len(lines):
        key, _, value = lines[i].partition(" ")
        if key != "STARTCHAR":
            if key in ("FONTBOUNDINGBOX", "FONT_ASCENT", "FONT_DESCENT", "DEFAULT_CHAR", "FAMILY_NAME"):
                properties[key] = value.strip().strip('"')
            i += 1
            continue
        code = advance = bbx = None
        rows = []
        i += 1
        while not lines[i].startswith("ENDCHAR"):
            key, _, value = lines[i].partition(" ")
            if key == "ENCODING":
                code = int(value.split()[0])
            elif key == "DWIDTH":
                advance = int(value.split()[0])
            elif key == "BBX":
                bbx = [int(v) for v in value.split()]
            elif key == "BITMAP":
                i += 1
                while not lines[i].startswith("ENDCHAR"):
                    hexits = lines[i].strip()
                    rows.append((int(hexits, 16), len(hexits) * 4))  # MSB = leftmost pixel
                    i += 1
                break
            i += 1
        if code is not None and code >= 0:
            glyphs[code] = (advance, bbx, rows)
        i += 1
    return properties, glyphs


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


def licence_files(bdf, family):
    """[(display name, source path)]: the font's own licence, then those of the fonts it draws on."""
    folder = bdf.parent
    own = sorted(p for p in folder.iterdir() if p.is_file() and p.suffix.lower() in (".txt", ".md")
                 and any(k in p.name.upper() for k in ("OFL", "LICENSE", "LICENCE")))
    found = [(family, p) for p in own[:1]]
    sources = folder / "LICENSES"
    if sources.is_dir():
        for sub in sorted(p for p in sources.iterdir() if p.is_dir()):
            for file in sorted(p for p in sub.iterdir() if p.is_file()):
                found.append((sub.name.replace("-", " ").title().replace("9X9", "9x9"), file))
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--size", type=int, default=10, help="Fusion Pixel size: 8, 10 or 12")
    parser.add_argument("--bdf", type=pathlib.Path, help="any BDF font instead of Fusion Pixel")
    args = parser.parse_args()
    bdf = args.bdf if args.bdf else fusion_pixel(args.size)

    properties, glyphs = read_bdf(bdf)
    cell_w = int(properties["FONTBOUNDINGBOX"].split()[0])
    baseline = int(properties["FONT_ASCENT"])
    cell_h = baseline + int(properties["FONT_DESCENT"])
    family = properties.get("FAMILY_NAME", bdf.stem)
    missing = int(properties.get("DEFAULT_CHAR", "-1"))
    if missing not in glyphs:
        missing = ord("?")

    wanted = wanted_characters()
    order = [missing] + [ord(c) for c in wanted if ord(c) in glyphs]
    rows_count = (len(order) + COLUMNS - 1) // COLUMNS
    width, height = COLUMNS * cell_w, rows_count * cell_h
    pixels = [[0] * (width * 4) for _ in range(height)]
    advances = []
    clipped = 0
    for index, code in enumerate(order):
        advance, (w, h, xoff, yoff), bitmap = glyphs[code]
        advances.append(min(advance, cell_w))
        cx, cy = (index % COLUMNS) * cell_w, (index // COLUMNS) * cell_h
        top = baseline - (yoff + h)
        for r, (bits, nbits) in enumerate(bitmap):
            for col in range(w):
                if not (bits >> (nbits - 1 - col)) & 1:
                    continue
                x, y = xoff + col, top + r
                if not (0 <= x < cell_w and 0 <= y < cell_h):
                    clipped += 1
                    continue
                pixels[cy + y][(cx + x) * 4:(cx + x) * 4 + 4] = [255, 255, 255, 255]
    description = {
        "format": "animal-font",
        "version": 1,
        "image": "ui.png",
        "cellWidth": cell_w,
        "cellHeight": cell_h,
        "baseline": baseline,
        "columns": COLUMNS,
        "missing": 0,
        "source": f"{family} ({bdf.name})",
        "characters": "".join(chr(c) for c in order[1:]),
        "advances": advances,
    }

    licences = licence_files(bdf, family)
    if not licences:
        sys.exit(f"no licence file next to {bdf}: the release must include the font's licence")
    LICENCES.mkdir(parents=True, exist_ok=True)
    for old in LICENCES.iterdir():
        old.unlink()
    listed = []
    for name, source in licences:
        file = f"{len(listed):02d}-{source.parent.name if source.parent != bdf.parent else 'font'}.txt"
        (LICENCES / file).write_text(source.read_text(encoding="utf-8", errors="replace"), encoding="utf-8")
        listed.append({"name": name, "file": file})
    (LICENCES / "licences.json").write_text(json.dumps(listed, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    for out in OUTPUTS:
        write_png(out / "ui.png", width, height, pixels)
        (out / "ui.font.json").write_text(json.dumps(description, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"{family}: {len(order)} glyphs in {cell_w}x{cell_h} cells ({len(wanted) - len(order) + 1} wanted "
          f"characters not in the font, {clipped} pixels clipped), sheet {width}x{height}, "
          f"licences: {', '.join(n for n, _ in licences)}")


if __name__ == "__main__":
    main()
