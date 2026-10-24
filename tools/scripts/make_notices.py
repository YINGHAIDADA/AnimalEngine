#!/usr/bin/env python3
"""Writes assets/package/THIRD_PARTY_NOTICES.txt: the licences of the third-party code compiled into
the released game, taken verbatim from the pinned sources in external/, and of the UI font (copied to
assets/package/fonts by make_font.py). Run after updating a dependency. (Dear ImGui, glslang and SPIRV-Cross are build or development tools and not shipped.)

    python3 tools/scripts/make_notices.py
"""

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
EXTERNAL = ROOT / "external"
OUT = ROOT / "assets" / "package" / "THIRD_PARTY_NOTICES.txt"

MIT_NLOHMANN = """MIT License

Copyright (c) 2013-2025 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
"""


def read(path):
    return (EXTERNAL / path).read_text(encoding="utf-8", errors="replace").strip() + "\n"


def stb_licence():
    text = (EXTERNAL / "stb" / "stb_vorbis.c").read_text(encoding="utf-8", errors="replace")
    start = text.rindex("This software is available under 2 licenses")
    block = text[start:].split("*/")[0]
    return re.sub(r"\n-{10,}\s*$", "", block.strip()) + "\n"


def khronos_licence():
    text = (EXTERNAL / "gl" / "include" / "KHR" / "khrplatform.h").read_text()
    comment = text[text.index("/*") + 2:text.index("*/")]
    lines = [re.sub(r"^\s*\*\*?\s?", "", line) for line in comment.splitlines()]
    return "\n".join(lines).strip() + "\n"


COMPONENTS = [
    ("SDL 3", "Windowing, input, audio and platform layer", lambda: read("SDL/LICENSE.txt")),
    ("Zstandard", "Resource pack compression", lambda: read("zstd/LICENSE")),
    ("volk", "Vulkan function loader", lambda: read("volk/LICENSE.md")),
    ("Vulkan Memory Allocator", "Vulkan memory management", lambda: read("VulkanMemoryAllocator/LICENSE.txt")),
    ("Vulkan-Headers", "Vulkan API headers", lambda: read("Vulkan-Headers/LICENSE.md")),
    ("JSON for Modern C++ (nlohmann/json)", "Data file parsing", lambda: MIT_NLOHMANN),
    ("stb_image, stb_image_write, stb_vorbis", "Image and Ogg Vorbis decoding", stb_licence),
    ("Khronos OpenGL headers", "OpenGL API headers", khronos_licence),
]

FONTS = ROOT / "assets" / "package" / "fonts"
# The UI font (ADR-0041): a subset of Fusion Pixel Font, which draws on the fonts after it.
FONT_COMPONENTS = [
    ("Fusion Pixel Font", "fusion-pixel.txt"),
    ("Ark Pixel Font", "ark-pixel.txt"),
    ("Cubic 11", "cubic-11.txt"),
    ("Galmuri", "galmuri.txt"),
]


def main():
    parts = ["Third-party software in Animal Engine\n",
             "The released game contains the following third-party code, under the licences below.\n"]
    for name, use, licence in COMPONENTS:
        parts.append("\n" + "=" * 78 + f"\n{name} - {use}\n" + "=" * 78 + "\n\n" + licence())
    for name, file in FONT_COMPONENTS:
        text = (FONTS / file).read_text(encoding="utf-8").strip() + "\n"
        parts.append("\n" + "=" * 78 + f"\n{name} - UI font (subset)\n" + "=" * 78 + "\n\n" + text)
    OUT.write_text("".join(parts), encoding="utf-8")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
