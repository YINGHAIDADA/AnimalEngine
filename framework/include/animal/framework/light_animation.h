#pragma once

#include <optional>
#include <string_view>

namespace animal {

// Intensity animation of a light: a multiplier computed from time and a per-light phase only, so
// it is deterministic and needs no state.
struct LightAnimation {
    enum class Mode {
        None,
        Flicker, // fast irregular wobble (flames)
        Pulse,   // slow sine (crystals, magic)
    };

    Mode mode = Mode::None;
    float speed = 1.0f;   // pulse: cycles per second; flicker: rate multiplier
    float amount = 0.15f; // relative swing: the multiplier stays within 1 ± amount

    float factor(double time, float phase) const;
};

// "none", "flicker" or "pulse" (Tiled property values).
std::optional<LightAnimation::Mode> parseLightAnimationMode(std::string_view name);

} // namespace animal
