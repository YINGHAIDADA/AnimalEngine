#include <animal/core/assert.h>
#include <animal/framework/action_map.h>

#include <algorithm>

namespace animal {

void ActionMap::bind(ActionId action, ActionBinding binding) {
    ANIMAL_ASSERT(action < kMaxActions);
    actions_[action].binding = std::move(binding);
}

void ActionMap::update(const platform::Input& input) {
    for (ActionId action = 0; action < kMaxActions; ++action) {
        const State& s = actions_[action];
        float strength = 0.0f;
        for (platform::Key key : s.binding.keys) {
            if (input.down(key)) {
                strength = 1.0f;
            }
        }
        for (platform::GamepadButton button : s.binding.buttons) {
            if (input.down(button)) {
                strength = 1.0f;
            }
        }
        for (const AxisBinding& a : s.binding.axes) {
            strength = std::max(strength, std::clamp(input.axis(a.axis) * a.direction, 0.0f, 1.0f));
        }
        applyStrength(action, strength);
    }
}

void ActionMap::applyStrength(ActionId action, float strength) {
    ANIMAL_ASSERT(action < kMaxActions);
    State& s = actions_[action];
    const bool wasDown = s.down;
    s.strength = std::clamp(strength, 0.0f, 1.0f);
    s.down = s.strength >= kAxisPressThreshold;
    if (s.down && !wasDown) {
        s.pressedLatch = true;
    }
}

void ActionMap::endFixedStep() {
    for (State& s : actions_) {
        s.pressedLatch = false;
    }
}

bool ActionMap::pressedThisFrame(ActionId action, const platform::Input& input) const {
    const ActionBinding& b = binding(action);
    return std::ranges::any_of(b.keys, [&](platform::Key k) { return input.pressed(k); }) ||
           std::ranges::any_of(b.buttons, [&](platform::GamepadButton g) { return input.pressed(g); });
}

} // namespace animal
