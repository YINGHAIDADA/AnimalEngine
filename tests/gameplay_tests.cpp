// Triggers, patrol movement, save data and light animation.

#include <animal/core/file.h>
#include <animal/framework/level.h>
#include <animal/framework/light_animation.h>
#include <animal/framework/patrol.h>
#include <animal/framework/save.h>
#include <animal/framework/tilemap.h>
#include <animal/framework/triggers.h>

#include "test_harness.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>
#include <string>

namespace {

using namespace animal;

constexpr float kDt = 1.0f / 60.0f;
constexpr TileId kSolid = 1;

TEST(trigger_fires_on_entry_and_rearms_after_leaving) {
    TriggerSet triggers;
    triggers.add({1, {40, 0, 16, 16}, false, "hello"});
    triggers.add({2, {80, 0, 16, 16}, true, "once"});

    CHECK(triggers.update({0, 4, 6, 7}).empty());  // outside
    const auto entered = triggers.update({38, 4, 6, 7});
    CHECK(entered.size() == 1 && entered[0]->message == "hello");
    CHECK(triggers.update({42, 4, 6, 7}).empty()); // still inside: no repeat
    CHECK(triggers.update({0, 4, 6, 7}).empty());  // left
    CHECK(triggers.update({42, 4, 6, 7}).size() == 1); // re-entered: fires again

    CHECK(triggers.update({82, 4, 6, 7}).size() == 1); // once
    triggers.update({0, 4, 6, 7});
    CHECK(triggers.update({82, 4, 6, 7}).empty());     // never again
}

// Floor row 10 from x = 0..9 tiles (ledge at x = 80), wall at tile 0.
TileMap patrolMap() {
    TileMap map(20, 12, 8);
    map.setSolid(kSolid, true);
    for (int x = 0; x < 10; ++x) {
        map.setTile(x, 10, kSolid);
    }
    for (int y = 0; y < 12; ++y) {
        map.setTile(0, y, kSolid);
    }
    return map;
}

Body crawlerAt(int x) {
    Body body;
    body.position = {x, 80 - 5};
    body.collider = {0, 0, 7, 5};
    return body;
}

TEST(patrol_turns_at_ledges_and_walls) {
    const TileMap map = patrolMap();
    Body body = crawlerAt(40);
    PatrolState state;
    const PatrolParams params;
    int minX = body.position.x;
    int maxX = body.position.x;
    int turns = 0;
    for (int i = 0; i < 60 * 12; ++i) {
        stepPatrol(body, state, params, map, kDt);
        minX = std::min(minX, body.position.x);
        maxX = std::max(maxX, body.position.x);
        turns += state.turnedThisStep;
        CHECK(state.grounded);
    }
    CHECK(maxX == 80 - 7); // stopped at the ledge (right edge of the floor)
    CHECK(minX == 8);      // stopped at the wall
    CHECK(turns >= 2);
}

TEST(patrol_can_walk_off_ledges_and_falls) {
    const TileMap map = patrolMap();
    Body body = crawlerAt(70);
    PatrolState state;
    PatrolParams params;
    params.turnAtLedges = false;
    bool fell = false;
    for (int i = 0; i < 120; ++i) {
        stepPatrol(body, state, params, map, kDt);
        fell = fell || body.position.y > 80;
    }
    CHECK(fell);
}

TEST(save_round_trips) {
    SaveData save;
    save.room = "crystal_gallery";
    save.position = {12.0f, 160.0f};
    save.flags = {"cave_entrance:3", "smoke_pit:7"};
    std::string error;
    const auto parsed = parseSave(serializeSave(save), &error);
    CHECK(parsed.has_value());
    if (parsed) {
        CHECK(parsed->room == save.room);
        CHECK(parsed->position == save.position);
        CHECK(parsed->flags == save.flags);
    }
}

TEST(save_rejects_foreign_old_and_malformed_data) {
    std::string error;
    CHECK(!parseSave(R"({"format": "other", "version": 1})", &error));
    CHECK(error.find("not an Animal Engine save") != std::string::npos);
    CHECK(!parseSave(R"({"format": "animal-save", "version": 99, "room": "a", "position": [0, 0], "flags": []})", &error));
    CHECK(error.find("version 99") != std::string::npos);
    CHECK(!parseSave(R"({"format": "animal-save", "version": 1, "room": "a", "position": [0], "flags": []})", &error));
    CHECK(!parseSave(R"({"format": "animal-save", "version": 1, "position": [0, 0], "flags": []})", &error));
    CHECK(error.find("malformed") != std::string::npos);
    CHECK(!parseSave("{ truncated", &error));
}

TEST(save_migrates_version_1_and_keeps_new_fields) {
    int version = 0;
    const auto old = parseSave(R"({"format": "animal-save", "version": 1, "room": "smoke_pit", "position": [3, 4],
        "flags": ["smoke_pit:10"]})", nullptr, &version);
    CHECK(old && version == 1 && old->visited == std::set<std::string>{"smoke_pit"} && old->playSeconds == 0.0);
    SaveData save;
    save.room = "a";
    save.visited = {"a", "b"};
    save.playSeconds = 12.5;
    const auto parsed = parseSave(serializeSave(save));
    CHECK(parsed && parsed->visited == save.visited && parsed->playSeconds == 12.5);
}

