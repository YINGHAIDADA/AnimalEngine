#pragma once

#include <animal/core/math.h>

#include <cstddef>
#include <cstdint>

namespace animal {

// A GPU particle emitter (ADR-0020): a fixed population of `count` particles simulated in a
// fragment shader. Dead particles respawn at a random point of `area` (world px).
struct GpuEmitter {
    enum class Style : uint8_t {
        Lit,  // drawn in the material pass: lit like sprites
        Glow, // added over the lit scene
    };

    RectF area;
    uint32_t count = 0;
    Vec2 velocityX;    // min, max (px/s)
    Vec2 velocityY;
    Vec2 acceleration; // px/s²
    float drag = 0.0f; // 1/s
    float windResponse = 0.0f; // px/s² per wind unit
    Vec2 lifetime{1.0f, 1.0f}; // min, max (s)
    float fade = 0.0f;         // s of fade-in and fade-out
    float floorY = 1.0e9f;     // world px: particles die below this line (a water surface)
    bool dieOnSolid = false;   // against the light occluders (Renderer2D::setLightOccluders)
    Color startColor = Color::white();
    Color endColor = Color::white();
    Style style = Style::Lit;
};

inline constexpr size_t kMaxGpuEmitters = 4;
inline constexpr uint32_t kGpuParticleCapacity = 128 * 128; // state texture texels

} // namespace animal
