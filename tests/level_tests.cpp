// Tiled room / world parsing (ADR-0010).

#include <animal/framework/level.h>
#include <animal/framework/world_map.h>

#include "test_harness.h"

#include <cmath>
#include <map>
#include <string>

namespace {

using namespace animal;

// A 4×3 map: tile 1 (solid wall) on the border row, tile 2 (grassy ground) in the middle.
const char* const kRoom = R"({
  "type": "map", "orientation": "orthogonal", "infinite": false,
  "width": 4, "height": 3, "tilewidth": 8, "tileheight": 8,
  "properties": [{"name": "music", "type": "string", "value": "cave"}],
  "tilesets": [{
    "firstgid": 1, "name": "test", "tilewidth": 8, "tileheight": 8, "tilecount": 3, "columns": 3,
    "tiles": [
      {"id": 1, "properties": [{"name": "solid", "type": "bool", "value": true}]},
      {"id": 2, "properties": [{"name": "solid", "type": "bool", "value": true},
                               {"name": "grass", "type": "bool", "value": true}]}
    ]
  }],
  "layers": [
    {"type": "tilelayer", "name": "tiles", "width": 4, "height": 3,
     "data": [2, 2, 2, 2,  0, 3, 0, 0,  3, 3, 2147483651, 3]},
    {"type": "objectgroup", "name": "objects", "objects": [
      {"id": 1, "name": "start", "class": "spawn", "x": 12, "y": 15, "width": 0, "height": 0, "point": true},
      {"id": 2, "name": "", "type": "light", "x": 4, "y": 5, "width": 0, "height": 0, "point": true,
       "properties": [{"name": "color", "type": "color", "value": "#ff80c0ff"},
                      {"name": "radius", "type": "int", "value": 40},
                      {"name": "intensity", "type": "float", "value": 1.5}]}
    ]}
  ]
})";

std::optional<std::string> noFiles(const std::string&) {
    return std::nullopt;
}

std::optional<Room> parse(const std::string& json, std::string* error = nullptr) {
    return parseRoom(json, "rooms/test.tmj", noFiles, error);
}

std::string replaced(std::string text, const std::string& from, const std::string& to) {
    text.replace(text.find(from), from.size(), to);
    return text;
}

TEST(level_room_parses_tiles_solidity_and_properties) {
    std::string error;
    const auto room = parse(kRoom, &error);
    CHECK(room.has_value());
    if (!room) {
        std::fprintf(stderr, "  %s\n", error.c_str());
        return;
    }
    CHECK(room->tiles.width() == 4 && room->tiles.height() == 3 && room->tiles.tileSize() == 8);
    CHECK(room->tiles.tile(0, 0) == 1);   // gid 2 - firstgid 1
    CHECK(room->tiles.tile(1, 1) == 2);
    CHECK(room->tiles.tile(0, 1) == 0);   // gid 0 = empty
    CHECK(room->tiles.tile(2, 2) == 2);   // flip flag stripped
    CHECK(room->tiles.isSolid(0, 0));
    CHECK(room->tiles.isSolid(1, 1));
    CHECK(!room->tiles.isSolid(0, 1));
    CHECK(room->propertiesOf(2).getBool("grass"));
    CHECK(!room->propertiesOf(1).getBool("grass"));
    CHECK(room->properties.getString("music") == "cave");
}

TEST(level_room_parses_objects_and_typed_properties) {
    const auto room = parse(kRoom);
    CHECK(room.has_value());
    if (!room) {
        return;
    }
    CHECK(room->objects.size() == 2);
    const auto spawns = room->objectsOfType("spawn");
    CHECK(spawns.size() == 1 && spawns[0]->point && spawns[0]->name == "start");
    CHECK(spawns[0]->position == (Vec2{12.0f, 15.0f}));
    const auto lights = room->objectsOfType("light"); // legacy "type" key
    CHECK(lights.size() == 1);
    const Properties& p = lights[0]->properties;
    CHECK((p.getColor("color") == Color::rgba(0x80, 0xc0, 0xff, 0xff)));
    CHECK(p.getFloat("radius") == 40.0f); // int converts to float
    CHECK(p.getFloat("intensity") == 1.5f);
    CHECK(p.getFloat("missing", 7.0f) == 7.0f);
}

TEST(level_room_resolves_external_tilesets) {
    const std::string tileset = R"({"tilewidth": 8, "tileheight": 8, "tilecount": 3, "columns": 3,
        "tiles": [{"id": 1, "properties": [{"name": "solid", "type": "bool", "value": true}]}]})";
    std::string json = kRoom;
    const size_t start = json.find("\"tilesets\"");
    const size_t end = json.find("\"layers\"");
    json.replace(start, end - start, R"("tilesets": [{"firstgid": 1, "source": "../tilesets/test.tsj"}], )");

    std::string requested;
    const auto room = parseRoom(json, "world/rooms/test.tmj",
                                [&](const std::string& path) -> std::optional<std::string> {
                                    requested = path;
                                    return tileset;
                                },
                                nullptr);
    CHECK(room.has_value());
    CHECK(requested == "world/tilesets/test.tsj");
    CHECK(room && room->tiles.isSolid(0, 0));
}

