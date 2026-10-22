#include <animal/framework/physics.h>
#include <animal/framework/tilemap.h>

#include <cmath>

namespace animal {

namespace {

bool moveAxis(Body& body, float amount, const TileMap& map, int32_t Vec2i::*axis, float Vec2::*remainderAxis) {
    float& remainder = body.remainder.*remainderAxis;
    remainder += amount;
    int32_t steps = static_cast<int32_t>(std::round(remainder));
    if (steps == 0) {
        return false;
    }
    remainder -= static_cast<float>(steps);

    const int32_t sign = steps > 0 ? 1 : -1;
    Vec2i step;
    step.*axis = sign;
    while (steps != 0) {
        if (collidesAt(body, step, map)) {
            remainder = 0.0f;
            return true;
        }
        body.position.*axis += sign;
        steps -= sign;
    }
    return false;
}

} // namespace

bool collidesAt(const Body& body, Vec2i offset, const TileMap& map) {
    return map.overlapsSolid(body.worldCollider().offset(offset));
}

bool moveX(Body& body, float amount, const TileMap& map) {
    return moveAxis(body, amount, map, &Vec2i::x, &Vec2::x);
}

bool moveY(Body& body, float amount, const TileMap& map) {
    return moveAxis(body, amount, map, &Vec2i::y, &Vec2::y);
}

} // namespace animal
