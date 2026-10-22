#pragma once

#include <animal/core/math.h>

namespace animal {

struct PointLight {
    Vec2 position;            // world pixels
    float radius = 48.0f;     // pixels; contribution reaches zero at the radius
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float height = 16.0f;     // pixels above the surface; lower = grazing light, stronger normal relief
    bool castsShadows = true; // blocked by the light occluders (Renderer2D::setLightOccluders)
    // Spot light: full cone angle in radians around `direction` (unit, y down); 0 = all directions.
    float coneAngle = 0.0f;
    Vec2 direction{0.0f, 1.0f};
};

struct LightingSettings {
    bool enabled = true;
    Vec3 ambient{0.12f, 0.12f, 0.16f};
    int bands = 0;            // 0: smooth falloff; N: quantize light into N steps per unit
    bool dither = true;       // ordered dither between bands
    bool shadows = true;
    float shadowPenetration = 16.0f; // light reaching into an occluder fades out over this many pixels
    // Rim light on sprites with SpriteMaterial::rim > 0, added regardless of albedo (ADR-0014):
    // silhouette pixels whose neighbour towards a light is empty catch that light...
    float rimIntensity = 1.0f;
    // ...and the top and side edges always get this, so characters read in the dark.
    Vec3 rimAmbient{0.10f, 0.10f, 0.14f};
};

} // namespace animal
