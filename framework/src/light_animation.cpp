#include <animal/core/math.h>
#include <animal/framework/light_animation.h>

#include <algorithm>
#include <cmath>

namespace animal {

float LightAnimation::factor(double time, float phase) const {
    const auto t = static_cast<float>(time) * speed;
    float wave = 0.0f;
    switch (mode) {
    case Mode::None:
        return 1.0f;
    case Mode::Flicker:
        // Two incommensurate sines read as irregular; their weights sum to 1.
        wave = 0.6f * std::sin(t * 13.0f + phase) + 0.4f * std::sin(t * 29.0f + phase * 2.3f);
        break;
    case Mode::Pulse:
        wave = std::sin(t * 2.0f * kPi + phase);
        break;
    }
    return std::max(0.0f, 1.0f + amount * wave);
}

std::optional<LightAnimation::Mode> parseLightAnimationMode(std::string_view name) {
    if (name == "none" || name.empty()) {
        return LightAnimation::Mode::None;
    }
    if (name == "flicker") {
        return LightAnimation::Mode::Flicker;
    }
    if (name == "pulse") {
        return LightAnimation::Mode::Pulse;
    }
    return std::nullopt;
}

} // namespace animal
