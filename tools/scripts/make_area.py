#!/usr/bin/env python3
"""Generates the M6 sample area (ADR-0034): seven rooms around the three original ones.

    python3 tools/scripts/make_area.py            # writes rooms that do not exist yet
    python3 tools/scripts/make_area.py --print    # shows the maps without writing
    python3 tools/scripts/make_area.py --force    # overwrites (loses editor changes!)

The rooms are a starting point to be edited in the level editor or Tiled. Object ids are save keys
(gems): never renumber them in rooms that have been played.

World layout (320×176 rooms):
            x=0               x=1               x=2               x=3
    y=0   cave_entrance     crystal_gallery   root_bridge       gate_hall (goal)
    y=1                     smoke_pit         spike_shaft       flooded_tunnel
    y=2   drowned_shrine    sunken_lake       undercurrent
"""

import argparse
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2] / "assets" / "source" / "world"
W, H, T = 40, 22, 8

# Tile characters -> local tile ids of cave.tsj (gid = id + 1).
TILES = {" ": None, "#": 1, "=": 2, ".": 3, "*": 4, "^": 5, "v": 6}


class Room:
    def __init__(self, name, look, extra_properties=()):
        self.name = name
        self.look = look
        self.extra_properties = list(extra_properties)
        self.grid = [["." for _ in range(W)] for _ in range(H)]
        self.fill(0, 0, W - 1, 0, "#")
        self.fill(0, H - 1, W - 1, H - 1, "#")
        self.fill(0, 0, 0, H - 1, "#")
        self.fill(W - 1, 0, W - 1, H - 1, "#")
        self.objects = []

    def fill(self, x0, y0, x1, y1, ch):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.grid[y][x] = ch
        return self

    def ledge(self, x0, x1, y):
        """A grassy ledge with a wall row under it."""
        self.fill(x0, y, x1, y, "=")
        if y + 1 < H:
            self.fill(x0, y + 1, x1, y + 1, "#")
        return self

    def open(self, x0, y0, x1, y1):
        return self.fill(x0, y0, x1, y1, ".")

    def obj(self, kind, x, y, w=0, h=0, **props):
        self.objects.append((kind, x, y, w, h, props))
        return self

    def text(self):
        return "\n".join("".join(row) for row in self.grid)


def prop(name, value):
    if isinstance(value, bool):
        kind = "bool"
    elif isinstance(value, int):
        kind = "int"
    elif isinstance(value, float):
        kind = "float"
    elif isinstance(value, str) and value.startswith("#") and len(value) == 9:
        kind = "color"
    elif isinstance(value, str) and value.endswith(".json"):
        kind = "file"
    else:
        kind = "string"
    return {"name": name, "type": kind, "value": value}


def tmj(room):
    data = []
    for row in room.grid:
        for ch in row:
            tile = TILES[ch]
            data.append(0 if tile is None else tile + 1)
    objects = []
    for index, (kind, x, y, w, h, props) in enumerate(room.objects, start=1):
        o = {"id": index, "name": "", "type": kind}
        if w == 0 and h == 0:
            o["point"] = True
        o.update({"rotation": 0, "visible": True, "x": x, "y": y, "width": w, "height": h})
        if props:
            o["properties"] = [prop(k, v) for k, v in props.items()]
        objects.append(o)
    return {
        "type": "map", "version": "1.10", "tiledversion": "1.11.0", "orientation": "orthogonal",
        "renderorder": "right-down", "infinite": False, "compressionlevel": -1,
        "width": W, "height": H, "tilewidth": T, "tileheight": T,
        "nextlayerid": 3, "nextobjectid": len(objects) + 1,
        "properties": [prop("look", f"../looks/{room.look}")] + [prop(k, v) for k, v in room.extra_properties],
        "tilesets": [{"firstgid": 1, "source": "../tilesets/cave.tsj"}],
        "layers": [
            {"type": "tilelayer", "id": 1, "name": "tiles", "width": W, "height": H, "x": 0, "y": 0,
             "opacity": 1, "visible": True, "data": data},
            {"type": "objectgroup", "id": 2, "name": "objects", "draworder": "topdown", "x": 0, "y": 0,
             "opacity": 1, "visible": True, "objects": objects},
        ],
    }


