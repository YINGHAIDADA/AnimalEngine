// Asset Cooker (ADR-0031): the test data (tests/data) cooks deterministically into a complete pack; broken
// references fail the cook.

#include <animal/core/file.h>
#include <animal/resources/loaders.h>

#include "cooker.h"
#include "test_harness.h"

#include <algorithm>
#include <filesystem>
#include <string>

namespace {

// Cleanup must not throw (Windows refuses to delete files that are still open).
std::error_code cleanupError;

using namespace animal;
namespace fs = std::filesystem;

cooker::Options testData() {
    cooker::Options o;
    o.manifest = std::string(ANIMAL_TEST_ASSETS_DIR) + "/cook.json";
    o.sourceRoot = ANIMAL_TEST_ASSETS_DIR;
    o.shaderDirectory = ANIMAL_TEST_SHADER_DIR;
    o.level = 3; // fast; determinism does not depend on the level
    return o;
}

bool hasEntry(const cooker::Result& r, const std::string& path) {
    return std::ranges::any_of(r.entries, [&](const auto& e) { return e.first == path; });
}

TEST(cooker_packs_the_test_data_deterministically) {
    const cooker::Result a = cooker::cook(testData());
    for (const std::string& e : a.errors) {
        std::fprintf(stderr, "  cook error: %s\n", e.c_str());
    }
    CHECK(a.ok);
    const cooker::Result b = cooker::cook(testData());
    CHECK(a.pack == b.pack); // same input, same bytes
    CHECK(hasEntry(a, "world/test.world") && hasEntry(a, "world/rooms/render_entrance.tmj") &&
          hasEntry(a, "world/tilesets/cave.tsj") && hasEntry(a, "world/looks/cave.look.json") &&
          hasEntry(a, "shaders/sprite_lit.frag.ashd") && hasEntry(a, "textures/atlas.json") &&
          hasEntry(a, "textures/atlas_n.tex") && hasEntry(a, "textures/creature.json"));
    CHECK(!hasEntry(a, "shaders/imgui.vert.ashd") && !hasEntry(a, "textures/tileset.png"));
    CHECK(a.atlas.materials == 4 && a.atlas.width > 0);
    CHECK(a.categories.at("world").cookedBytes < a.categories.at("world").sourceBytes);
    CHECK(a.pack.size() < a.categories.at("world").sourceBytes + a.categories.at("shaders").sourceBytes +
                              a.categories.at("sounds").sourceBytes);
    CHECK(hasEntry(a, "sounds/gem.wav") && hasEntry(a, "sounds/cave_ambience.wav"));
    CHECK(hasEntry(a, "fonts/ui.font.json") && hasEntry(a, "fonts/ui.png") &&
          hasEntry(a, "strings/zh-Hans.strings.json") && hasEntry(a, "strings/en.strings.json"));
    CHECK(std::ranges::find(a.unreferenced, "shaders/conformance.vert.ashd") != a.unreferenced.end());

    // The written pack loads like the game would read it.
    const fs::path dir = fs::temp_directory_path() / "animal_cook_test";
    fs::create_directories(dir);
    const std::string pak = (dir / "data.pak").generic_string();
    CHECK(writeFileAtomic(pak, a.pack.data(), a.pack.size()));
    const auto problems = cooker::verifyPack(pak, testData().manifest);
    for (const std::string& p : problems) {
        std::fprintf(stderr, "  verify: %s\n", p.c_str());
    }
    CHECK(problems.empty());
    fs::remove_all(dir, cleanupError);
}

TEST(cooker_reports_missing_references_and_invalid_data) {
    const fs::path dir = fs::temp_directory_path() / "animal_cook_broken";
    fs::remove_all(dir, cleanupError);
    fs::create_directories(dir / "source" / "world" / "looks");
    const auto write = [](const fs::path& p, const std::string& text) {
        writeFileAtomic(p.generic_string(), text.data(), text.size());
    };
    write(dir / "source" / "world" / "looks" / "a.look.json",
          R"({"format": "animal-look", "version": 1, "base": "missing.look.json"})");
    write(dir / "source" / "world" / "looks" / "b.look.json", R"({"format": "animal-look", "version": 1, "bands": "x"})");
    write(dir / "source" / "world" / "notes.txt", "not referenced");
    write(dir / "cook.json", R"({"format": "animal-cook", "version": 1, "dataVersion": "t",
        "files": ["world/looks/a.look.json", "world/looks/b.look.json", "world/looks/gone.look.json"],
        "materials": [], "shaders": ["nope.frag"]})");
    cooker::Options o;
    o.manifest = (dir / "cook.json").generic_string();
    o.sourceRoot = (dir / "source").generic_string();
    o.shaderDirectory = (dir / "shaders").generic_string();
    const cooker::Result r = cooker::cook(o);
    CHECK(!r.ok);
    const auto mentions = [&](const std::string& text) {
        return std::ranges::any_of(r.errors, [&](const std::string& e) { return e.find(text) != std::string::npos; });
    };
    CHECK(mentions("gone.look.json")); // listed in the manifest, missing
    CHECK(mentions("missing.look.json")); // base of a.look.json
    CHECK(mentions("b.look.json")); // invalid field
    CHECK(mentions("nope.frag"));
    CHECK(std::ranges::find(r.unreferenced, "world/notes.txt") != r.unreferenced.end());
    fs::remove_all(dir, cleanupError);
}

// ADR-0041: text the font cannot show fails the cook; a key one language lacks is a warning.
TEST(cooker_checks_text_against_the_font) {
    const fs::path dir = fs::temp_directory_path() / "animal_cook_text";
    fs::remove_all(dir, cleanupError);
    fs::create_directories(dir / "source" / "fonts");
    fs::create_directories(dir / "source" / "strings");
    const auto write = [](const fs::path& p, const std::string& text) {
        writeFileAtomic(p.generic_string(), text.data(), text.size());
    };
    const std::vector<uint8_t> sheet(24 * 16 * 4, 255);
    CHECK(writePng((dir / "source" / "fonts" / "ui.png").generic_string(), 24, 16, sheet.data()));
    write(dir / "source" / "fonts" / "ui.font.json", R"({"format": "animal-font", "version": 1, "image": "ui.png",
        "cellWidth": 12, "cellHeight": 16, "baseline": 13, "columns": 2, "characters": "中", "advances": [12, 12]})");
    write(dir / "source" / "strings" / "zh-Hans.strings.json", R"({"format": "animal-strings", "version": 1,
        "language": "zh-Hans", "name": "中", "strings": {"a": "中文", "b": "中"}})");
    write(dir / "source" / "strings" / "en.strings.json", R"({"format": "animal-strings", "version": 1,
        "language": "en", "name": "中", "strings": {"a": "中"}})");
    write(dir / "cook.json", R"({"format": "animal-cook", "version": 1, "dataVersion": "t",
        "files": ["fonts/ui.font.json", "strings/zh-Hans.strings.json", "strings/en.strings.json"],
        "materials": [], "shaders": []})");
    cooker::Options o;
    o.manifest = (dir / "cook.json").generic_string();
    o.sourceRoot = (dir / "source").generic_string();
    o.shaderDirectory = (dir / "shaders").generic_string();
    const cooker::Result r = cooker::cook(o);
    CHECK(!r.ok);
    CHECK(r.errors.size() == 1 && r.errors[0].find("U+6587") != std::string::npos); // 文
    CHECK(std::ranges::any_of(r.warnings, [](const std::string& w) { return w.find("en: no text for b") != std::string::npos; }));
    fs::remove_all(dir, cleanupError);
}

} // namespace
