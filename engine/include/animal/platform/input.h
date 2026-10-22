#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace animal::platform {

enum class Key : uint8_t {
    Left,
    Right,
    Up,
    Down,
    W,
    A,
    S,
    D,
    Z,
    X,
    C,
    Space,
    Enter,
    Escape,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F8,
    F12,
    Count,
};

// Positional names (SDL convention): South is A on Xbox, Cross on PlayStation.
enum class GamepadButton : uint8_t {
    South,
    East,
    West,
    North,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    LeftShoulder,
    RightShoulder,
    Back,
    Start,
    Count,
};

enum class GamepadAxis : uint8_t {
    LeftX,
    LeftY,
    Count,
};

class Input {
public:
    static constexpr float kStickDeadzone = 0.25f;

    // Samples keyboard and the active gamepad; call once per frame after pollEvents().
    void update();

    bool down(Key key) const { return keys_.current[index(key)]; }
    bool pressed(Key key) const { return keys_.current[index(key)] && !keys_.previous[index(key)]; }

    bool gamepadConnected() const { return gamepadConnected_; }
    bool down(GamepadButton button) const { return buttons_.current[index(button)]; }
    bool pressed(GamepadButton button) const {
        return buttons_.current[index(button)] && !buttons_.previous[index(button)];
    }
    // In [-1, 1] with the deadzone removed and rescaled; +Y points down.
    float axis(GamepadAxis axis) const { return axes_[index(axis)]; }

private:
    template <typename E>
    static constexpr size_t index(E value) {
        return static_cast<size_t>(value);
    }

    template <size_t N>
    struct ButtonState {
        std::array<bool, N> current{};
        std::array<bool, N> previous{};
    };

    ButtonState<static_cast<size_t>(Key::Count)> keys_;
    ButtonState<static_cast<size_t>(GamepadButton::Count)> buttons_;
    std::array<float, static_cast<size_t>(GamepadAxis::Count)> axes_{};
    bool gamepadConnected_ = false;
};

} // namespace animal::platform