TEST(save_keeps_a_backup_and_recovers_from_damage) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "animal_save_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const std::string path = (dir / "save.json").generic_string();
    CHECK(loadSave(path).status == SaveLoad::Status::Missing);

    SaveData first;
    first.room = "cave_entrance";
    first.flags = {"cave_entrance:15"};
    CHECK(writeSave(path, first));
    CHECK(!fileExists(path + ".bak")); // nothing to keep yet
    SaveData second = first;
    second.flags.insert("crystal_gallery:7");
    CHECK(writeSave(path, second));
    CHECK(fileExists(path + ".bak"));
    CHECK(loadSave(path).status == SaveLoad::Status::Loaded && loadSave(path).data->flags.size() == 2);

    // A damaged save: the backup (the previous good save) is used, the damaged file kept.
    CHECK(writeFileAtomic(path, "{ damaged", 9));
    const SaveLoad recovered = loadSave(path);
    CHECK(recovered.status == SaveLoad::Status::Recovered && recovered.data->flags.size() == 1);
    CHECK(fileExists(path + ".corrupt") && !fileExists(path));
    // Writing again must not replace the good backup with anything damaged.
    CHECK(writeSave(path, second));
    CHECK(loadSave(path).data->flags.size() == 2);

    // Both damaged: a new game, nothing crashes.
    CHECK(writeFileAtomic(path, "x", 1));
    CHECK(writeFileAtomic(path + ".bak", "y", 1));
    CHECK(loadSave(path).status == SaveLoad::Status::Unusable);

    // From a newer game: reported and left alone.
    const std::string newer = R"({"format": "animal-save", "version": 99, "room": "a", "position": [0, 0], "flags": []})";
    CHECK(writeFileAtomic(path, newer.data(), newer.size()));
    CHECK(loadSave(path).status == SaveLoad::Status::TooNew);
    CHECK(readText(path) == newer);
    fs::remove_all(dir, ec);
}

TEST(light_animation_stays_within_its_amount) {
    const LightAnimation none;
    CHECK(none.factor(3.7, 1.0f) == 1.0f);

    for (const LightAnimation::Mode mode : {LightAnimation::Mode::Flicker, LightAnimation::Mode::Pulse}) {
        const LightAnimation animation{mode, 1.3f, 0.2f};
        float lowest = 2.0f;
        float highest = 0.0f;
        for (int i = 0; i < 600; ++i) {
            const float f = animation.factor(i / 60.0, 0.5f);
            lowest = std::min(lowest, f);
            highest = std::max(highest, f);
        }
        CHECK(lowest >= 0.8f - 1e-5f && highest <= 1.2f + 1e-5f);
        CHECK(highest - lowest > 0.2f); // it does move
        CHECK(animation.factor(2.25, 0.5f) == animation.factor(2.25, 0.5f));
        CHECK(animation.factor(2.25, 0.5f) != animation.factor(2.25, 2.0f)); // phase decorrelates lights
    }

    const LightAnimation pulse{LightAnimation::Mode::Pulse, 0.5f, 0.1f};
    CHECK(std::fabs(pulse.factor(0.5, 0.0f) - 1.1f) < 1e-5f); // a quarter of a 2 s cycle: the peak
    CHECK(std::fabs(pulse.factor(2.5, 0.0f) - 1.1f) < 1e-4f); // one cycle later

    const LightAnimation strong{LightAnimation::Mode::Pulse, 1.0f, 3.0f};
    CHECK(strong.factor(0.75, 0.0f) == 0.0f); // never negative
}

TEST(light_animation_names_parse) {
    CHECK(parseLightAnimationMode("pulse") == LightAnimation::Mode::Pulse);
    CHECK(parseLightAnimationMode("flicker") == LightAnimation::Mode::Flicker);
    CHECK(parseLightAnimationMode("none") == LightAnimation::Mode::None);
    CHECK(parseLightAnimationMode("") == LightAnimation::Mode::None);
    CHECK(!parseLightAnimationMode("strobe"));
}

} // namespace
