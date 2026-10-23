#!/usr/bin/env python3
"""Generates the test-only rooms and world in tests/data (ADR-0035).

    python3 tools/scripts/make_test_data.py           # (re)writes tests/data/world/rooms/test_*.tmj and the world
    python3 tools/scripts/make_test_data.py --print   # shows the maps

tests/data is a complete data directory used only by the automated tests (--data=<build>/test_data):
its own world, presets, textures and sounds, so editing the game's content never affects a test.
  render_*  frozen copies of the first three rooms: the demo route and the rendering scenes
  test_*    one capability each, driven by trivial input (see game/CMakeLists.txt)
After changing a test room, refresh the render goldens if a rendering scene shows it
(tools/scripts/render_regression.py --update) and rerun ctest.
"""

import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from make_area import Room, tmj  # noqa: E402  (same room format and helpers)

ROOT = pathlib.Path(__file__).resolve().parents[2] / "tests" / "data" / "world"
WATER = {"color": "#ff3c7ca0", "opacity": 0.3, "reflection": 0.2}


def floored(name, look="cave.look.json"):
    """A closed room with a floor on row 20 (stand at y = 160)."""
    r = Room(name, look)
    r.ledge(1, 38, 20)
    r.obj("light", 160, 8, color="#ffc8d0e0", radius=200.0, intensity=1.0, height=24.0)
    return r


def test_walk():
    r = floored("test_walk")
    r.open(39, 17, 39, 19)                  # east: test_walk_east
    r.obj("entry", 40, 160)
    return r


def test_walk_east():
    r = floored("test_walk_east")
    r.open(0, 17, 0, 19)
    return r


def test_platform():
    """Standing on a moving platform carries the player into a gem out of reach from the floor."""
    r = floored("test_platform")
    r.obj("platform", 40, 120, 24, 8, travelX=200.0, speed=50.0, pause=0.2)
    r.obj("gem", 252, 113)
    r.obj("entry", 52, 120)
    return r


def test_crush():
    """A platform rising into a low ceiling crushes the player standing on it."""
    r = floored("test_crush")
    r.fill(8, 12, 16, 13, "#")              # the ceiling, bottom at y = 112
    r.obj("platform", 72, 144, 24, 8, travelY=-40.0, speed=30.0, pause=0.5)
    r.obj("entry", 84, 144)
    return r


def test_falling():
    """A falling platform over a hole drops the player into the room below."""
    r = floored("test_falling")
    r.open(15, 20, 24, 21)
    r.obj("platform", 136, 120, 32, 8, falls=True)
    r.obj("entry", 152, 120)
    return r


def test_falling_below():
    r = floored("test_falling_below")
    r.open(15, 0, 24, 0)
    return r


def test_spikes():
    r = floored("test_spikes")
    r.fill(12, 19, 14, 19, "^")
    r.fill(20, 1, 22, 1, "v")
    r.obj("entry", 40, 160)
    return r


def test_crawler():
    r = floored("test_crawler")
    r.obj("entry", 40, 160)
    r.obj("crawler", 160, 160)              # object id 3: odd ids walk left, towards the player
    return r


def door_room(name, with_switch):
    """Floor, (a switch,) a door and a gem behind it; the door is roofed so it cannot be jumped."""
    r = floored(name)
    r.fill(22, 1, 23, 16, "#")              # the wall around the door, down to the door's top
    if with_switch:
        r.obj("switch", 100, 160, flag="test_door")
    r.obj("door", 176, 136, 8, 24, flag="test_door" if with_switch else "test_elsewhere")
    r.obj("gem", 240, 156)
    r.obj("entry", 40, 160)
    return r


def test_dive():
    """A gem in a chamber reached only under water, beneath the wall that divides the pool."""
    r = Room("test_dive", "cave.look.json")
    r.fill(1, 15, 7, 21, "#")               # the ledge the player starts on (top at y = 120)
    r.fill(8, 20, 38, 21, "#")              # the pool's floor
    r.fill(20, 1, 38, 17, "#")              # the wall over the chamber
    r.fill(31, 18, 38, 19, "#")
    r.obj("light", 100, 40, color="#ffc8d0e0", radius=200.0, intensity=1.0, height=24.0)
    r.obj("water", 64, 120, 184, 40, **WATER)
    r.obj("gem", 216, 152)
    r.obj("entry", 40, 120)
    return r


def test_current():
    """A current carries a floating player along the surface into a gem."""
    r = Room("test_current", "cave.look.json")
    r.fill(1, 15, 5, 21, "#")
    r.fill(6, 20, 38, 21, "#")
    r.obj("light", 160, 40, color="#ffc8d0e0", radius=200.0, intensity=1.0, height=24.0)
    r.obj("water", 48, 120, 264, 40, currentX=50.0, **WATER)
    r.obj("gem", 280, 124)
    r.obj("entry", 24, 120)
    return r


