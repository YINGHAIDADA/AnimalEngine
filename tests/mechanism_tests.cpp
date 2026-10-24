// Hazard tiles, moving solids, diving and currents (ADR-0034).

#include <animal/framework/physics.h>
#include <animal/framework/platformer.h>
#include <animal/framework/platforms.h>
#include <animal/framework/tilemap.h>
#include <animal/framework/water.h>

#include "test_harness.h"

#include <cmath>

namespace {

using namespace animal;

constexpr float kDt = 1.0f / 60.0f;
constexpr TileId kSolid = 1;
constexpr TileId kSpikesUp = 5;
constexpr TileId kSpikesDown = 6;

// A 20×12 room of 8 px tiles with a floor on row 11.
TileMap room() {
    TileMap map(20, 12, 8);
    map.setSolid(kSolid, true);
    map.setHazard(kSpikesUp, Hazard::Up);
    map.setHazard(kSpikesDown, Hazard::Down);
    for (int x = 0; x < 20; ++x) {
        map.setTile(x, 11, kSolid);
    }
    return map;
}

Body player(Vec2i position) {
    Body body;
    body.position = position;
    body.collider = {0, 0, 6, 7};
    return body;
}

TEST(hazard_tiles_hurt_only_in_their_pointed_half) {
    TileMap map = room();
    map.setTile(5, 10, kSpikesUp);   // floor spikes: pixels y 84..87 hurt
    map.setTile(8, 2, kSpikesDown);  // ceiling spikes: pixels y 16..19 hurt
    CHECK(map.overlapsHazard({41, 84, 6, 7}));
    CHECK(!map.overlapsHazard({41, 76, 6, 7})); // standing just above the tips
    CHECK(!map.overlapsHazard({34, 84, 6, 7})); // one pixel of slack at the side (tile starts at 40)
    CHECK(map.overlapsHazard({65, 19, 6, 7}));
    CHECK(!map.overlapsHazard({65, 20, 6, 7}));
    CHECK(!map.overlapsSolid({41, 84, 6, 2})); // hazards are not solid
}

TEST(dynamic_solids_block_like_tiles_until_removed) {
    TileMap map = room();
    map.setDynamicSolids({{40, 60, 16, 8}, {}});
    Body body = player({44, 40});
    for (int i = 0; i < 30; ++i) {
        moveY(body, 3.0f, map);
    }
    CHECK(body.worldCollider().bottom() == 60); // landed on the solid
    CHECK(isGrounded(body, map));
    map.dynamicSolids()[0] = {};
    CHECK(!isGrounded(body, map));
}

TEST(moving_platform_carries_its_rider_and_pushes_what_is_in_the_way) {
    TileMap map = room();
    PlatformSystem platforms;
    platforms.add({{16, 60, 16, 8}, {64.0f, 0.0f}, 60.0f, 0.0f, 0.0f, false});
    platforms.publish(map);
    Body rider = player({20, 53}); // standing on top
    CHECK(isGrounded(rider, map));
    for (int i = 0; i < 30; ++i) {
        const PlatformSystem::StepResult r = platforms.step(kDt, map, &rider, 200);
        CHECK(!r.crushed && r.riding);
    }
    const IntRect& rect = platforms.platforms()[0].rect;
    CHECK(rect.x == 46);
    const Vec2 velocity = platforms.platforms()[0].velocity; // along its path at its speed
    CHECK(std::fabs(velocity.x - 60.0f) < 0.01f && velocity.y == 0.0f);
    CHECK(rider.position.x == 50 && rider.worldCollider().bottom() == rect.y); // moved with it

    // A body beside the path is pushed along; against a wall it is crushed.
    PlatformSystem pusher;
    pusher.add({{0, 80, 8, 8}, {80.0f, 0.0f}, 120.0f, 0.0f, 0.0f, false});
    pusher.publish(map);
    Body standing = player({20, 81});
    bool crushed = false;
    map.setTile(4, 10, kSolid); // wall at x 32..39 on the body's row
    for (int i = 0; i < 30 && !crushed; ++i) {
        crushed = pusher.step(kDt, map, &standing, 200).crushed;
    }
    CHECK(crushed);
    CHECK(standing.worldCollider().right() == 32); // pushed flush against the wall first
}

TEST(falling_platform_shakes_falls_and_comes_back) {
    TileMap map = room();
    PlatformSystem platforms;
    platforms.add({{40, 40, 16, 8}, {}, 0.0f, 0.0f, 0.0f, true});
    platforms.publish(map);
    Body rider = player({44, 33});
    platforms.step(kDt, map, &rider, 96);
    CHECK(platforms.platforms()[0].state == PlatformSystem::State::Shaking);
    CHECK(platforms.platforms()[0].velocity == Vec2{}); // shaking is drawn, not moved
    for (int i = 0; i < 28; ++i) { // 0.45 s of shaking
        platforms.step(kDt, map, &rider, 96);
    }
    CHECK(platforms.platforms()[0].state == PlatformSystem::State::Falling);
    Body away = player({120, 81});
    platforms.step(kDt, map, &away, 96);
    platforms.step(kDt, map, &away, 96);
    CHECK(platforms.platforms()[0].velocity.y > 0.0f && platforms.platforms()[0].velocity.x == 0.0f);
    for (int i = 0; i < 120 && platforms.platforms()[0].state != PlatformSystem::State::Gone; ++i) {
        platforms.step(kDt, map, &away, 96);
    }
    CHECK(platforms.platforms()[0].state == PlatformSystem::State::Gone);
    platforms.step(kDt, map, &away, 96);
    CHECK(platforms.platforms()[0].velocity == Vec2{});
    CHECK(map.dynamicSolids()[0].width == 0); // no longer solid
    for (int i = 0; i < 160; ++i) {
        platforms.step(kDt, map, &away, 96);
    }
    CHECK(platforms.platforms()[0].state == PlatformSystem::State::Resting);
    CHECK(map.dynamicSolids()[0] == IntRect(40, 40, 16, 8));
}

TEST(doors_are_solid_until_opened) {
    TileMap map = room();
    PlatformSystem doors;
    PlatformDesc closed;
    closed.rect = {80, 64, 8, 24};
    const size_t door = doors.add(closed);
    doors.publish(map);
    Body body = player({60, 81});
    for (int i = 0; i < 60; ++i) {
        moveX(body, 2.0f, map);
    }
    CHECK(body.worldCollider().right() == 80);
    doors.setSolid(door, false);
    doors.publish(map);
    for (int i = 0; i < 60; ++i) {
        moveX(body, 2.0f, map);
    }
    CHECK(body.position.x > 90);
}

TEST(diving_swims_in_both_axes_and_drifts_up_when_idle) {
    TileMap map = room();
    Body body = player({16, 8});
    PlatformerState state;
    DiveParams params;
    for (int i = 0; i < 40; ++i) {
        stepDive(body, state, {1.0f, 1.0f}, params, map, kDt);
    }
    CHECK(body.position.x > 40 && body.position.y > 32 && body.position.y < 81); // diagonal, clear of the floor
    CHECK(std::fabs(state.velocity.x - state.velocity.y) < 1e-3f);
    CHECK(std::hypot(state.velocity.x, state.velocity.y) <= params.speed + 1e-3f);
    const int y = body.position.y;
    for (int i = 0; i < 120; ++i) {
        stepDive(body, state, {}, params, map, kDt);
    }
    CHECK(body.position.y < y && std::fabs(state.velocity.y + params.drift) < 1e-3f);
}

TEST(currents_carry_bodies_by_how_deep_they_are) {
    TileMap map = room();
    Body deep = player({40, 40});
    Body shallow = player({40, 40});
    for (int i = 0; i < 60; ++i) {
        applyCurrent(deep, {30.0f, 0.0f}, 1.0f, map, kDt);
        applyCurrent(shallow, {30.0f, 0.0f}, 0.5f, map, kDt);
    }
    CHECK(deep.position.x == 70 && shallow.position.x == 55);
    Body dry = player({40, 40});
    applyCurrent(dry, {30.0f, 0.0f}, 0.0f, map, kDt);
    CHECK(dry.position.x == 40);
}

} // namespace
