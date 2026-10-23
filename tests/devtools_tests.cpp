// Developer tools that do not need a window: JSON formatting and source / data path mapping.

#include <animal/core/file.h>
#include <animal/devtools/file_watcher.h>
#include <animal/devtools/json_editor.h>
#include <animal/devtools/source_assets.h>
#include <animal/devtools/tiled_map.h>
#include <animal/devtools/tiled_world.h>
#include <animal/framework/level.h>

#include "test_harness.h"

#include <filesystem>
#include <string>

namespace {

// Cleanup must not throw (Windows refuses to delete files that are still open).
std::error_code cleanupError;

using namespace animal;

// Saving a preset from the editor must not reformat a hand-written file.
TEST(devtools_format_json_reproduces_the_data_files) {
    namespace fs = std::filesystem;
    const fs::path world = fs::path(ANIMAL_TEST_SOURCE_DIR) / "assets" / "source" / "world";
    int files = 0;
    for (const char* dir : {"looks", "effects", "plants"}) {
        for (const fs::directory_entry& entry : fs::directory_iterator(world / dir)) {
            const std::u8string u8 = entry.path().generic_u8string();
            const std::optional<std::string> text = readText(std::string(u8.begin(), u8.end()));
            CHECK(text.has_value());
            if (!text) {
                continue;
            }
            const std::string formatted = devtools::formatJson(devtools::JsonDocument::parse(*text));
            if (formatted != *text) {
                std::fprintf(stderr, "  reformatted: %s\n", reinterpret_cast<const char*>(u8.c_str()));
            }
            CHECK(formatted == *text);
            ++files;
        }
    }
    CHECK(files >= 10);
}

TEST(devtools_format_json_numbers_and_nesting) {
    const auto doc = devtools::JsonDocument::parse(R"({"a": 2.0, "b": 0.30000001, "c": [1, [2, 3]], "d": {}, "e": []})");
    CHECK(devtools::formatJson(doc) == "{\n  \"a\": 2.0,\n  \"b\": 0.3,\n  \"c\": [\n    1,\n    [2, 3]\n  ],\n"
                                       "  \"d\": {},\n  \"e\": []\n}\n");
}

TEST(devtools_source_assets_map_data_and_source_paths) {
    devtools::SourceAssets assets;
    assets.init("/game/bin/data/");
    if (!assets.available()) {
        return; // built without the source tree definition
    }
    const std::string src = assets.sourceDirectory();
    CHECK(assets.sourceOf("/game/bin/data/world/looks/../looks/cave.look.json") ==
          src + "assets/source/world/looks/cave.look.json");
    CHECK(assets.sourceOf("/game/bin/data/shaders/light.frag.ashd") == src + "shaders/light.frag");
    CHECK(assets.sourceOf("/game/bin/data/textures/props.png") == src + "assets/source/textures/props.png");
    CHECK(!assets.sourceOf("/elsewhere/world/x.json").has_value());
    CHECK(assets.dataOf(src + "shaders/water.frag") == std::string("/game/bin/data/shaders/water.frag.ashd"));
    CHECK(assets.dataOf(src + "assets/source/world/rooms/a.tmj") == std::string("/game/bin/data/world/rooms/a.tmj"));
    CHECK(!assets.dataOf(src + "README.md").has_value());
    CHECK(assets.displayName("/game/bin/data/world/tuning.json") == "world/tuning.json");
}

// The level editor saves rooms through TiledMap: an unchanged room must come out byte-identical.
TEST(devtools_tiled_map_round_trips_the_rooms) {
    namespace fs = std::filesystem;
    int rooms = 0;
    for (const fs::directory_entry& entry :
         fs::directory_iterator(fs::path(ANIMAL_TEST_SOURCE_DIR) / "assets" / "source" / "world" / "rooms")) {
        const std::u8string u8 = entry.path().generic_u8string();
        const std::optional<std::string> text = readText(std::string(u8.begin(), u8.end()));
        CHECK(text.has_value());
        const std::optional<devtools::TiledMap> map = text ? devtools::TiledMap::parse(*text) : std::nullopt;
        CHECK(map.has_value());
        if (map) {
            CHECK(map->serialize() == *text);
            ++rooms;
        }
    }
    CHECK(rooms >= 3);
}

TEST(devtools_tiled_map_edits_tiles_objects_and_properties) {
    auto map = devtools::TiledMap::parse(R"({"type": "map", "width": 3, "height": 2, "nextobjectid": 5,
        "layers": [{"type": "tilelayer", "name": "tiles", "width": 3, "height": 2, "data": [1, 2, 3, 4, 5, 6]},
                   {"type": "objectgroup", "name": "objects", "objects": [{"id": 7, "type": "gem", "x": 1, "y": 2}]}]})");
    CHECK(map.has_value());
    if (!map) {
        return;
    }
    CHECK(map->tileLayers() == std::vector<std::string>{"tiles"});
    CHECK(map->tile("tiles", 2, 1) == 6 && map->tile("tiles", 3, 0) == 0);
    CHECK(map->setTile("tiles", 1, 1, 9) && map->tile("tiles", 1, 1) == 9);
    CHECK(!map->setTile("tiles", -1, 0, 9) && !map->setTile("other", 0, 0, 9));

