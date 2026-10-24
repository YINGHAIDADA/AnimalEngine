// Swinging lanterns and thrown dynamite (ADR-0038).

#include <animal/framework/pendulum.h>
#include <animal/framework/projectile.h>
#include <animal/framework/tilemap.h>

#include "test_harness.h"

#include <cmath>

namespace {

using namespace animal;

constexpr float kDt = 1.0f / 60.0f;
constexpr IntRect kWeight{-4, -4, 8, 8};

// The largest swing after a player runs through the weight for a few steps.
float swingAfterPassing(float mass) {
    PendulumParams params;
    params.mass = mass;
    Pendulum p{{100.0f, 0.0f}, 40.0f};
    float largest = 0.0f;
    for (int i = 0; i < 240; ++i) {
        const IntRect player{static_cast<int>(80.0f + 84.0f * kDt * static_cast<float>(i)), 36, 6, 7};
        p.touch(player, {84.0f, 0.0f}, kWeight, params, kDt);
        p.step(params, kDt);
        largest = std::max(largest, std::fabs(p.angle));
    }
    return largest;
}

TEST(pendulum_hangs_still_and_swings_back) {
    PendulumParams params;
    Pendulum p{{100.0f, 0.0f}, 40.0f};
    for (int i = 0; i < 60; ++i) {
        p.step(params, kDt);
    }
    CHECK(p.angle == 0.0f && p.bob().x == 100.0f && p.bob().y == 40.0f);
    p.push({120.0f, 0.0f}, params);
    p.step(params, kDt);
    CHECK(p.angle > 0.0f && p.bobVelocity().x > 0.0f); // pushed to the right
    float lowest = 1.0f;
    for (int i = 0; i < 1200; ++i) {
        p.step(params, kDt);
        lowest = std::min(lowest, p.angle);
    }
    CHECK(lowest < 0.0f);                      // swung back past the middle
    CHECK(std::fabs(p.angle) < 0.02f);         // and settled (damping)
}

TEST(pendulum_heavy_weights_swing_less) {
    const float light = swingAfterPassing(1.0f);
    const float heavy = swingAfterPassing(4.0f);
    CHECK(heavy > 0.02f);          // a passing player does move a heavy lantern
    CHECK(heavy < light * 0.6f);   // but much less than a light one
    CHECK(light <= PendulumParams{}.maxAngle);
}

TEST(projectile_arcs_bounces_and_comes_to_rest) {
    TileMap map(20, 12, 8);
    map.setSolid(1, true);
    for (int x = 0; x < 20; ++x) {
        map.setTile(x, 11, 1);
    }
    for (int y = 0; y < 12; ++y) {
        map.setTile(19, y, 1);
    }
    Projectile p;
    p.body.position = {20, 60};
    p.body.collider = {0, 0, 4, 4};
    p.velocity = {120.0f, -150.0f};
    float highest = 60.0f;
    int bounces = 0;
    for (int i = 0; i < 600 && !p.resting; ++i) {
        bounces += p.step({}, map, kDt) ? 1 : 0;
        highest = std::min(highest, static_cast<float>(p.body.position.y));
    }
    CHECK(highest < 50.0f);                          // went up first
    CHECK(bounces >= 2);                             // off the wall and the floor
    CHECK(p.resting && p.body.worldCollider().bottom() == 88); // resting on the floor
    CHECK(p.body.worldCollider().right() <= 152);    // never through the wall
}

} // namespace
