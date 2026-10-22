#pragma once

#include <animal/core/math.h>
#include <animal/framework/physics.h>

#include <cstdint>

namespace animal {

class TileMap;

// Feel parameters, in pixels and seconds (frame counts assume the 60 Hz fixed step).
struct PlatformerParams {
    float runSpeed = 84.0f;
    float groundAcceleration = 900.0f;
    float groundDeceleration = 1200.0f;
    float airAcceleration = 650.0f;
    float airDeceleration = 350.0f;

    float gravity = 800.0f;
    float fallGravityMultiplier = 1.5f;  // snappier descent
    float apexGravityMultiplier = 0.55f; // hang time near the top while jump is held
    float apexSpeedThreshold = 24.0f;    // |vy| below this counts as the apex
    float maxFallSpeed = 220.0f;

    float jumpSpeed = 205.0f;        // max height ≈ jumpSpeed² / (2 · gravity)
    float jumpCutMultiplier = 0.4f;  // vy scale when jump is released while rising
    int32_t coyoteFrames = 6;        // jump still allowed this long after leaving a ledge
    int32_t jumpBufferFrames = 6;    // a press this long before landing still jumps
    int32_t cornerCorrection = 2;    // px nudged sideways around ceiling corners
};

struct PlatformerInput {
    float moveX = 0.0f; // -1..1
    bool jumpPressed = false;
    bool jumpHeld = false;
};

struct PlatformerState {
    Vec2 velocity;
    bool grounded = false;
    bool rising = false; // in a jump whose height can still be cut
    int32_t coyoteTimer = 0;
    int32_t jumpBufferTimer = 0;
    bool jumpedThisStep = false;
    bool landedThisStep = false;
    float landingSpeed = 0.0f; // fall speed at the moment of landing (valid when landedThisStep)
};

// One fixed step of platformer movement: run, gravity, buffered / coyote jumps, variable
// jump height and ceiling corner correction, then pixel-stepped collision (ADR-0006).
void stepPlatformer(Body& body, PlatformerState& state, const PlatformerInput& input, const PlatformerParams& params,
                    const TileMap& map, float dt);

} // namespace animal
