#pragma once

#include <animal/core/math.h>

#include <cstddef>
#include <span>

namespace animal {

// A body of water to draw this frame (ADR-0018); Renderer2D::addWater copies what it needs.
// Only one body per canvas column is drawn (the last one added wins).
struct WaterSurface {
    Vec2 topLeft;                   // world px: left end of the surface at rest
    int width = 0;                  // px
    int depth = 0;                  // px from the surface at rest to the bottom
    std::span<const float> offsets; // per pixel column, + = down; empty = flat
    Color color = Color::rgba(40, 90, 120); // tint, lit by the scene's lights
    float opacity = 0.45f;          // at the surface; grows with depth
    float opacityPerPixel = 0.015f; // added per px below the surface (small for flooded rooms)
    float reflection = 0.35f;       // mirrored scene just below the surface, fading with depth
    int refraction = 1;             // px of stepped sideways wobble below the surface
    float time = 0.0f;              // animation time in s (the same for every surface in a frame)
};

inline constexpr size_t kMaxWaterStyles = 4; // distinct surfaces per frame

} // namespace animal
