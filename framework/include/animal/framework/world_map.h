#pragma once

#include <animal/core/math.h>
#include <animal/framework/level.h>

#include <vector>

namespace animal {

// The world layout drawn as a map: every room's rectangle scaled into a screen area. The scale fits
// the whole world (not only the rooms seen so far), so the map keeps its size while it fills in.
struct WorldMapLayout {
    float scale = 0.0f;      // screen pixels per world pixel
    Vec2 origin;             // screen position of world (0, 0)
    std::vector<RectF> rooms; // by World::rooms index, snapped to whole pixels (neighbours share edges)

    Vec2 toScreen(Vec2 world) const { return {origin.x + world.x * scale, origin.y + world.y * scale}; }
};

// Centred in `area`, at most `maxScale`. Empty when the world has no rooms.
WorldMapLayout layoutWorldMap(const World& world, const RectF& area, float maxScale);

} // namespace animal
