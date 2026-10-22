#pragma once

#include <animal/core/math.h>
#include <animal/framework/geometry.h>

namespace animal {

class TileMap;

// Integer pixel position plus a fractional remainder (ADR-0006).
struct Body {
    Vec2i position;
    Vec2 remainder;
    IntRect collider; // relative to position

    IntRect worldCollider() const { return collider.offset(position); }
    Vec2 exactPosition() const {
        return {static_cast<float>(position.x) + remainder.x, static_cast<float>(position.y) + remainder.y};
    }
};

// Moves `body` by `amount` pixels along one axis, one pixel at a time, stopping flush against
// solid tiles. Fractions accumulate in the remainder. Returns true if movement was blocked
// (the remainder on that axis is then cleared).
bool moveX(Body& body, float amount, const TileMap& map);
bool moveY(Body& body, float amount, const TileMap& map);

// True if the body would collide after shifting by `offset` pixels.
bool collidesAt(const Body& body, Vec2i offset, const TileMap& map);

inline bool isGrounded(const Body& body, const TileMap& map) {
    return collidesAt(body, {0, 1}, map);
}

} // namespace animal
