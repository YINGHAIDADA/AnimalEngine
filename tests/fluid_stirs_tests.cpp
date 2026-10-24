// Moving bodies stirring the smoke (ADR-0008).

#include <animal/framework/fluid_stirs.h>

#include "test_harness.h"

#include <vector>

namespace {

using namespace animal;

// A vertical vine of `count` points 4 px apart; point i moved by `moved(i)` px sideways in the last
// substep of 1/240 s.
VineSystem::Vine vine(size_t count, float (*moved)(size_t)) {
    VineSystem::Vine v;
    for (size_t i = 0; i < count; ++i) {
        const Vec2 p{100.0f, 4.0f * static_cast<float>(i)};
        v.points.push_back(p);
        v.previous.push_back(p - Vec2{moved(i), 0.0f});
    }
    return v;
}

constexpr float kSubstep = 1.0f / 240.0f;

TEST(stirs_push_with_the_velocity_of_moving_bodies_only) {
    const std::vector<StirBody> bodies = {{{10.0f, 20.0f}, {60.0f, -30.0f}, 7.0f},
                                          {{40.0f, 20.0f}, {5.0f, 0.0f}, 6.0f}}; // settling: below 8 px/s
    StirParams params;
    params.coupling = 2.0f;
    const auto splats = fluidStirs(bodies, {}, kSubstep, params, 32);
    CHECK(splats.size() == 1);
    CHECK(splats[0].position == Vec2(10.0f, 20.0f) && splats[0].radius == 7.0f);
    CHECK(splats[0].force == Vec2(120.0f, -60.0f) && splats[0].smoke == 0.0f);

    params.coupling = 0.0f;
    CHECK(fluidStirs(bodies, {}, kSubstep, params, 32).empty());
    CHECK(fluidStirs(bodies, {}, kSubstep, StirParams{}, 0).empty());
}

TEST(stirs_keep_the_fastest_bodies_then_vines_within_the_budget) {
    std::vector<StirBody> bodies;
    for (int i = 1; i <= 5; ++i) {
        bodies.push_back({{static_cast<float>(i), 0.0f}, {10.0f * static_cast<float>(i), 0.0f}, 6.0f});
    }
    const std::vector<VineSystem::Vine> vines = {vine(10, [](size_t) { return 1.0f; })}; // 240 px/s
    const auto three = fluidStirs(bodies, vines, kSubstep, StirParams{}, 3);
    CHECK(three.size() == 3); // bodies first, even though the vine is faster
    CHECK(three[0].position.x == 5.0f && three[1].position.x == 4.0f && three[2].position.x == 3.0f);
    const auto all = fluidStirs(bodies, vines, kSubstep, StirParams{}, 32);
    CHECK(all.size() == 5 + 3); // 36 px of vine: a sample per 12 px
    CHECK(all[5].radius == StirParams{}.vineRadius);
}

TEST(vine_samples_take_the_fastest_point_of_each_stretch) {
    // 9 points, 32 px: stretches end at points 3 and 6, the rest forms the last one. Point 2 is the
    // fastest of the first stretch; points 4..5 do not move; point 8 moves.
    const std::vector<VineSystem::Vine> vines = {vine(9, [](size_t i) {
        return i == 2 ? 0.5f : (i == 1 || i == 3 ? 0.2f : (i == 8 ? 0.1f : 0.0f));
    })};
    const auto splats = fluidStirs({}, vines, kSubstep, StirParams{}, 32);
    CHECK(splats.size() == 2); // the still middle stretch gets none
    CHECK(splats[0].position == Vec2(100.0f, 8.0f));
    CHECK(test::near(splats[0].force.x, 0.5f * 240.0f * StirParams{}.coupling, 0.01f));
    CHECK(splats[1].position == Vec2(100.0f, 32.0f));
    CHECK(fluidStirs({}, vines, 0.0f, StirParams{}, 32).empty()); // no step yet: no velocities
}

} // namespace
