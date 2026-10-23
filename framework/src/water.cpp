#include <animal/framework/water.h>
#include <animal/framework/tilemap.h>

#include <algorithm>
#include <cmath>

namespace animal {

WaterBody::WaterBody(const IntRect& area, const WaterParams& params)
    : area_(area),
      params_(params),
      height_(static_cast<size_t>(std::max(area.width, 1)), 0.0f),
      velocity_(height_.size(), 0.0f) {}

void WaterBody::step(float dt) {
    // Semi-implicit wave equation; substeps keep waveSpeed · dt below one column (stability).
    const auto substeps = static_cast<int>(std::ceil(params_.waveSpeed * dt / 0.5f));
    const float h = dt / static_cast<float>(std::max(substeps, 1));
    const float c2 = params_.waveSpeed * params_.waveSpeed;
    const size_t n = height_.size();
    for (int s = 0; s < substeps; ++s) {
        for (size_t i = 0; i < n; ++i) {
            const float left = height_[i > 0 ? i - 1 : i];
            const float right = height_[i + 1 < n ? i + 1 : i];
            const float laplacian = left + right - 2.0f * height_[i];
            const float a = c2 * laplacian - params_.stiffness * height_[i] - params_.damping * velocity_[i];
            velocity_[i] += a * h;
        }
        for (size_t i = 0; i < n; ++i) {
            height_[i] = std::clamp(height_[i] + velocity_[i] * h, -params_.maxOffset, params_.maxOffset);
        }
    }
}

void WaterBody::disturb(float x, float halfWidth, float velocity) {
    const float local = x - static_cast<float>(area_.x);
    const int first = std::max(0, static_cast<int>(std::floor(local - halfWidth)));
    const int last = std::min(static_cast<int>(height_.size()) - 1, static_cast<int>(std::ceil(local + halfWidth)));
    for (int i = first; i <= last; ++i) {
        // Strongest at x, fading to the edges of the disturbed span.
        const float falloff = 1.0f - std::min(1.0f, std::fabs(static_cast<float>(i) + 0.5f - local) / (halfWidth + 1.0f));
        velocity_[static_cast<size_t>(i)] += velocity * falloff;
    }
}

float WaterBody::surfaceY(float x) const {
    const int column = std::clamp(static_cast<int>(std::floor(x)) - area_.x, 0, static_cast<int>(height_.size()) - 1);
    return static_cast<float>(area_.y) + height_[static_cast<size_t>(column)];
}

float WaterBody::submersion(const IntRect& box) const {
    const float centerX = static_cast<float>(box.x) + static_cast<float>(box.width) * 0.5f;
    if (centerX < static_cast<float>(area_.x) || centerX >= static_cast<float>(area_.right()) ||
        box.bottom() <= area_.y - static_cast<int>(params_.maxOffset) || box.y >= area_.bottom() || box.height <= 0) {
        return 0.0f;
    }
    const float below = static_cast<float>(box.bottom()) - surfaceY(centerX);
    return std::clamp(below / static_cast<float>(box.height), 0.0f, 1.0f);
}

PlatformerParams swimmingParams(const PlatformerParams& base, float submersion, const SwimParams& swim) {
    if (submersion <= 0.0f) {
        return base;
    }
    const float s = std::clamp(submersion, 0.0f, 1.0f);
    auto blend = [s](float dry, float wet) { return dry + (wet - dry) * s; };
    PlatformerParams p = base;
    p.gravity = base.gravity * (1.0f - s / swim.floatDepth);
    p.fallGravityMultiplier = 1.0f; // no snappy descent in water
    p.runSpeed = blend(base.runSpeed, base.runSpeed * swim.runScale);
    p.groundAcceleration = blend(base.groundAcceleration, base.groundAcceleration * swim.runScale);
    p.airAcceleration = blend(base.airAcceleration, base.airAcceleration * swim.runScale);
    p.maxFallSpeed = blend(base.maxFallSpeed, base.maxFallSpeed * swim.fallSpeedScale);
    return p;
}

void applyWater(PlatformerState& state, const PlatformerParams& base, float submersion, float dt,
                const SwimParams& swim) {
    if (submersion <= 0.0f) {
        return;
    }
    const float keep = std::max(0.0f, 1.0f - swim.drag * submersion * dt);
    state.velocity = state.velocity * keep;
    if (submersion >= swim.jumpDepth) {
        state.coyoteTimer = base.coyoteFrames;
    }
}

void stepDive(Body& body, PlatformerState& state, Vec2 move, const DiveParams& params, const TileMap& map, float dt) {
    const float length = std::hypot(move.x, move.y);
    if (length > 1.0f) {
        move = move * (1.0f / length);
    }
    Vec2 target = move * params.speed;
    if (move.y == 0.0f) {
        target.y = -params.drift;
    }
    const float maxChange = params.acceleration * dt;
    const auto approach = [maxChange](float value, float goal) {
        return value + std::clamp(goal - value, -maxChange, maxChange);
    };
    state.velocity = {approach(state.velocity.x, target.x), approach(state.velocity.y, target.y)};
    if (moveX(body, state.velocity.x * dt, map)) {
        state.velocity.x = 0.0f;
    }
    if (moveY(body, state.velocity.y * dt, map)) {
        state.velocity.y = 0.0f;
    }
    state.grounded = isGrounded(body, map);
    state.rising = false;
    state.jumpedThisStep = false;
    state.landedThisStep = false;
}

void applyCurrent(Body& body, Vec2 current, float submersion, const TileMap& map, float dt) {
    if (submersion <= 0.0f) {
        return;
    }
    const float share = std::min(submersion, 1.0f);
    moveX(body, current.x * share * dt, map);
    moveY(body, current.y * share * dt, map);
}

} // namespace animal
