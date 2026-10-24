#pragma once

// Looks (ADR-0015): the art-directed parameters of a scene's lighting, rim light, bloom / grading
// and smoke colour, as reusable preset files that rooms reference and override.

#include <animal/core/math.h>
#include <animal/framework/level.h>
#include <animal/renderer/fluid.h>
#include <animal/renderer/lighting.h>
#include <animal/renderer/post.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace animal {

struct Look {
    // Lighting (ADR-0007, ADR-0012, ADR-0014)
    Vec3 ambient;
    int bands = 0;
    bool dither = true;
    bool shadows = true;
    float shadowPenetration = 0.0f;
    float rimIntensity = 0.0f;
    Vec3 rimAmbient;
    // Depth (ADR-0039)
    float backLight = 1.0f;
    float backFlatten = 0.0f;
    float depthFog = 0.0f;
    Vec3 depthFogColor;
    float indirect = 0.0f;
    float ambientOcclusion = 0.0f;
    float ambientGradient = 0.0f;
    // Bloom and grading (ADR-0009)
    float bloomThreshold = 0.0f;
    float bloomKnee = 0.0f;
    float bloomIntensity = 0.0f;
    int bloomBands = 0;
    float exposure = 1.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    Vec3 shadowTint;
    Vec3 highlightTint;
    float vignette = 0.0f;
    // Smoke colour (ADR-0008)
    Vec3 smokeTint;
    float smokeOpacity = 1.0f;

    // The look the renderer has when nothing is configured.
    static Look engineDefaults();
};

// Parses a look file over `defaults`. File format (every field optional):
//   {"format": "animal-look", "version": 1, "base": "other.look.json", "exposure": 1.2, ...}
// `base` (relative to this file) is applied first. Colours ("ambient", "rimAmbient",
// "depthFogColor") are "#rrggbb";
// tints ("shadowTint", "highlightTint", "smokeTint") are [r, g, b] and may exceed 1. Unknown
// fields, wrong types and base cycles are errors.
std::optional<Look> parseLook(std::string_view json, const std::string& path, const TextReader& read,
                              const Look& defaults, std::string* error);
std::optional<Look> loadLook(const std::string& path, const Look& defaults); // logs errors

// Applies Tiled properties named `prefix` + field ("look.exposure", ...). Colours take Tiled
// colours; tints take a colour or a string "r g b". Returns one message per unknown or mistyped
// property (they are skipped).
std::vector<std::string> applyLookOverrides(Look& look, const Properties& properties,
                                            std::string_view prefix = "look.");

// t = 0: a, t = 1: b. Numbers and colours blend; whole-number and on / off fields take b's value
// as soon as t > 0, so a transition never shows an in-between band count.
Look mixLooks(const Look& a, const Look& b, float t);

// Writes the look into renderer settings; their `enabled` switches and smoke dynamics are kept.
void applyLook(const Look& look, LightingSettings& lighting, PostSettings& post, FluidSettings& fluid);

} // namespace animal
