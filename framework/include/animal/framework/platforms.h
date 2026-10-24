#pragma once

// Moving solids (ADR-0034): platforms that travel back and forth, platforms that fall a moment after
// being stood on, and doors. They are solid for every body through the tile map's dynamic solids;
// the one body that matters (the player) rides on them and is pushed by them.

#include <animal/framework/geometry.h>
#include <animal/framework/physics.h>

#include <vector>

namespace animal {

class TileMap;

struct PlatformDesc {
    IntRect rect;          // start position, room pixels
    Vec2 travel;           // moving: offset to the far end of the path (zero: stays put)
    float speed = 30.0f;   // px/s along the path
    float pause = 0.5f;    // seconds of rest at each end
    float phase = 0.0f;    // 0..1: where on the path (out and back) it starts
    bool falls = false;    // falling platform: shakes when stood on, falls, comes back later
};

struct FallParams {
    float shakeSeconds = 0.45f;
    float gravity = 520.0f;      // px/s²
    float maxSpeed = 260.0f;     // px/s
    float returnSeconds = 2.5f;  // gone for this long before it comes back
};

class PlatformSystem {
public:
    enum class State { Resting, Moving, Shaking, Falling, Gone };
    struct Platform {
        PlatformDesc desc;
        IntRect rect;          // current, room pixels
        Vec2 exact;            // current top-left, sub-pixel
        State state = State::Resting;
        float timer = 0.0f;    // pause / shake / return time left
        float progress = 0.0f; // 0..1 along the path
        int direction = 1;     // +1 towards the far end
        float fallSpeed = 0.0f;
        Vec2 velocity;         // px/s over the last step (0 while resting, pausing or gone)
        bool solid = true;     // doors open by turning this off
    };

    struct StepResult {
        bool crushed = false; // the body was pushed into something solid
        bool riding = false;  // the body stood on a platform at the start of the step
    };

    void clear();
    size_t add(const PlatformDesc& desc);
    void setSolid(size_t index, bool solid);
    const std::vector<Platform>& platforms() const { return platforms_; }
    const FallParams& fallParams() const { return fall_; }
    void setFallParams(const FallParams& params) { fall_ = params; }

    // Moves every platform by one step; `body` (may be null) rides along and is pushed. `bottom` is
    // the y below which a falling platform is gone. Updates the map's dynamic solids.
    StepResult step(float dt, TileMap& map, Body* body, int bottom);
    // Writes the solid platforms into the map's dynamic solids (after add / setSolid).
    void publish(TileMap& map) const;

private:
    // Moves platform `index` by whole pixels, carrying a rider and pushing a body in the way.
    void moveBy(size_t index, Vec2i delta, TileMap& map, Body* body, bool riding, StepResult& result);

    std::vector<Platform> platforms_;
    FallParams fall_;
};

} // namespace animal
