// String tables and the UI font (ADR-0041).

#include <animal/framework/level.h>
#include <animal/framework/strings.h>
#include <animal/resources/bitmap_font.h>

#include "test_harness.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace animal;

void expect(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "  CHECK failed: %s\n", what.c_str());
        ++::animal::test::failures();
    }
}

StringTable table(const std::string& json) {
    std::string error;
    auto parsed = parseStringTable(json, "strings/x.strings.json", &error);
    CHECK(parsed.has_value());
    return parsed ? *parsed : StringTable{};
}

TEST(string_table_parses_and_rejects_bad_files) {
    const StringTable zh = table(R"({"format": "animal-strings", "version": 1, "language": "zh-Hans",
        "name": "简体中文", "strings": {"menu.back": "返回"}})");
    CHECK(zh.language == "zh-Hans" && zh.name == "简体中文" && zh.strings.at("menu.back") == "返回");

    std::string error;
    CHECK(!parseStringTable(R"({"format": "animal-strings", "version": 1, "language": "en", "name": "English",
        "strings": {"menu.back": 3}})", "en.strings.json", &error));
    CHECK(error.find("menu.back") != std::string::npos);
    CHECK(!parseStringTable(R"({"format": "animal-strings", "version": 1, "strings": {}})", "en.strings.json", &error));
    CHECK(!parseStringTable(R"({"format": "animal-look", "version": 1})", "en.strings.json", &error));
}

TEST(strings_fall_back_to_the_second_table_then_the_key) {
    Strings strings;
    strings.set(table(R"({"format": "animal-strings", "version": 1, "language": "zh-Hans", "name": "简体中文",
                         "strings": {"a": "甲"}})"),
                table(R"({"format": "animal-strings", "version": 1, "language": "en", "name": "English",
                         "strings": {"a": "A", "b": "B"}})"));
    CHECK(strings.get("a") == "甲" && strings.get("b") == "B" && strings.get("c") == "c");
    CHECK(strings.has("b") && !strings.has("c"));
    CHECK(strings.language() == "zh-Hans" && strings.name() == "简体中文");
}

TEST(text_arguments_replace_numbered_braces_only) {
    const std::vector<std::string> args = {"14", "3"};
    CHECK(formatText("需要 {0} 颗（已找到 {1} 颗）", args) == "需要 14 颗（已找到 3 颗）");
    CHECK(formatText("{1}/{0} {2} {x} {}", args) == "3/14 {2} {x} {}");
}

// The test data's tables and levels: same keys in every language, level messages that are keys exist,
// and every character the player can read is in the UI font.
TEST(test_data_text_is_complete_and_in_the_font) {
    const std::string data = std::string(ANIMAL_TEST_ASSETS_DIR) + "/";
    const auto zh = loadStringTable(data + "strings/zh-Hans.strings.json");
    const auto en = loadStringTable(data + "strings/en.strings.json");
    const auto fontText = readText(data + "fonts/ui.font.json");
    CHECK(zh && en && fontText);
    if (!zh || !en || !fontText) {
        return;
    }
    for (const auto& [key, value] : zh->strings) {
        expect(en->strings.contains(key), "en lacks " + key);
    }
    for (const auto& [key, value] : en->strings) {
        expect(zh->strings.contains(key), "zh-Hans lacks " + key);
    }

    const auto desc = parseBitmapFont(*fontText, data + "fonts/ui.font.json", nullptr);
    CHECK(desc.has_value());
    const BitmapFont font(*desc, {});
    std::vector<std::string> shown = {zh->name, en->name};
    for (const StringTable* t : {&*zh, &*en}) {
        for (const auto& [key, value] : t->strings) {
            shown.push_back(value);
        }
    }
    const auto world = loadWorld(data + "world/test.world");
    CHECK(world.has_value());
    for (const WorldRoom& worldRoom : world ? world->rooms : std::vector<WorldRoom>{}) {
        const auto room = loadRoom(worldRoom.path);
        CHECK(room.has_value());
        for (const LevelObject& object : room ? room->objects : std::vector<LevelObject>{}) {
            const std::string message = object.properties.getString("message");
            if (message.starts_with("level.") || message.starts_with("message.")) {
                expect(zh->strings.contains(message), worldRoom.path + ": no text for " + message);
            } else if (!message.empty()) {
                shown.push_back(message);
            }
        }
    }
    for (const std::string& s : shown) {
        expect(font.missing(s).empty(), "not in the font: " + s);
    }
}

} // namespace