def test_gate():
    """A gate that wants 3 gems, the goal behind it."""
    r = floored("test_gate", "crystal.look.json")
    r.fill(24, 1, 38, 14, "#")              # the gate's roof
    r.obj("door", 200, 120, 8, 40, gems=3)
    r.obj("goal", 256, 120, 32, 40)
    r.obj("entry", 40, 160)
    return r


def test_rise():
    """Flooded: swimming up through the ceiling into the pool of the room above."""
    r = Room("test_rise", "deep.look.json")
    r.open(18, 0, 21, 0)
    r.obj("light", 160, 80, color="#ff80c0e0", radius=160.0, intensity=1.0)
    r.obj("water", 0, 0, 320, 176, deepening=0.0012, **WATER)
    r.obj("entry", 160, 168)
    return r


def test_rise_above():
    r = Room("test_rise_above", "cave.look.json")
    r.ledge(1, 14, 18)
    r.ledge(25, 38, 18)
    r.fill(1, 20, 38, 21, "#")
    r.open(15, 18, 24, 20)
    r.open(18, 21, 21, 21)
    r.obj("light", 160, 40, color="#ffc8d0e0", radius=200.0, intensity=1.0, height=24.0)
    r.obj("water", 120, 144, 80, 32, **WATER)
    return r


def render_mechanisms():
    """Every mechanism's look in one scene: platforms, doors, a gate, switches, the orb, spikes and a
    flooded corner with a current."""
    r = floored("render_mechanisms")
    r.fill(4, 19, 7, 19, "^")
    r.fill(10, 1, 13, 1, "v")
    r.fill(24, 13, 38, 19, "#")
    r.open(25, 14, 37, 19)
    r.obj("platform", 72, 112, 24, 8, travelX=64.0, speed=20.0)
    r.obj("platform", 40, 80, 16, 8, falls=True)
    r.obj("door", 152, 128, 8, 32, flag="render_door")
    r.obj("door", 176, 128, 8, 32, gems=3)
    r.obj("switch", 128, 160, flag="render_switch")
    r.obj("ability", 112, 100, name="dive")
    r.obj("water", 200, 112, 104, 48, currentX=20.0, deepening=0.0012, **WATER)
    r.obj("lantern", 236, 60)
    r.obj("light", 96, 96, color="#ffffe0b0", radius=110.0, intensity=1.3, height=20.0)
    r.obj("light", 160, 140, color="#ffc0d8ff", radius=70.0, intensity=1.2, height=16.0)
    r.obj("candle", 140, 160)
    r.obj("candle", 60, 160)
    r.obj("crawler", 280, 104)
    r.obj("gem", 240, 136)
    r.obj("entry", 24, 160)
    return r


ROOMS = [test_walk, test_walk_east, test_platform, test_crush, test_falling, test_falling_below, test_spikes,
         test_crawler, lambda: door_room("test_switch", True), lambda: door_room("test_door_locked", False),
         test_dive, test_current, test_gate, test_rise, test_rise_above, render_mechanisms]

# (room, x, y) in 320×176 room units. Test rooms stand apart: only the designed pairs touch.
WORLD = [
    ("render_entrance", 0, 0), ("render_gallery", 1, 0), ("render_pit", 1, 1), ("render_mechanisms", 3, 0),
    ("test_walk", 0, 3), ("test_walk_east", 1, 3), ("test_platform", 3, 3), ("test_crush", 5, 3),
    ("test_falling", 7, 3), ("test_falling_below", 7, 4), ("test_spikes", 0, 6), ("test_crawler", 2, 6),
    ("test_switch", 4, 6), ("test_door_locked", 6, 6), ("test_dive", 0, 8), ("test_current", 2, 8),
    ("test_gate", 4, 8), ("test_rise_above", 6, 8), ("test_rise", 6, 9),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--print", action="store_true", help="show the maps, write nothing")
    args = parser.parse_args()
    rooms = [make() for make in ROOMS]
    if args.print:
        for room in rooms:
            print(f"--- {room.name}\n{room.text()}\n")
        return
    for room in rooms:
        (ROOT / "rooms" / f"{room.name}.tmj").write_text(json.dumps(tmj(room), indent=1) + "\n")
    world = {"type": "world", "onlyShowAdjacentMaps": False,
             "maps": [{"fileName": f"rooms/{n}.tmj", "x": x * 320, "y": y * 176, "width": 320, "height": 176}
                      for n, x, y in WORLD]}
    (ROOT / "test.world").write_text(json.dumps(world, indent=1) + "\n")
    print(f"wrote {len(rooms)} test rooms and the world to {ROOT}")


if __name__ == "__main__":
    main()