    const int id = map->addObject({{"type", "light"}, {"x", 3}, {"y", 4}});
    CHECK(id == 8); // past every existing id, even when nextobjectid is behind
    CHECK(map->document()["nextobjectid"] == 9);
    CHECK(map->objectIds() == std::vector<int>({7, 8}));
    devtools::JsonDocument* light = map->object(id);
    CHECK(light && (*light)["type"] == "light");
    devtools::TiledMap::setProperty(*light, "radius", "float", 40.0);
    devtools::TiledMap::setProperty(*light, "radius", "float", 50.0);
    const devtools::JsonDocument* radius = devtools::TiledMap::property(*map->object(id), "radius");
    CHECK(radius && (*radius)["value"] == 50.0 && map->object(id)->at("properties").size() == 1);
    CHECK(devtools::TiledMap::removeProperty(*map->object(id), "radius") && !map->object(id)->contains("properties"));
    CHECK(map->removeObject(7) && !map->object(7) && !map->removeObject(7));
    CHECK(map->addObject({{"type", "gem"}}) == 9); // ids are never reused
    CHECK(!devtools::TiledMap::parse(R"({"type": "tileset"})").has_value());
}

TEST(devtools_tiled_world_round_trips_and_edits_the_layout) {
    const std::string path = std::string(ANIMAL_TEST_SOURCE_DIR) + "/assets/source/world/test.world";
    const std::optional<std::string> text = readText(path);
    CHECK(text.has_value());
    std::optional<devtools::TiledWorld> world = text ? devtools::TiledWorld::parse(*text) : std::nullopt;
    CHECK(world.has_value());
    if (!world) {
        return;
    }
    CHECK(world->serialize() == *text);
    CHECK(world->maps().size() >= 3 && world->find("rooms/cave_entrance.tmj"));
    CHECK(world->move("rooms/cave_entrance.tmj", -320, 0) && world->find("rooms/cave_entrance.tmj")->x == -320);
    CHECK(world->add({"rooms/new.tmj", 640, 0, 320, 176}) && !world->add({"rooms/new.tmj", 0, 0, 1, 1}));
    CHECK(world->find("rooms/new.tmj")->width == 320);
    CHECK(world->remove("rooms/new.tmj") && !world->find("rooms/new.tmj") && !world->remove("rooms/new.tmj"));
    std::string error;
    CHECK(!devtools::TiledWorld::parse("{}", &error) && !error.empty());
}

TEST(devtools_file_watcher_reports_settled_changes_once) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "animal_watcher_test";
    fs::remove_all(dir, cleanupError);
    fs::create_directories(dir / "sub");
    const auto write = [](const fs::path& path, const std::string& text) {
        const std::u8string u8 = path.generic_u8string();
        writeFileAtomic(std::string(u8.begin(), u8.end()), text.data(), text.size());
    };
    const auto name = [](const fs::path& path) {
        const std::u8string u8 = path.lexically_normal().generic_u8string();
        return std::string(u8.begin(), u8.end());
    };
    write(dir / "a.json", "1");
    devtools::FileWatcher watcher(0.0);
    const std::u8string root = dir.generic_u8string();
    watcher.watchDirectory(std::string(root.begin(), root.end()));
    CHECK(watcher.poll().empty()); // existing files are not changes

    write(dir / "a.json", "22");
    write(dir / "sub" / "b.json", "3");
    CHECK(watcher.poll().empty()); // seen, but not settled yet
    const std::vector<std::string> changed = watcher.poll();
    CHECK(changed.size() == 2);
    CHECK(changed.size() == 2 && changed[0] == name(dir / "a.json") && changed[1] == name(dir / "sub" / "b.json"));
    CHECK(watcher.poll().empty()); // reported once

    write(dir / "a.json", "333");
    watcher.acknowledge(name(dir / "a.json")); // written by the tools themselves
    CHECK(watcher.poll().empty() && watcher.poll().empty());

    write(dir / ".hidden", "x");
    write(dir / "c.json.tmp", "x");
    CHECK(watcher.poll().empty() && watcher.poll().empty());
    fs::remove_all(dir, cleanupError);
}

} // namespace
