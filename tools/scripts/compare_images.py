#!/usr/bin/env python3
"""Compares two 8-bit RGBA PNG screenshots (e.g. OpenGL vs Vulkan render-consistency checks).

usage: compare_images.py a.png b.png [--tolerance N] [--max-diff-pixels N]
Exit code 0 when the images match within the tolerances, 1 otherwise.
Pure standard library (zlib + struct).
"""

import argparse
import struct
import sys
import zlib


def read_png_rgba(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG file")
    pos, idat = 8, b""
    width = height = None
    while pos < len(data):
        length, tag = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if tag == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or color != 6 or interlace != 0:
                raise ValueError(f"{path}: only 8-bit non-interlaced RGBA is supported")
        elif tag == b"IDAT":
            idat += body
        pos += 12 + length

    raw = zlib.decompress(idat)
    stride = width * 4
    pixels = bytearray(height * stride)
    prev = bytearray(stride)
    offset = 0
    for y in range(height):
        kind = raw[offset]
        row = bytearray(raw[offset + 1:offset + 1 + stride])
        offset += 1 + stride
        for x in range(stride):
            left = row[x - 4] if x >= 4 else 0
            up = prev[x]
            up_left = prev[x - 4] if x >= 4 else 0
            if kind == 1:
                row[x] = (row[x] + left) & 0xFF
            elif kind == 2:
                row[x] = (row[x] + up) & 0xFF
            elif kind == 3:
                row[x] = (row[x] + ((left + up) >> 1)) & 0xFF
            elif kind == 4:
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                predictor = left if pa <= pb and pa <= pc else (up if pb <= pc else up_left)
                row[x] = (row[x] + predictor) & 0xFF
        pixels[y * stride:(y + 1) * stride] = row
        prev = row
    return width, height, pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("a")
    parser.add_argument("b")
    parser.add_argument("--tolerance", type=int, default=0, help="max allowed per-channel difference")
    parser.add_argument("--max-diff-pixels", type=int, default=0, help="pixels allowed to exceed the tolerance")
    args = parser.parse_args()

    wa, ha, pa = read_png_rgba(args.a)
    wb, hb, pb = read_png_rgba(args.b)
    if (wa, ha) != (wb, hb):
        print(f"size mismatch: {wa}x{ha} vs {wb}x{hb}")
        return 1

    diff_pixels, max_diff, first = 0, 0, None
    for i in range(0, len(pa), 4):
        d = max(abs(pa[i + c] - pb[i + c]) for c in range(4))
        if d > args.tolerance:
            diff_pixels += 1
            if first is None:
                first = ((i // 4) % wa, (i // 4) // wa)
        max_diff = max(max_diff, d)

    total = wa * ha
    print(f"{wa}x{ha}: {diff_pixels} of {total} pixels differ beyond tolerance {args.tolerance} "
          f"(max channel difference {max_diff}" + (f", first at {first})" if first else ")"))
    return 0 if diff_pixels <= args.max_diff_pixels else 1


if __name__ == "__main__":
    sys.exit(main())