def ambient(room, dust=50, fireflies=8, moss=True):
    room.obj("emitter", 0, 0, 320, 176, effect="../effects/dust.effect.json", count=dust)
    if fireflies:
        room.obj("emitter", 0, 0, 320, 176, effect="../effects/firefly.effect.json", count=fireflies)
    if moss:
        room.obj("plants", 0, 0, 320, 176, plant="../plants/moss.plant.json")


# Flooded rooms: the water barely darkens with depth, or a whole room of it would be fog.
DEEP_WATER = {"color": "#ff3c7ca0", "opacity": 0.25, "reflection": 0.1, "deepening": 0.0012}


def root_bridge():
    """Moving and falling platforms over a chasm that drops into the spike shaft; the door to the gate
    hall opens from the switch at the bottom of the shaft."""
    r = Room("root_bridge", "cave.look.json")
    r.open(0, 17, 0, 19)                    # west: crystal_gallery
    r.ledge(1, 8, 20)
    r.open(9, 20, 30, 21)                   # the chasm: falls into spike_shaft
    r.ledge(31, 38, 20)
    r.fill(39, 20, 39, 21, "#")
    r.ledge(35, 38, 17)
    r.ledge(31, 34, 14)
    r.ledge(26, 29, 11)
    r.ledge(32, 38, 8)
    r.fill(39, 8, 39, 9, "#")
    r.fill(33, 4, 39, 4, "#")               # tunnel to the gate hall, roofed so the door cannot be jumped
    r.open(39, 5, 39, 7)
    r.fill(5, 3, 5, 3, "*")
    r.fill(14, 19, 14, 19, "*")
    r.obj("light", 160, 4, color="#ff9ab0d8", radius=170.0, intensity=0.9, height=24.0)
    r.obj("lantern", 36, 60)
    r.obj("candle", 300, 64)
    r.obj("candle", 44, 160)
    r.obj("platform", 80, 144, 24, 8, travelX=136.0, speed=32.0, pause=0.6)
    r.obj("platform", 176, 112, 16, 8, falls=True)   # stepping off the third ledge lands here
    r.obj("platform", 136, 112, 16, 8, falls=True)
    r.obj("vine", 124, 8)
    r.obj("door", 296, 40, 8, 24, flag="bridge")
    r.obj("gem", 144, 98)
    r.obj("gem", 120, 122)
    r.obj("trigger", 8, 136, 24, 24, message="level.bridge_gone", once=True)
    r.obj("entry", 28, 160)
    r.obj("crawler", 300, 160)
    ambient(r, dust=60, fireflies=10)
    return r


