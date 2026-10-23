#!/usr/bin/env python3
"""Generates the test world as Tiled files in assets/source/world/ (ADR-0010).

  test.world           room layout (Tiled world file)
  tilesets/cave.tsj    tileset with custom tile properties (solid, grass, light)
  rooms/*.tmj          rooms: tile layer "tiles" + object layer "objects"

Rooms reference the hand-written presets in looks/ (ADR-0015), effects/ (ADR-0017) and plants/
(ADR-0019), which this script leaves alone.

The output is ordinary Tiled data: open test.world in Tiled to edit rooms visually, or use the
in-game level editor (Tab, ADR-0029). This script only exists because the first rooms were authored
as text. Rooms edited since are the truth, so existing files are only overwritten with --force.
"""

import argparse
import json
import pathlib

OUT = pathlib.Path(__file__).resolve().parents[2] / "assets" / "source" / "world"
W, H, TILE = 40, 22, 8
CHARS = {".": 3, "#": 1, "=": 2, "c": 4, " ": 0}  # tile ids (tileset index)


def prop(name, kind, value):
    return {"name": name, "type": kind, "value": value}


def color(r, g, b, a=255):
    return f"#{a:02x}{r:02x}{g:02x}{b:02x}"


# --- Tileset ------------------------------------------------------------------------------

TILESET = {
    "type": "tileset", "version": "1.10", "tiledversion": "1.11.0", "name": "cave",
    "tilewidth": TILE, "tileheight": TILE, "tilecount": 5, "columns": 5, "margin": 0, "spacing": 0,
    "image": "../../textures/tileset.png", "imagewidth": 5 * TILE, "imageheight": TILE,
    "tiles": [
        {"id": 1, "properties": [prop("solid", "bool", True)]},
        {"id": 2, "properties": [prop("solid", "bool", True), prop("grass", "bool", True)]},
        {"id": 4, "properties": [prop("light", "bool", True),
                                 prop("lightColor", "color", color(89, 204, 255)),
                                 prop("lightRadius", "float", 44.0),
                                 prop("lightIntensity", "float", 1.1),
                                 prop("lightHeight", "float", 12.0),
                                 prop("lightAnimation", "string", "pulse"),
                                 prop("lightSpeed", "float", 0.21),
                                 prop("lightAmount", "float", 0.14)]},
    ],
}


# --- Room building helpers ------------------------------------------------------------------------

