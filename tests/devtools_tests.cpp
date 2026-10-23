// Developer tools that do not need a window: JSON formatting and source / data path mapping.

#include <animal/devtools/json_editor.h>
#include <animal/devtools/source_assets.h>
#include <animal/framework/level.h>

#include "test_harness.h"

#include <filesystem>
#include <string>

namespace {

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

} // namespace
