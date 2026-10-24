#pragma once

// Thrown things (ADR-0038): a body under gravity that bounces off solid tiles, losing speed, and
// comes to rest on the ground.

#include <animal/core/math.h>
#include <animal/framework/physics.h>

namespace animal {

class TileMap;

struct ProjectileParams {
    float gravity = 520.0f;     // px/s²
    float maxFallSpeed = 260.0f;
    float bounce = 0.4f;        // share of the speed kept by a bounce
    float restSpeed = 40.0f;    // px/s: slower landings stop bouncing
    float groundFriction = 6.0f; // 1/s while sliding on the ground
};

struct Projectile {
    Body body;
    Vec2 velocity;
    bool resting = false;

    // One fixed step; returns true when it hit something hard enough to be heard.
    bool step(const ProjectileParams& params, const TileMap& map, float dt);
};

} // namespace animal