class Room:
    def __init__(self, name, rows=None):
        self.name = name
        if rows:
            self.grid = [list(r) for r in rows]
        else:  # walls around, grass floor on row 20 (same height as the entrance room)
            self.grid = [list("#" * W)] + [list("#" + "." * (W - 2) + "#") for _ in range(H - 3)]
            self.grid += [list("#" + "=" * (W - 2) + "#"), list("#" * W)]
        self.objects = []
        self.properties = []
        assert len(self.grid) == H and all(len(r) == W for r in self.grid), name

    def set(self, x, y, ch):
        self.grid[y][x] = ch

    def platform(self, row, x0, x1, thick=True):
        for x in range(x0, x1 + 1):
            self.grid[row][x] = "="
            if thick:
                self.grid[row + 1][x] = "#"

    def open(self, row, x0, x1):
        for x in range(x0, x1 + 1):
            self.grid[row][x] = "."

    def obj(self, kind, x, y, **props):
        entry = {"id": len(self.objects) + 1, "name": "", "type": kind, "point": True, "rotation": 0,
                 "visible": True, "x": x, "y": y, "width": 0, "height": 0}
        if props:
            entry["properties"] = [prop(k, *v) for k, v in props.items()]
        self.objects.append(entry)

    def area(self, kind, tx, ty, tw, th, **props):
        entry = {"id": len(self.objects) + 1, "name": "", "type": kind, "rotation": 0, "visible": True,
                 "x": tx * TILE, "y": ty * TILE, "width": tw * TILE, "height": th * TILE}
        if props:
            entry["properties"] = [prop(k, *v) for k, v in props.items()]
        self.objects.append(entry)

    def trigger(self, tx, ty, tw, th, message, once=True):
        self.area("trigger", tx, ty, tw, th, message=("string", message), once=("bool", once))

    def light(self, x, y, rgb, radius, intensity=1.0, height=12.0, cone=None, direction=None):  # pixels
        extra = {}
        if cone is not None:
            extra["cone"] = ("float", cone)
            extra["direction"] = ("float", direction)
        self.obj("light", x, y, color=("color", color(*rgb)), radius=("float", radius),
                 intensity=("float", intensity), height=("float", height), **extra)

    def lantern(self, tx, ty):  # centred in a tile cell; hangs from the ceiling above
        self.obj("lantern", tx * TILE + 4, ty * TILE + 4)

    def emitter_px(self, effect, x, y, w, h, **props):  # an area emitter in pixels
        entry = {"id": len(self.objects) + 1, "name": "", "type": "emitter", "rotation": 0, "visible": True,
                 "x": x, "y": y, "width": w, "height": h,
                 "properties": [prop("effect", "file", f"../effects/{effect}.effect.json")] +
                               [prop(k, "float", float(v)) for k, v in props.items()]}
        self.objects.append(entry)

    def emitter(self, effect, tx=0, ty=0, tw=W, th=H, **props):  # an area emitter (whole room by default)
        extra = {k: (("int" if isinstance(v, int) else "float"), v) for k, v in props.items()}
        self.area("emitter", tx, ty, tw, th, effect=("file", f"../effects/{effect}.effect.json"), **extra)

    def water(self, tx, ty, tw, th, rgb=(40, 90, 120), opacity=0.45, reflection=0.35):
        self.area("water", tx, ty, tw, th, color=("color", color(*rgb)), opacity=("float", opacity),
                  reflection=("float", reflection))

    def plants(self, plant, tx=0, ty=0, tw=W, th=H):  # plants on exposed faces in the area
        self.area("plants", tx, ty, tw, th, plant=("file", f"../plants/{plant}.plant.json"))

    def wind(self, x, y, direction, strength, length, radius):  # a draft from a point (pixels)
        self.obj("wind", x, y, direction=("float", direction), strength=("float", strength),
                 length=("float", length), radius=("float", radius))

    def gem(self, tx, ty):  # centred in a tile cell
        self.obj("gem", tx * TILE + 4, ty * TILE + 4)

    def crawler(self, tx, surface_row):  # feet on the surface
        self.obj("crawler", tx * TILE + 4, surface_row * TILE)

    # Point objects stand on a surface: (tile column, surface row) -> bottom-centre of the prop.
    def candle(self, tx, surface_row):
        self.obj("candle", tx * TILE + 4, surface_row * TILE)

    def vent(self, tx, surface_row, rate=6.0, force=420.0):
        self.obj("vent", tx * TILE + 4, surface_row * TILE, rate=("float", rate), force=("float", force))

    def vine(self, tx, ty):
        self.obj("vine", tx * TILE + 4, ty * TILE)

    def tmj(self):
        data = [CHARS[ch] + 1 if CHARS[ch] else 0 for row in self.grid for ch in row]  # gid = id + firstgid
        return {
            "type": "map", "version": "1.10", "tiledversion": "1.11.0", "orientation": "orthogonal",
            "renderorder": "right-down", "infinite": False, "compressionlevel": -1,
            "width": W, "height": H, "tilewidth": TILE, "tileheight": TILE,
            "nextlayerid": 3, "nextobjectid": len(self.objects) + 1,
            "properties": self.properties,
            "tilesets": [{"firstgid": 1, "source": "../tilesets/cave.tsj"}],
            "layers": [
                {"type": "tilelayer", "id": 1, "name": "tiles", "width": W, "height": H, "x": 0, "y": 0,
                 "opacity": 1, "visible": True, "data": data},
                {"type": "objectgroup", "id": 2, "name": "objects", "draworder": "topdown", "x": 0, "y": 0,
                 "opacity": 1, "visible": True, "objects": self.objects},
            ],
        }