def spike_shaft():
    """Down past spikes to the dive orb and the switch for the bridge door; a pool at the bottom leads
    under water to the undercurrent (down) and the flooded tunnel (east)."""
    r = Room("spike_shaft", "ember.look.json", [("look.vignette", 0.4)])
    r.open(9, 0, 30, 0)                     # from root_bridge's chasm
    r.ledge(1, 22, 3)                       # catches the fall; the only way on is the gap
    r.ledge(26, 38, 3)
    r.ledge(1, 38, 13)                      # spike floor
    r.open(2, 13, 3, 14)                    # drop to the bottom level
    r.fill(9, 12, 20, 12, "^")
    r.fill(27, 12, 31, 12, "^")
    r.ledge(18, 19, 10)                     # stepping stones over the spikes (room to jump above)
    r.ledge(29, 30, 10)
    r.fill(33, 5, 35, 5, "v")
    r.open(0, 15, 0, 18)                    # west: smoke_pit
    r.ledge(1, 14, 19)
    r.fill(1, 20, 14, 20, "#")
    r.fill(7, 18, 8, 18, "^")
    r.fill(15, 21, 38, 21, "#")
    r.open(15, 19, 38, 20)                  # the pool
    r.open(20, 21, 23, 21)                  # under water, down: undercurrent
    r.open(39, 19, 39, 20)                  # under water, east: flooded_tunnel
    r.fill(36, 1, 36, 1, "*")
    r.fill(3, 17, 3, 17, "*")
    r.obj("water", 120, 152, 200, 24, **DEEP_WATER)
    r.obj("platform", 104, 80, 16, 8, falls=True)
    r.obj("switch", 44, 152, flag="bridge", message="level.opened_above")
    r.obj("ability", 100, 146, name="dive", message="message.dive")
    r.obj("gem", 284, 92)
    r.obj("gem", 236, 16)
    r.obj("candle", 44, 104)
    r.obj("candle", 164, 24)
    r.obj("lantern", 180, 126)
    r.obj("vent", 60, 104, rate=5.0, force=360.0)
    r.obj("trigger", 160, 8, 40, 16, message="level.spikes", once=True)
    r.obj("light", 120, 84, color="#ffff7050", radius=110.0, intensity=0.9, animation="flicker", amount=0.1)
    r.obj("light", 232, 84, color="#ffff7050", radius=80.0, intensity=0.8, animation="flicker", amount=0.1)
    r.obj("entry", 100, 24)
    ambient(r, dust=90, fireflies=4)
    return r


def flooded_tunnel():
    """Entirely under water: from the spike shaft's pool up to the gate hall's fountain."""
    r = Room("flooded_tunnel", "deep.look.json")
    r.open(0, 19, 0, 20)                    # west: spike_shaft
    r.open(18, 0, 20, 0)                    # up: gate_hall
    r.fill(1, 15, 12, 21, "#")
    r.fill(1, 19, 12, 20, ".")              # the inflow channel
    r.fill(13, 21, 38, 21, "#")
    r.fill(24, 1, 38, 12, "#")
    r.fill(1, 1, 14, 9, "#")
    r.fill(13, 13, 13, 18, "#")
    r.ledge(26, 34, 17)
    r.fill(27, 16, 31, 16, "^")
    r.fill(15, 10, 17, 10, "v")
    r.fill(22, 13, 22, 13, "*")
    r.fill(34, 19, 34, 19, "*")
    r.obj("water", 0, 0, 320, 176, currentY=-18.0, **DEEP_WATER)
    r.obj("gem", 300, 140)
    r.obj("light", 160, 96, color="#ff6aa0e0", radius=90.0, intensity=0.8)
    r.obj("trigger", 104, 144, 16, 24, message="level.water_light", once=True)
    r.obj("light", 56, 160, color="#ff5a90c8", radius=80.0, intensity=0.8)
    r.obj("entry", 40, 166)
    ambient(r, dust=30, fireflies=0, moss=False)
    return r


def gate_hall():
    """The goal: a gate that opens for 14 gems; reached from the bridge tunnel or the fountain."""
    r = Room("gate_hall", "crystal.look.json")
    r.fill(1, 4, 7, 4, "#")
    r.open(0, 5, 0, 7)                      # west: root_bridge tunnel
    r.ledge(1, 9, 8)
    r.ledge(10, 12, 9)                      # steps down to the hall (24 px each)
    r.ledge(13, 16, 12)
    r.ledge(6, 10, 15)
    r.ledge(1, 14, 18)                      # hall floor, west of the fountain
    r.ledge(25, 38, 18)                     # and east of it
    r.fill(1, 20, 38, 21, "#")
    r.open(15, 18, 24, 20)                  # the fountain
    r.open(18, 21, 20, 21)                  # down: flooded_tunnel
    r.fill(30, 4, 38, 11, "#")              # the gate's roof
    r.ledge(18, 22, 13)
    r.fill(6, 2, 6, 2, "*")
    r.fill(26, 3, 26, 3, "*")
    r.fill(36, 16, 36, 16, "*")
    r.obj("water", 120, 144, 80, 32, **DEEP_WATER)
    r.obj("door", 256, 96, 8, 48, gems=14)
    r.obj("goal", 280, 112, 24, 32)
    r.obj("gem", 160, 92)
    r.obj("lantern", 100, 52)
    r.obj("lantern", 220, 52)
    r.obj("candle", 44, 144)
    r.obj("candle", 236, 144)
    r.obj("light", 292, 128, color="#ffffe0a0", radius=70.0, intensity=1.4, animation="pulse", speed=0.4, amount=0.2)
    r.obj("plants", 200, 120, 56, 32, plant="../plants/glowflower.plant.json")
    r.obj("trigger", 216, 120, 24, 24, message="level.old_gate", once=True)
    r.obj("entry", 36, 64)
    ambient(r, dust=40, fireflies=12)
    return r


