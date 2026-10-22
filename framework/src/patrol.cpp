#include <animal/framework/patrol.h>
#include <animal/framework/tilemap.h>

#include <algorithm>

namespace animal {

void stepPatrol(Body& body, PatrolState& state, const PatrolParams& params, const TileMap& map, float dt) {
    state.turnedThisStep = false;
    state.grounded = isGrounded(body, map);

    if (state.grounded) {
        state.fallSpeed = 0.0f;
        // Turn before stepping off a ledge: no ground under the collider's leading edge.
        if (params.turnAtLedges) {
            const IntRect c = body.worldCollider();
            const int leadX = state.direction > 0.0f ? c.right() : c.x - 1;
            if (!map.overlapsSolid({leadX, c.bottom(), 1, 1})) {
                state.direction = -state.direction;
                state.turnedThisStep = true;
            }
        }
        if (moveX(body, state.direction * params.speed * dt, map)) {
            state.direction = -state.direction;
            state.turnedThisStep = true;
        }
    } else {
        state.fallSpeed = std::min(state.fallSpeed + params.gravity * dt, params.maxFallSpeed);
    }
    if (moveY(body, state.fallSpeed * dt, map)) {
        state.fallSpeed = 0.0f;
    }
}

} // namespace animal
