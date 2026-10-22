#pragma once

#include <animal/framework/foliage.h>
#include <animal/framework/physics.h>
#include <animal/framework/platformer.h>

namespace animal {

class TileMap;

struct ClimbParams {
    float climbSpeed = 40.0f;   // px/s along the vine
    float swingForce = 700.0f;  // px/s² applied to the vine by left / right input
    float weight = 500.0f;      // px/s² the climber pulls the vine down with
    float jumpSpeed = 170.0f;   // upward speed when jumping off
    float jumpSideSpeed = 70.0f; // sideways speed when jumping off with a direction held
    float inheritVelocity = 0.8f; // share of the vine's velocity kept when jumping off
    float regrabDelay = 0.3f;   // s after jumping off before the same press can grab again
    int grabMargin = 2;         // px the grab area extends beyond the collider sideways
};

struct ClimbState {
    bool climbing = false;
    VineSystem::Grip grip;
    float cooldown = 0.0f;
    bool movedThisStep = false; // climbing up or down (drives the animation)
};

struct ClimbInput {
    float moveX = 0.0f; // -1..1, swings the vine
    float moveY = 0.0f; // -1..1, negative = up; holding up near a vine grabs it
    bool jumpPressed = false;
};

// Climbing on Verlet vines. Call before stepPlatformer each fixed step; returns true while the
// body is attached (skip the platformer step then). While attached the body follows the grip
// point with pixel-stepped collision, the climber's weight and swing input are applied to the
// vine, and jumping off hands a velocity back to the platformer state.
bool stepClimb(Body& body, PlatformerState& platformer, ClimbState& climb, const ClimbInput& input,
               const ClimbParams& params, VineSystem& vines, const TileMap& map, float dt);

} // namespace animal
