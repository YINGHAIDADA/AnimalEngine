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
    // Depth (ADR-0039): the back wall (sprites drawn with Renderer2D::setLayerDepth(1)) takes this
    // share of the direct light, its normals are flattened by `backFlatten`, and it is pulled towards
    // `depthFogColor` by `depthFog`.
    float backLight = 1.0f;
    float backFlatten = 0.0f;
    float depthFog = 0.0f;
    Vec3 depthFogColor{0.10f, 0.10f, 0.15f};
    // Indirect light: the light blurred wide at a quarter of the resolution, added with this strength
    // (light bouncing off the walls around a lamp).
    float indirect = 0.0f;
    // Ambient occlusion from the light occluders (solid tiles): open space next to walls gets this
    // much less ambient and indirect light; ambient brighter at the top and darker at the bottom by
    // `ambientGradient`.
    float ambientOcclusion = 0.0f;
    float ambientGradient = 0.0f;
};

} // namespace animal
