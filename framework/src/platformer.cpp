#include <animal/framework/platformer.h>
#include <animal/framework/tilemap.h>

#include <algorithm>
#include <cmath>

namespace animal {

namespace {

float approach(float value, float target, float maxDelta) {
    if (value < target) {
        return std::min(value + maxDelta, target);
    }
    return std::max(value - maxDelta, target);
}

// When the head clips a ceiling corner while rising, slide sideways (up to `maxNudge` px)
// if that clears the way, instead of stopping the jump.
bool tryCornerCorrection(Body& body, int32_t maxNudge, float dy, const TileMap& map) {
    const int32_t up = dy < 0.0f ? -1 : 0;
    if (up == 0) {
        return false;
    }
    for (int32_t nudge = 1; nudge <= maxNudge; ++nudge) {
        for (int32_t dir : {-1, 1}) {
            const Vec2i side{dir * nudge, 0};
            if (!collidesAt(body, side, map) && !collidesAt(body, {side.x, up}, map)) {
                body.position.x += side.x;
                return true;
            }
        }
    }
    return false;
}

} // namespace

void stepPlatformer(Body& body, PlatformerState& state, const PlatformerInput& input, const PlatformerParams& params,
                    const TileMap& map, float dt) {
    state.jumpedThisStep = false;
    state.landedThisStep = false;
    Vec2& v = state.velocity;

    state.grounded = isGrounded(body, map);
    if (state.grounded) {
        state.coyoteTimer = params.coyoteFrames;
    } else if (state.coyoteTimer > 0) {
        --state.coyoteTimer;
    }
    if (input.jumpPressed) {
        state.jumpBufferTimer = params.jumpBufferFrames;
    } else if (state.jumpBufferTimer > 0) {
        --state.jumpBufferTimer;
    }

    // Horizontal run.
    const float move = std::clamp(input.moveX, -1.0f, 1.0f);
    const float target = move * params.runSpeed;
    const bool accelerating = std::fabs(move) > 0.0f && (v.x == 0.0f || (v.x > 0.0f) == (target > 0.0f));
    const float rate = state.grounded ? (accelerating ? params.groundAcceleration : params.groundDeceleration)
                                      : (accelerating ? params.airAcceleration : params.airDeceleration);
    v.x = approach(v.x, target, rate * dt);

    // Jump: buffered press + (grounded or within coyote time).
    if (state.jumpBufferTimer > 0 && state.coyoteTimer > 0) {
        v.y = -params.jumpSpeed;
        state.jumpBufferTimer = 0;
        state.coyoteTimer = 0;
        state.rising = true;
        state.grounded = false;
        state.jumpedThisStep = true;
    }

    // Variable jump height.
    if (state.rising) {
        if (v.y >= 0.0f) {
            state.rising = false;
        } else if (!input.jumpHeld) {
            v.y *= params.jumpCutMultiplier;
            state.rising = false;
        }
    }

    // Gravity.
    if (!state.grounded || v.y < 0.0f) {
        float g = params.gravity;
        if (v.y > 0.0f) {
            g *= params.fallGravityMultiplier;
        }
        if (input.jumpHeld && std::fabs(v.y) < params.apexSpeedThreshold) {
            g *= params.apexGravityMultiplier;
        }
        v.y = std::min(v.y + g * dt, params.maxFallSpeed);
    } else {
        v.y = 0.0f;
    }

    // Move and collide.
    if (moveX(body, v.x * dt, map)) {
        v.x = 0.0f;
    }
    const float dy = v.y * dt;
    if (moveY(body, dy, map)) {
        if (dy < 0.0f && tryCornerCorrection(body, params.cornerCorrection, dy, map)) {
            moveY(body, dy, map);
        } else {
            if (v.y > 0.0f) {
                state.landedThisStep = true;
            }
            v.y = 0.0f;
            state.rising = false;
        }
    }
    state.grounded = isGrounded(body, map);
}

} // namespace animal