def sunken_lake():
    """Under the smoke pit's pool: a flooded cave with a current flowing east."""
    r = Room("sunken_lake", "deep.look.json")
    r.open(30, 0, 33, 0)                    # up: smoke_pit's pool
    r.open(0, 9, 0, 13)                     # west: drowned_shrine
    r.open(39, 14, 39, 18)                  # east: undercurrent
    r.fill(1, 1, 26, 4, "#")
    r.fill(36, 1, 38, 8, "#")
    r.fill(1, 14, 10, 20, "#")
    r.fill(14, 8, 24, 10, "#")
    r.fill(14, 7, 24, 7, "^")
    r.fill(16, 11, 22, 11, "v")
    r.fill(26, 19, 38, 20, "#")
    r.fill(28, 18, 34, 18, "^")
    r.fill(12, 17, 20, 20, "#")
    r.fill(4, 5, 4, 5, "*")
    r.fill(30, 12, 30, 12, "*")
    r.fill(18, 16, 18, 16, "*")
    r.obj("water", 0, 0, 320, 176, currentX=30.0, **DEEP_WATER)
    r.obj("gem", 20, 100)
    r.obj("gem", 248, 60)
    r.obj("light", 240, 120, color="#ff60b0d0", radius=100.0, intensity=0.9)
    r.obj("trigger", 232, 0, 40, 32, message="level.current_east", once=True)
    r.obj("light", 60, 90, color="#ff60b0d0", radius=90.0, intensity=0.9)
    r.obj("light", 260, 20, color="#ff80c0e0", radius=80.0, intensity=0.8)
    r.obj("entry", 188, 168)
    ambient(r, dust=25, fireflies=0, moss=False)
    return r


def drowned_shrine():
    """West of the lake, against the current: two gems and the switch for the undercurrent's door."""
    r = Room("drowned_shrine", "deep.look.json", [("look.vignette", 0.45)])
    r.open(39, 9, 39, 13)                   # east: sunken_lake
    r.fill(1, 1, 38, 3, "#")
    r.fill(30, 4, 38, 8, "#")
    r.fill(30, 14, 38, 20, "#")
    r.ledge(4, 26, 18)
    r.fill(1, 19, 38, 20, "#")
    r.fill(8, 12, 12, 17, "#")
    r.fill(18, 12, 22, 17, "#")
    r.fill(13, 17, 17, 17, "^")
    r.fill(10, 4, 10, 4, "*")
    r.fill(20, 4, 20, 4, "*")
    r.fill(15, 16, 15, 16, "*")
    r.obj("water", 0, 0, 320, 176, currentX=24.0, **DEEP_WATER)
    r.obj("switch", 80, 96, flag="shrine", message="level.door_deep")
    r.obj("gem", 40, 120)
    r.obj("gem", 196, 84)
    r.obj("light", 120, 48, color="#ff8070ff", radius=120.0, intensity=1.0, animation="pulse", speed=0.3, amount=0.2)
    r.obj("light", 40, 130, color="#ff60b0d0", radius=80.0, intensity=0.8)
    r.obj("light", 260, 90, color="#ff60b0d0", radius=80.0, intensity=0.8)
    r.obj("entry", 290, 96)
    ambient(r, dust=20, fireflies=0, moss=False)
    return r


