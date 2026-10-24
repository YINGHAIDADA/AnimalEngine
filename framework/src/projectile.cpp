#include <animal/framework/projectile.h>
#include <animal/framework/tilemap.h>

#include <algorithm>
#include <cmath>

namespace animal {

bool Projectile::step(const ProjectileParams& params, const TileMap& map, float dt) {
    bool hit = false;
    const bool grounded = isGrounded(body, map);
    if (grounded && velocity.y > 0.0f) { // landed: bounce up again, or stay down when slow
        hit = velocity.y > params.restSpeed;
        velocity.y = hit ? -velocity.y * params.bounce : 0.0f;
    }
    if (grounded && velocity.y >= 0.0f) {
        velocity.x *= std::max(0.0f, 1.0f - params.groundFriction * dt);
    } else {
        velocity.y = std::min(velocity.y + params.gravity * dt, params.maxFallSpeed);
        resting = false;
    }
    if (std::fabs(velocity.x) > 0.0f && moveX(body, velocity.x * dt, map)) {
        hit = hit || std::fabs(velocity.x) > params.restSpeed;
        velocity.x = -velocity.x * params.bounce;
    }
    if (!grounded || velocity.y < 0.0f) {
        if (moveY(body, velocity.y * dt, map)) {
            const float speed = std::fabs(velocity.y);
            hit = hit || speed > params.restSpeed;
            velocity.y = velocity.y > 0.0f && speed < params.restSpeed ? 0.0f : -velocity.y * params.bounce;
        }
    }
    if (isGrounded(body, map) && std::fabs(velocity.y) < 1.0f && std::fabs(velocity.x) < 2.0f) {
        velocity = {};
        resting = true;
    }
    return hit;
}

} // namespace animal
