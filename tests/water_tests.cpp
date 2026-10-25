// Water surface waves and floating (ADR-0018).

#include <animal/framework/physics.h>
#include <animal/framework/platformer.h>
#include <animal/framework/tilemap.h>
#include <animal/framework/water.h>

#include "test_harness.h"

#include <algorithm>
#include <cmath>

namespace {

using namespace animal;

constexpr float kDt = 1.0f / 60.0f;
constexpr TileId kSolid = 1;

float maxOffset(const WaterBody& water) {
    float m = 0.0f;
    for (const float h : water.offsets()) {
        m = std::max(m, std::fabs(h));
    }
    return m;
}

TEST(water_surface_rests_spreads_and_settles) {
    WaterBody water({0, 100, 80, 24});
    for (int i = 0; i < 60; ++i) {
        water.step(kDt);
    }
    CHECK(maxOffset(water) == 0.0f);

    water.disturb(20.0f, 3.0f, 120.0f);
    water.step(kDt);
    CHECK(water.surfaceY(20.0f) > 100.0f); // pushed down where it was hit
    for (int i = 0; i < 20; ++i) {
        water.step(kDt);
    }
    CHECK(std::fabs(water.offsets()[50]) > 0.01f); // the wave travelled 30 px
    for (int i = 0; i < 600; ++i) {
        water.step(kDt);
    }
    CHECK(maxOffset(water) < 0.05f); // and died down

    water.disturb(40.0f, 4.0f, 5000.0f);
    water.step(kDt);
    CHECK(maxOffset(water) <= WaterParams{}.maxOffset);
}

TEST(water_submersion_measures_the_box_below_the_surface) {
    const WaterBody water({0, 100, 80, 24});
    CHECK(water.submersion({10, 80, 6, 10}) == 0.0f);                    // above
    CHECK(std::fabs(water.submersion({10, 95, 6, 10}) - 0.5f) < 1e-6f); // half in
    CHECK(water.submersion({10, 110, 6, 10}) == 1.0f);                   // under
    CHECK(water.submersion({100, 110, 6, 10}) == 0.0f);                  // beside the pool
}

// A pool 3 tiles deep with a floor; the body is dropped in from above.
TEST(bodies_float_in_water_and_can_jump_out) {
    TileMap map(10, 8, 8);
    map.setSolid(kSolid, true);
    for (int x = 0; x < 10; ++x) {
        map.setTile(x, 7, kSolid);
    }
    WaterBody water({0, 32, 80, 24}); // y 32..56, floor at 56
    Body body;
    body.collider = {0, 0, 6, 7};
    body.position = {30, 0};
    PlatformerState state;
    const PlatformerParams base;

    for (int i = 0; i < 240; ++i) { // 4 s
        const float s = water.submersion(body.worldCollider());
        applyWater(state, base, s, kDt);
        stepPlatformer(body, state, {}, swimmingParams(base, s, {}), map, kDt);
        water.step(kDt);
    }
    const float s = water.submersion(body.worldCollider());
    CHECK(s > 0.3f && s < 0.9f);            // floating near floatDepth
    CHECK(std::fabs(state.velocity.y) < 20.0f);
    CHECK(body.worldCollider().bottom() < 56); // not on the floor

    float lowestSubmersion = 1.0f;
    for (int i = 0; i < 30; ++i) {
        PlatformerInput input;
        input.jumpPressed = i == 0;
        input.jumpHeld = true;
        const float now = water.submersion(body.worldCollider());
        lowestSubmersion = std::min(lowestSubmersion, now);
        applyWater(state, base, now, kDt);
        stepPlatformer(body, state, input, swimmingParams(base, now, {}), map, kDt);
    }
    CHECK(lowestSubmersion == 0.0f); // cleared the surface
}

// Walls on tile columns 1 and 5, floor on row 5, and a step on tile (4, 4).
// The rectangle sits inside the basin and does not reach the walls or the floor.
TileMap basin() {
    TileMap map(8, 8, 8);
    map.setSolid(kSolid, true);
    for (int y = 0; y < 8; ++y) {
        map.setTile(1, y, kSolid);
        map.setTile(5, y, kSolid);
    }
    for (int x = 1; x <= 5; ++x) {
        map.setTile(x, 5, kSolid);
    }
    map.setTile(4, 4, kSolid); // shallower floor at x 32..39
    return map;
}

TEST(water_fits_the_open_tiles_of_a_basin) {
    const std::vector<WaterBody> pools = WaterBody::fit({20, 24, 12, 8}, basin());
    CHECK(pools.size() == 1);
    const WaterBody& water = pools[0];
    CHECK(water.area().x == 16 && water.area().right() == 40); // out to the walls
    CHECK(water.area().y == 24);
    CHECK(water.floors()[0] == 40);                             // left column, down to the floor
    CHECK(water.floors()[16] == 32);                            // x = 32, the step
    CHECK(water.tops()[0] == 24);
    CHECK(water.submersion({16, 30, 6, 8}) > 0.0f);             // in the pool
    CHECK(water.submersion({16, 40, 6, 8}) == 0.0f);            // on the floor
    CHECK(water.submersion({0, 30, 6, 8}) == 0.0f);             // outside the walls
    CHECK(water.falls().empty());                               // solid walls, nothing pours out
}

TEST(water_pours_down_back_wall_air_beside_it) {
    TileMap map(8, 8, 8);
    map.setSolid(kSolid, true);
    for (int y = 0; y < 8; ++y) {
        map.setTile(1, y, kSolid); // a solid wall on the left
    }
    for (int y = 5; y < 8; ++y) {
        for (int x = 1; x <= 5; ++x) {
            map.setTile(x, y, kSolid); // floor at y 40
        }
    }
    map.setTile(5, 3, kSolid); // only a lip on the right (y 24..31); below it is back-wall air
    const std::vector<WaterBody> pools = WaterBody::fit({20, 24, 12, 8}, map);
    CHECK(pools.size() == 1);
    CHECK(pools[0].falls().size() == 1);
    const WaterBody::Fall& fall = pools[0].falls()[0];
    CHECK(fall.x0 == 40 && fall.x1 == 41); // the pixel just outside the pool
    CHECK(fall.top == 32);                 // under the lip
    CHECK(fall.bottom == 40);              // the floor
}

// A solid shelf cuts the column, leaving a hollow beside deeper water. The hollow is back-wall
// air, so the water next to it pours down to the floor.
TEST(water_pours_into_a_hollow_beside_it) {
    TileMap map(6, 10, 8);
    map.setSolid(kSolid, true);
    for (int y = 0; y < 10; ++y) {
        map.setTile(0, y, kSolid);
        map.setTile(5, y, kSolid);
    }
    for (int x = 0; x <= 5; ++x) {
        map.setTile(x, 9, kSolid); // floor at y 72
    }
    for (int y = 4; y <= 5; ++y) {
        map.setTile(2, y, kSolid); // shelf at y 32..48 over tile column 2
    }
    const std::vector<WaterBody> pools = WaterBody::fit({8, 16, 32, 56}, map);
    CHECK(pools.size() == 1);
    const WaterBody::Fall* hollow = nullptr;
    for (const WaterBody::Fall& fall : pools[0].falls()) {
        if (fall.x0 == 16) {
            hollow = &fall;
        }
    }
    CHECK(hollow != nullptr);
    if (hollow != nullptr) {
        CHECK(hollow->x1 == 17);
        CHECK(hollow->top == 48);   // first air under the shelf
        CHECK(hollow->bottom == 72); // the floor
    }
}

TEST(water_fit_splits_on_a_wall_and_starts_under_a_ceiling) {
    TileMap map(8, 8, 8);
    map.setSolid(kSolid, true);
    for (int y = 0; y < 8; ++y) {
        map.setTile(1, y, kSolid);
        map.setTile(3, y, kSolid); // x 24..31, splits the basin
        map.setTile(5, y, kSolid);
    }
    for (int y = 5; y < 8; ++y) {
        for (int x = 1; x <= 5; ++x) {
            map.setTile(x, y, kSolid); // thick floor at y 40
        }
    }
    for (int x = 2; x <= 4; ++x) {
        map.setTile(x, 3, kSolid); // y 24..31, a ceiling on the waterline
    }
    const std::vector<WaterBody> pools = WaterBody::fit({16, 24, 24, 16}, map);
    CHECK(pools.size() == 2);
    CHECK(pools[0].area().x == 16 && pools[0].area().right() == 24);
    CHECK(pools[1].area().x == 32 && pools[1].area().right() == 40);
    CHECK(pools[0].tops()[0] == 32); // underside of the ceiling tile
    CHECK(pools[0].floors()[0] == 40);
    CHECK(pools[1].tops()[0] == 32 && pools[1].floors()[0] == 40);
}

TEST(swimming_params_scale_with_depth) {
    const PlatformerParams base;
    const PlatformerParams dry = swimmingParams(base, 0.0f);
    CHECK(dry.gravity == base.gravity && dry.runSpeed == base.runSpeed);
    const PlatformerParams floating = swimmingParams(base, 0.6f);
    CHECK(std::fabs(floating.gravity) < 1e-3f); // balanced at floatDepth
    const PlatformerParams deep = swimmingParams(base, 1.0f);
    CHECK(deep.gravity < 0.0f);                 // lift
    CHECK(deep.runSpeed < base.runSpeed && deep.maxFallSpeed < base.maxFallSpeed);
}

} // namespace