# --- Rooms -------------------------------------------------------------------------------------

def cave_entrance():
    room = Room("cave_entrance", [
        "########################################",
        "#......................................#",
        "#....c.................................#",
        "#......................................#",
        "#......................................#",
        "#.........................======.......#",
        "#.........................######.......#",
        "#......................................#",
        "#.................======...............#",
        "#.................######..........c....#",
        "#......................................#",
        "#..........=====.......................#",
        "#..........#####.......................#",
        "#......................................#",
        "#.................=====........=====...#",
        "#.................#####........#####...#",
        "#.....c................................#",
        "#.........................=============#",
        "#.........................##############",
        "#..............................c........",   # tunnel exit east
        "#======================================#",
        "########################################",
    ])
    room.properties = [prop("look", "file", "../looks/cave.look.json"),
                       # gusts blow in from the tunnel to the east
                       prop("wind.gusts", "float", 1.6), prop("wind.gustInterval", "float", 8.0),
                       prop("wind.gustSpeed", "float", -110.0)]
    room.obj("spawn", 3 * TILE + 3, 20 * TILE)
    for tx, row in [(11, 20), (21, 20), (13, 11), (28, 5), (36, 17), (20, 8)]:
        room.candle(tx, row)
    room.vent(13, 20)
    for tx, ty in [(9, 1), (15, 1), (33, 1), (13, 13), (19, 10), (27, 7)]:
        room.vine(tx, ty)
    room.gem(30, 4)
    room.gem(36, 19)
    room.crawler(33, 14)
    room.trigger(7, 16, 2, 4, "HOLD UP NEAR A VINE TO CLIMB")
    room.trigger(24, 18, 2, 3, "THE TUNNEL LEADS EAST")
    # A crack in the ceiling lets daylight in: a narrow cold spot light shining down through it.
    room.open(0, 21, 22)
    room.light(22 * TILE, 2, (170, 200, 255), 176.0, 1.5, height=24.0, cone=44.0, direction=90.0)
    room.emitter("dust", count=70)
    room.emitter("firefly", count=14)
    room.plants("moss")                                # hanging from every exposed underside
    room.wind(39 * TILE, 19 * TILE + 4, 180.0, 1.6, 130.0, 8.0)  # air flowing in from the tunnel
    return room


def crystal_gallery():
    room = Room("crystal_gallery")
    room.open(19, 0, 0)            # tunnel entrance from the west
    room.open(20, 27, 35)          # pit, edges one tile lower: a full jump out of the smoke pit
                                   # peaks ~2 px short of the normal floor height
    room.open(21, 30, 32)          # hole down to the smoke pit
    for row, x0, x1 in [(17, 18, 23), (14, 6, 10), (14, 31, 36), (11, 17, 21), (8, 8, 12), (8, 28, 32), (5, 18, 22)]:
        room.platform(row, x0, x1)
    for x, y in [(4, 2), (35, 3), (19, 8), (12, 15), (31, 19), (25, 11), (14, 19)]:
        room.set(x, y, "c")
    room.properties = [prop("look", "file", "../looks/crystal.look.json")]
    room.candle(20, 17)
    room.candle(9, 8)
    for tx, ty in [(12, 1), (24, 1), (20, 7), (34, 16)]:
        room.vine(tx, ty)
    room.gem(20, 4)
    room.gem(30, 7)
    room.gem(8, 13)
    room.crawler(10, 20)
    room.trigger(26, 16, 9, 4, "A WARM DRAFT RISES FROM BELOW")
    # Object ids are save keys (gems): only ever append objects to a room.
    room.lantern(14, 11)  # platforms cast shadows around it
    room.emitter("dust", count=50)
    room.emitter("firefly", count=8)
    room.plants("moss")
    room.plants("glowflower", 6, 13, 5, 2)             # on the left platform
    room.plants("glowflower", 28, 7, 5, 2)             # on the upper right platform
    return room


