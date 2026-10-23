// Asset Cooker (ADR-0031): the real assets cook deterministically into a complete pack; broken
// references fail the cook.

#include <animal/core/file.h>

#include "cooker.h"
#include "test_harness.h"

#include <algorithm>
#include <filesystem>
#include <string>

namespace {

using namespace animal;
namespace fs = std::filesystem;

cooker::Options realAssets() {
    cooker::Options o;
    o.manifest = std::string(ANIMAL_TEST_SOURCE_DIR) + "/assets/cook.json";
    o.sourceRoot = std::string(ANIMAL_TEST_SOURCE_DIR) + "/assets/source";
    o.shaderDirectory = ANIMAL_TEST_SHADER_DIR;
    o.level = 3; // fast; determinism does not depend on the level
    return o;
}

bool hasEntry(const cooker::Result& r, const std::string& path) {
    return std::ranges::any_of(r.entries, [&](const auto& e) { return e.first == path; });
}

TEST(cooker_packs_the_real_assets_deterministically) {
    const cooker::Result a = cooker::cook(realAssets());
    for (const std::string& e : a.errors) {
        std::fprintf(stderr, "  cook error: %s\n", e.c_str());
    }
    CHECK(a.ok);
    const cooker::Result b = cooker::cook(realAssets());
    CHECK(a.pack == b.pack); // same input, same bytes
    CHECK(hasEntry(a, "world/test.world") && hasEntry(a, "world/rooms/cave_entrance.tmj") &&
          hasEntry(a, "world/tilesets/cave.tsj") && hasEntry(a, "world/looks/cave.look.json") &&
          hasEntry(a, "shaders/sprite_lit.frag.ashd") && hasEntry(a, "textures/atlas.json") &&
          hasEntry(a, "textures/atlas_n.tex") && hasEntry(a, "textures/creature.json"));
    CHECK(!hasEntry(a, "shaders/imgui.vert.ashd") && !hasEntry(a, "textures/tileset.png"));
    CHECK(a.atlas.materials == 4 && a.atlas.width > 0);
    CHECK(a.pack.size() < a.categories.at("world").sourceBytes + a.categories.at("shaders").sourceBytes);
    CHECK(std::ranges::find(a.unreferenced, "shaders/imgui.vert.ashd") != a.unreferenced.end());

    // The written pack loads like the game would read it.
    const fs::path dir = fs::temp_directory_path() / "animal_cook_test";
    fs::create_directories(dir);
    const std::string pak = (dir / "data.pak").generic_string();
    CHECK(writeFileAtomic(pak, a.pack.data(), a.pack.size()));
    const auto problems = cooker::verifyPack(pak, realAssets().manifest);
    for (const std::string& p : problems) {
        std::fprintf(stderr, "  verify: %s\n", p.c_str());
    }
    CHECK(problems.empty());
    fs::remove_all(dir);
}

TEST(cooker_reports_missing_references_and_invalid_data) {
    const fs::path dir = fs::temp_directory_path() / "animal_cook_broken";
    fs::remove_all(dir);
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
    fs::remove_all(dir);
}

} // namespace
