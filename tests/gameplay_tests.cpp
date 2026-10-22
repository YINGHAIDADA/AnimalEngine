// Triggers, patrol movement and save data.

#include <animal/framework/patrol.h>
#include <animal/framework/save.h>
#include <animal/framework/tilemap.h>
#include <animal/framework/triggers.h>

#include "test_harness.h"

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

} // namespace
