#pragma once

#include <animal/core/math.h>

namespace animal {

struct PointLight {
    Vec2 position;            // world pixels
    float radius = 48.0f;     // pixels; contribution reaches zero at the radius
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float height = 16.0f;     // pixels above the surface; lower = grazing light, stronger normal relief
};

struct LightingSettings {
    bool enabled = true;
    Vec3 ambient{0.12f, 0.12f, 0.16f};
    int bands = 0;            // 0: smooth falloff; N: quantize light into N steps per unit
    bool dither = true;       // ordered dither between bands
};

} // namespace animal
