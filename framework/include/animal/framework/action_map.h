#pragma once

#include <animal/platform/input.h>

#include <array>
#include <cstdint>
#include <vector>

namespace animal {

using ActionId = uint32_t;

struct AxisBinding {
    platform::GamepadAxis axis = platform::GamepadAxis::LeftX;
    float direction = 1.0f; // +1 or -1
};

struct ActionBinding {
    std::vector<platform::Key> keys;
    std::vector<platform::GamepadButton> buttons;
    std::vector<AxisBinding> axes;
};

// Maps keyboard / gamepad inputs to game actions. Sampled once per frame; presses are
// latched until endFixedStep() so each press is seen by exactly one fixed simulation step,
// regardless of how many steps run in a frame.
class ActionMap {
public:
    static constexpr size_t kMaxActions = 32;
    static constexpr float kAxisPressThreshold = 0.5f;

    void bind(ActionId action, ActionBinding binding);
    void update(const platform::Input& input);
    // Feeds one action's sampled strength directly (input replay, tests); update() uses it.
    void applyStrength(ActionId action, float strength);
    void endFixedStep();

    const ActionBinding& binding(ActionId action) const { return state(action).binding; }
    // A key or button of the action went down this frame (for menus and toggles read every frame,
    // not by the fixed steps).
    bool pressedThisFrame(ActionId action, const platform::Input& input) const;

    bool down(ActionId action) const { return state(action).down; }
    bool pressed(ActionId action) const { return state(action).pressedLatch; }
    // 0..1: 1 for digital inputs, the deflection for analog stick bindings.
    float strength(ActionId action) const { return state(action).strength; }

private:
    struct State {
        ActionBinding binding;
        bool down = false;
        bool pressedLatch = false;
        float strength = 0.0f;
    };

    const State& state(ActionId action) const { return actions_[action < kMaxActions ? action : 0]; }

    std::array<State, kMaxActions> actions_{};
};

} // namespace animal