def smoke_pit():
    room = Room("smoke_pit")
    room.open(0, 30, 32)           # hole up to the crystal gallery
    # Staircase up to a landing ledge right under the hole: falling in lands on it, and a
    # straight jump from it rises through the hole into the gallery.
    for row, x0, x1 in [(17, 20, 24), (14, 26, 30), (11, 20, 24), (8, 26, 30), (5, 33, 37), (2, 30, 32)]:
        room.platform(row, x0, x1, thick=False)
    for x, y in [(3, 18), (12, 4), (36, 12)]:
        room.set(x, y, "c")
    room.properties = [prop("look", "file", "../looks/ember.look.json"),
                       prop("look.vignette", "float", 0.35)]  # per-room override of the preset
    room.vent(8, 20, rate=9.0, force=480.0)
    room.vent(14, 20, rate=5.0, force=380.0)
    for tx, row in [(5, 20), (11, 20), (17, 20), (22, 17)]:
        room.candle(tx, row)
    for tx, ty in [(4, 1), (16, 1)]:
        room.vine(tx, ty)
    room.gem(31, 1)
    room.gem(22, 10)
    room.crawler(25, 20)
    room.trigger(18, 18, 3, 2, "CLIMB THE LEDGES TO GET BACK UP")
    room.emitter("dust", count=110)
    room.emitter("firefly", count=5)
    # A pool in the floor under the staircase: banks two tiles high, 16 px of water.
    for x in range(27, 37):
        room.set(x, 20, "#")         # no grass under water
    for x in (26, 37):
        room.set(x, 18, "=")
        room.set(x, 19, "#")
    room.water(27, 18, 10, 2, rgb=(46, 84, 110), reflection=0.45)
    room.lantern(33, 16)  # lights the pool so the surface and reflections read
    room.plants("reeds", 25, 15, 14, 6)                # on the banks and the pool floor
    # A waterfall (GPU particles) pours from under the upper right platform into the pool,
    # vanishing at its surface (floor) and throwing up mist where it lands.
    room.emitter_px("falls", 277, 48, 6, 2, floor=144)
    room.emitter_px("mist", 266, 137, 28, 6)
    return room


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--force", action="store_true",
                        help="overwrite existing rooms (edits made in Tiled or the level editor are lost)")
    args = parser.parse_args()
    rooms = [(cave_entrance(), 0, 0), (crystal_gallery(), W * TILE, 0), (smoke_pit(), W * TILE, H * TILE)]
    outputs = [OUT / "tilesets" / "cave.tsj", OUT / "test.world"] + [OUT / "rooms" / f"{r.name}.tmj" for r, _, _ in rooms]
    existing = [p for p in outputs if p.exists()]
    if existing and not args.force:
        raise SystemExit(f"{len(existing)} file(s) already exist (e.g. {existing[0].relative_to(OUT)}); "
                         "rooms may have been edited since. Use --force to overwrite them.")
    (OUT / "tilesets").mkdir(parents=True, exist_ok=True)
    (OUT / "rooms").mkdir(parents=True, exist_ok=True)
    (OUT / "tilesets" / "cave.tsj").write_text(json.dumps(TILESET, indent=1) + "\n")
    world = {"type": "world", "onlyShowAdjacentMaps": False, "maps": []}
    for room, x, y in rooms:
        (OUT / "rooms" / f"{room.name}.tmj").write_text(json.dumps(room.tmj(), indent=1) + "\n")
        world["maps"].append({"fileName": f"rooms/{room.name}.tmj", "x": x, "y": y,
                              "width": W * TILE, "height": H * TILE})
    (OUT / "test.world").write_text(json.dumps(world, indent=1) + "\n")
    print(f"wrote {len(rooms)} rooms to {OUT}")


if __name__ == "__main__":
    main()