TEST(level_room_rejects_unsupported_maps_with_clear_errors) {
    std::string error;
    CHECK(!parse(replaced(kRoom, "\"orthogonal\"", "\"isometric\""), &error));
    CHECK(error.find("orthogonal") != std::string::npos);
    CHECK(!parse(replaced(kRoom, "\"infinite\": false", "\"infinite\": true"), &error));
    CHECK(error.find("infinite") != std::string::npos);
    CHECK(!parse(replaced(kRoom, "\"name\": \"tiles\", \"width\": 4", "\"name\": \"tiles\", \"encoding\": \"base64\", \"width\": 4"), &error));
    CHECK(error.find("CSV") != std::string::npos);
    CHECK(!parse(replaced(kRoom, "3, 3, 2147483651, 3", "3, 3, 9, 3"), &error)); // gid 9 not in the tileset
    CHECK(error.find("gid 9") != std::string::npos);
    CHECK(!parse("{ not json", &error));
    CHECK(error.find("invalid JSON") != std::string::npos);
    CHECK(!parse(replaced(kRoom, "\"width\": 4, \"height\": 3, \"tilewidth\"", "\"width\": \"4\", \"height\": 3, \"tilewidth\""), &error));
    CHECK(error.find("rooms/test.tmj") != std::string::npos); // errors name the file
}

TEST(level_world_finds_rooms_by_point) {
    const char* json = R"({"type": "world", "maps": [
        {"fileName": "rooms/a.tmj", "x": 0, "y": 0, "width": 320, "height": 176},
        {"fileName": "rooms/b.tmj", "x": 320, "y": 0, "width": 320, "height": 176}]})";
    std::string error;
    const auto world = parseWorld(json, "data/world/test.world", &error);
    CHECK(world.has_value());
    if (!world) {
        return;
    }
    CHECK(world->rooms.size() == 2);
    CHECK(world->rooms[1].path == "data/world/rooms/b.tmj");
    const WorldRoom* a = world->roomContaining({319, 100});
    const WorldRoom* b = world->roomContaining({320, 100});
    CHECK(a && a->path == "data/world/rooms/a.tmj");
    CHECK(b && b->path == "data/world/rooms/b.tmj");
    CHECK(world->roomContaining({100, 200}) == nullptr);

    const char* overlapping = R"({"maps": [
        {"fileName": "a.tmj", "x": 0, "y": 0, "width": 320, "height": 176},
        {"fileName": "b.tmj", "x": 300, "y": 0, "width": 320, "height": 176}]})";
    CHECK(!parseWorld(overlapping, "w.world", &error));
    CHECK(error.find("overlap") != std::string::npos);
}

TEST(level_world_map_fits_the_whole_world_centred) {
    World world;
    world.rooms = {{"a", {0, 0, 320, 176}}, {"b", {320, 0, 320, 176}}, {"c", {320, 176, 320, 176}}};
    const WorldMapLayout map = layoutWorldMap(world, {10.0f, 20.0f, 200.0f, 200.0f}, 1.0f);
    CHECK(map.rooms.size() == 3);
    CHECK(std::fabs(map.scale - 200.0f / 640.0f) < 1e-6f); // width-bound
    CHECK(map.rooms[0].x == 10.0f && map.rooms[1].x == map.rooms[0].x + map.rooms[0].width);
    CHECK(map.rooms[2].y == map.rooms[1].y + map.rooms[1].height); // neighbours share edges
    const float top = map.rooms[0].y;
    const float bottom = map.rooms[2].y + map.rooms[2].height;
    CHECK(std::fabs((top - 20.0f) - (220.0f - bottom)) <= 1.0f); // centred vertically
    for (const RectF& r : map.rooms) {
        CHECK(r.x == std::round(r.x) && r.width == std::round(r.width));
    }

    const WorldMapLayout small = layoutWorldMap(world, {0.0f, 0.0f, 4000.0f, 4000.0f}, 0.25f);
    CHECK(small.scale == 0.25f && small.rooms[0].width == 80.0f);
    CHECK(small.toScreen({320.0f, 0.0f}).x == small.rooms[1].x);
    CHECK(layoutWorldMap(World{}, {0.0f, 0.0f, 10.0f, 10.0f}, 1.0f).rooms.empty());
}

} // namespace
