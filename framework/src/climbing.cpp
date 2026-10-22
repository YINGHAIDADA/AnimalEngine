#include <animal/framework/climbing.h>
#include <animal/framework/tilemap.h>

#include <algorithm>
#include <cmath>

namespace animal {

namespace {

// Exact (sub-pixel) centre: following the grip from the integer position would add the same
// fraction to the remainder every step and make the climber flicker between two pixels.
Vec2 colliderCenter(const Body& body) {
    const Vec2 p = body.exactPosition();
    return {p.x + static_cast<float>(body.collider.x) + static_cast<float>(body.collider.width) * 0.5f,
            p.y + static_cast<float>(body.collider.y) + static_cast<float>(body.collider.height) * 0.5f};
}

} // namespace

bool stepClimb(Body& body, PlatformerState& platformer, ClimbState& climb, const ClimbInput& input,
               const ClimbParams& params, VineSystem& vines, const TileMap& map, float dt) {
    climb.cooldown = std::max(0.0f, climb.cooldown - dt);
    climb.movedThisStep = false;

    if (!climb.climbing) {
        if (input.moveY >= -0.5f || climb.cooldown > 0.0f) {
            return false;
        }
        IntRect area = body.worldCollider();
        area.x -= params.grabMargin;
        area.width += 2 * params.grabMargin;
        const std::optional<VineSystem::Grip> grip = vines.findGrip(area);
        if (!grip) {
            return false;
        }
        climb.climbing = true;
        climb.grip = *grip;
        platformer = PlatformerState{};
    }

    if (climb.grip.vine >= vines.vines().size()) { // the vine went away (room change)
        climb.climbing = false;
        return false;
    }

    if (input.jumpPressed) {
        const Vec2 vineVelocity = vines.velocityAt(climb.grip, dt);
        const float side = input.moveX > 0.25f ? 1.0f : (input.moveX < -0.25f ? -1.0f : 0.0f);
        platformer = PlatformerState{};
        platformer.velocity = {vineVelocity.x * params.inheritVelocity + side * params.jumpSideSpeed,
                               -params.jumpSpeed};
        platformer.rising = true;
        platformer.jumpedThisStep = true;
        climb.climbing = false;
        climb.cooldown = params.regrabDelay;
        return false;
    }

    // Move along the vine; holding down at the bottom lets go.
    const float total = vines.length(climb.grip.vine);
    const float minimum = vines.params().segmentLength; // don't climb into the anchor
    if (input.moveY > 0.5f && climb.grip.distance >= total - 0.01f) {
        platformer = PlatformerState{};
        platformer.velocity = vines.velocityAt(climb.grip, dt) * params.inheritVelocity;
        climb.climbing = false;
        climb.cooldown = params.regrabDelay;
        return false;
    }
    const float before = climb.grip.distance;
    climb.grip.distance = std::clamp(climb.grip.distance + input.moveY * params.climbSpeed * dt, minimum, total);
    climb.movedThisStep = std::fabs(climb.grip.distance - before) > 1e-4f;

    vines.applyForce(climb.grip, {input.moveX * params.swingForce, params.weight});
    vines.hold(climb.grip);

    // Follow the grip point (centre of the collider on the vine), colliding with tiles.
    const Vec2 delta = vines.pointAt(climb.grip) - colliderCenter(body);
    moveX(body, delta.x, map);
    moveY(body, delta.y, map);
    platformer.velocity = vines.velocityAt(climb.grip, dt);
    platformer.grounded = false;
    return true;
}

} // namespace animal
