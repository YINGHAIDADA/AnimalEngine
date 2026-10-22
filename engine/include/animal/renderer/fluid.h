#pragma once

#include <animal/core/math.h>
#include <animal/rhi/rhi.h>

namespace animal {

// Smoke simulation grid: one cell per 2×2 canvas pixels.
inline constexpr rhi::Extent2D kFluidGrid{160, 90};

struct FluidSettings {
    bool enabled = true;
    float velocityKeepPerSecond = 0.5f; // share of velocity left after one second
    float smokeKeepPerSecond = 0.6f;    // share of smoke left after one second
    float buoyancy = 30.0f;             // upward acceleration per unit density (cells/s²)
    float vorticity = 12.0f;            // confinement strength: higher = curlier smoke (0 = off)
    int pressureIterations = 12;        // Jacobi iterations (rounded up to even), warm-started from the last frame
    Vec3 smokeTint{0.8f, 0.8f, 0.85f};
    float opacity = 1.0f;
};

// A Gaussian source of force and/or smoke, in world pixels; amounts are per second.
struct FluidSplat {
    Vec2 position;
    float radius = 6.0f;          // px
    Vec2 force;                   // px/s² of velocity added
    Vec3 color{1.0f, 1.0f, 1.0f}; // smoke color
    float smoke = 0.0f;           // density added per second
};

} // namespace animal
