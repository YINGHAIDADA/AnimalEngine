// Controls (ADR-0036): the game's actions with their default keys, gamepad buttons and stick
// directions; the player's rebinding of keys and buttons (settings) replaces the defaults, the stick
// stays. Prompts name the input of the device used last.

#include "sandbox_game.h"

#include <animal/platform/input.h>

#include <algorithm>
#include <cctype>

namespace game {

namespace {

using animal::platform::GamepadAxis;
using animal::platform::GamepadButton;
using animal::platform::Key;

const std::vector<SandboxGame::ActionInfo>& actionTable() {
    static const std::vector<SandboxGame::ActionInfo> table = {
        {MoveLeft, "left", "MOVE LEFT", {{Key::Left, Key::A}, {GamepadButton::DpadLeft}, {{GamepadAxis::LeftX, -1.0f}}}},
        {MoveRight, "right", "MOVE RIGHT", {{Key::Right, Key::D}, {GamepadButton::DpadRight}, {{GamepadAxis::LeftX, 1.0f}}}},
        {MoveUp, "up", "UP / CLIMB", {{Key::Up, Key::W}, {GamepadButton::DpadUp}, {{GamepadAxis::LeftY, -1.0f}}}},
        {MoveDown, "down", "DOWN / DIVE", {{Key::Down, Key::S}, {GamepadButton::DpadDown}, {{GamepadAxis::LeftY, 1.0f}}}},
        {Jump, "jump", "JUMP", {{Key::Space, Key::Z}, {GamepadButton::South}, {}}},
        {Map, "map", "MAP", {{Key::M}, {GamepadButton::Back}, {}}},
        {Pause, "pause", "PAUSE", {{Key::Escape, Key::P}, {GamepadButton::Start}, {}}},
        {Throw, "throw", "THROW DYNAMITE", {{Key::X}, {GamepadButton::West}, {}}},
    };
    return table;
}

std::string keyLabel(Key key) {
    if (key == Key::Escape) {
        return "ESC";
    }
    std::string name(animal::platform::keyName(key));
    std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return name;
}

} // namespace

const std::vector<SandboxGame::ActionInfo>& SandboxGame::actionInfos() {
    return actionTable();
}

animal::ActionBinding SandboxGame::bindingFor(const ActionInfo& info) const {
    animal::ActionBinding binding = info.defaults;
    if (const auto it = settings_.bindings.find(info.name); it != settings_.bindings.end()) {
        binding.keys = it->second.keys;
        binding.buttons = it->second.buttons;
    }
    return binding;
}

void SandboxGame::bindActions() {
    for (const ActionInfo& info : actionInfos()) {
        actions_.bind(info.id, bindingFor(info));
    }
}

bool SandboxGame::usingGamepad() const {
    return input_ && input_->lastDevice() == animal::platform::InputDevice::Gamepad;
}

std::string SandboxGame::prompt(Action action) const {
    const animal::ActionBinding& b = actions_.binding(action);
    if (usingGamepad()) {
        if (!b.buttons.empty()) {
            return animal::platform::gamepadButtonLabel(b.buttons.front());
        }
        if (!b.axes.empty()) {
            return "STICK";
        }
    }
    if (!b.keys.empty()) {
        return keyLabel(b.keys.front());
    }
    return b.buttons.empty() ? "-" : animal::platform::gamepadButtonLabel(b.buttons.front());
}

std::string SandboxGame::inputLabel(Key key) {
    return keyLabel(key);
}

} // namespace game
