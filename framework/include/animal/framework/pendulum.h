#pragma once

// A weight on a rope (ADR-0038): hanging lanterns swing when bodies brush past them or a blast
// pushes them. The weight resists by its mass relative to the player (1): a heavy lantern only
// leans away from a passing player and swings back slowly.

#include <animal/core/math.h>
#include <animal/framework/geometry.h>

namespace animal {

struct PendulumParams {
    float gravity = 600.0f;  // px/s²
    float damping = 0.35f;   // 1/s
    float mass = 4.0f;       // relative to the player
    float contact = 10.0f;   // 1/s: how fast a touching body drags a weight of mass 1 along
    float maxAngle = 1.2f;   // radians either way
};

struct Pendulum {
    Vec2 pivot;          // where the rope is fixed (world px)
    float length = 16.0f; // px from the pivot to the centre of the weight
    float angle = 0.0f;   // radians, 0 = hanging straight down, + = swung to the right
    float angularVelocity = 0.0f;

    Vec2 bob() const;          // centre of the weight
    Vec2 bobVelocity() const;  // px/s
    // A body of `bounds` moving at `velocity` drags the weight along while it overlaps `weight`
    // (the weight's box around its centre, relative to the centre).
    void touch(const IntRect& bounds, Vec2 velocity, const IntRect& weight, const PendulumParams& params, float dt);
    // An instantaneous push (a blast): `impulse` in px/s for a weight of mass 1.
    void push(Vec2 impulse, const PendulumParams& params);
    void step(const PendulumParams& params, float dt);
};

} // namespace animal
