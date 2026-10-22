#pragma once

#include <animal/core/math.h>

namespace animal {

// Bloom and color grading applied to the finished canvas (after lighting, smoke and glow).
// Neutral defaults: no visible change except bloom.
struct PostSettings {
    bool enabled = true;          // false: the canvas is shown untouched

    float bloomThreshold = 0.6f;  // brightness (max channel) where bloom starts
    float bloomKnee = 0.3f;       // soft transition above the threshold
    float bloomIntensity = 0.9f;  // 0 = no bloom
    int bloomBands = 6;           // quantize the halo with ordered dither (0 = smooth)

    float exposure = 1.0f;
    float contrast = 1.0f;        // around mid grey
    float saturation = 1.0f;
    Vec3 shadowTint{1.0f, 1.0f, 1.0f};    // multiplied into dark tones
    Vec3 highlightTint{1.0f, 1.0f, 1.0f}; // multiplied into bright tones
    float vignette = 0.0f;        // 0..1 darkening towards the corners
};

} // namespace animal
