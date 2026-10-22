#pragma once

#include <animal/core/math.h>
#include <animal/framework/physics.h>

namespace animal {

class TileMap;

struct PatrolParams {
    float speed = 18.0f;     // px/s
    float gravity = 600.0f;  // px/s²
    float maxFallSpeed = 200.0f;
    bool turnAtLedges = true; // stay on the platform instead of walking off
};

struct PatrolState {
    float direction = 1.0f; // +1 right, -1 left
    float fallSpeed = 0.0f;
    bool grounded = false;
    bool turnedThisStep = false;
};

// Ground patrol: walks until a wall or (optionally) a ledge, then turns around; falls when
// unsupported. Pixel-stepped like the player (ADR-0006).
void stepPatrol(Body& body, PatrolState& state, const PatrolParams& params, const TileMap& map, float dt);

} // namespace animal
