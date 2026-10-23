#pragma once

// Water bodies (ADR-0018): a rectangle of water whose surface carries 1D waves, and the
// floating rules for platformer bodies in it.

#include <animal/framework/geometry.h>
#include <animal/framework/physics.h>
#include <animal/framework/platformer.h>

#include <span>
#include <vector>

namespace animal {

struct WaterParams {
    float waveSpeed = 90.0f; // px/s along the surface
    float stiffness = 40.0f; // pull back to rest, 1/s²
    float damping = 2.5f;    // 1/s
    float maxOffset = 6.0f;  // px, either way
};

class WaterBody {
public:
    // `area` in world pixels; its top edge is the surface at rest.
    explicit WaterBody(const IntRect& area, const WaterParams& params = {});

    const IntRect& area() const { return area_; }
    void step(float dt);
    // Adds vertical velocity (px/s, + = down) to the surface columns within `halfWidth` of x.
    void disturb(float x, float halfWidth, float velocity);

    // One entry per pixel column of the area: surface displacement, + = down.
    std::span<const float> offsets() const { return height_; }
    float surfaceY(float x) const; // world y of the surface at x (columns outside clamp to the edge)
    // Share of the box's height below the surface at its centre column (0 outside the area).
    float submersion(const IntRect& box) const;

private:
    IntRect area_;
    WaterParams params_;
    std::vector<float> height_;
    std::vector<float> velocity_;
};

struct SwimParams {
    float floatDepth = 0.6f;     // submersion where buoyancy balances gravity
    float runScale = 0.55f;      // run speed and accelerations
    float fallSpeedScale = 0.3f; // max sinking speed
    float drag = 3.0f;           // 1/s at full submersion
    float jumpDepth = 0.3f;      // jumping (out of the water) allowed from this submersion
};

// Platformer parameters for a body `submersion` deep (0 = unchanged): gravity scales by
// (1 - submersion / floatDepth), so it turns into lift deeper than floatDepth.
PlatformerParams swimmingParams(const PlatformerParams& base, float submersion, const SwimParams& swim = {});
// Before stepPlatformer: water drag on the velocity, and a jump made available when deep enough.
void applyWater(PlatformerState& state, const PlatformerParams& base, float submersion, float dt,
                const SwimParams& swim = {});

// Diving (ADR-0034): with the ability, a body under water swims freely in both axes instead of
// floating at the surface; without input it drifts slowly up.
struct DiveParams {
    float speed = 72.0f;         // px/s with the stick fully over
    float acceleration = 420.0f; // px/s² towards the input velocity
    float drift = 16.0f;         // px/s upwards with no vertical input
    float enterDepth = 0.5f;     // submersion from which holding down starts a dive
    float leaveDepth = 0.85f;    // rising above this submersion (not holding down) ends the dive
};

// One fixed step of diving: `move` is the input (-1..1 per axis, + = right / down).
void stepDive(Body& body, PlatformerState& state, Vec2 move, const DiveParams& params, const TileMap& map, float dt);

// A current carries a body in water along with it, in proportion to how deep it is (px/s).
void applyCurrent(Body& body, Vec2 current, float submersion, const TileMap& map, float dt);

} // namespace animal
