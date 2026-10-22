#pragma once

#include <animal/core/math.h>
#include <animal/framework/geometry.h>

namespace animal {

// Something that pushes ambient simulations around (foliage, particles): the player now,
// later creatures and projectiles. Provided by the game every fixed step.
struct Influence {
    IntRect bounds; // world pixels
    Vec2 velocity;  // px/s
    bool landed = false;
};

} // namespace animal
