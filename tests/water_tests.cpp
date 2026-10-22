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