def undercurrent():
    """East of the lake: a current pushing back west, a door opened by the shrine's switch, and the way
    up into the spike shaft's pool."""
    r = Room("undercurrent", "deep.look.json")
    r.open(0, 14, 0, 18)                    # west: sunken_lake
    r.open(20, 0, 23, 0)                    # up: spike_shaft's pool
    r.fill(1, 1, 18, 6, "#")
    r.fill(25, 1, 38, 6, "#")
    r.fill(1, 19, 38, 20, "#")
    r.fill(1, 7, 8, 13, "#")
    r.fill(14, 10, 26, 12, "#")
    r.fill(15, 13, 25, 13, "v")
    r.fill(30, 12, 38, 18, "#")
    r.open(31, 14, 37, 17)                  # the nook behind the door
    r.fill(30, 14, 30, 17, ".")
    r.fill(28, 18, 28, 18, "*")
    r.fill(28, 8, 28, 8, "*")
    r.obj("water", 0, 0, 320, 176, currentX=-34.0, **DEEP_WATER)
    r.obj("door", 240, 112, 8, 32, flag="shrine")
    r.obj("gem", 276, 124)
    r.obj("gem", 160, 140)
    r.obj("light", 170, 60, color="#ff5ec8b0", radius=90.0, intensity=0.8)
    r.obj("light", 60, 130, color="#ff60b0d0", radius=90.0, intensity=0.8)
    r.obj("light", 270, 100, color="#ff60b0d0", radius=70.0, intensity=0.7)
    r.obj("entry", 30, 140)
    ambient(r, dust=25, fireflies=0, moss=False)
    return r


ROOMS = [root_bridge, spike_shaft, flooded_tunnel, gate_hall, sunken_lake, drowned_shrine, undercurrent]

WORLD = [
    ("cave_entrance", 0, 0), ("crystal_gallery", 1, 0), ("smoke_pit", 1, 1),
    ("root_bridge", 2, 0), ("gate_hall", 3, 0), ("spike_shaft", 2, 1), ("flooded_tunnel", 3, 1),
    ("drowned_shrine", 0, 2), ("sunken_lake", 1, 2), ("undercurrent", 2, 2),
]

# In the layout the editor writes (short arrays on one line), so saving it from the editor is a no-op.
DEEP_LOOK = """{
  "format": "animal-look",
  "version": 1,
  "base": "cave.look.json",
  "ambient": "#16304a",
  "rimAmbient": "#1c3a54",
  "bloomIntensity": 1.1,
  "saturation": 1.1,
  "shadowTint": [0.8, 0.95, 1.25],
  "highlightTint": [0.9, 1.02, 1.12]
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--print", action="store_true", help="show the maps, write nothing")
    parser.add_argument("--force", action="store_true", help="overwrite existing rooms")
    args = parser.parse_args()
    rooms = [make() for make in ROOMS]
    if args.print:
        for room in rooms:
            print(f"--- {room.name}\n{room.text()}\n")
        return
    for room in rooms:
        path = ROOT / "rooms" / f"{room.name}.tmj"
        if path.exists() and not args.force:
            print(f"kept {path.name} (exists; --force overwrites)")
            continue
        path.write_text(json.dumps(tmj(room), indent=1) + "\n")
        print(f"wrote {path.name}")
    look = ROOT / "looks" / "deep.look.json"
    if not look.exists() or args.force:
        look.write_text(DEEP_LOOK)
    world = {"type": "world", "onlyShowAdjacentMaps": False,
             "maps": [{"fileName": f"rooms/{n}.tmj", "x": x * 320, "y": y * 176, "width": 320, "height": 176}
                      for n, x, y in WORLD]}
    (ROOT / "test.world").write_text(json.dumps(world, indent=1) + "\n")
    print("wrote test.world")


if __name__ == "__main__":
    main()
