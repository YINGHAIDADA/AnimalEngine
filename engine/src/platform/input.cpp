#include "gamepads.h"

#include <animal/core/log.h>
#include <animal/platform/input.h>

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <string>
#include <cmath>
#include <iterator>
#include <vector>

namespace animal::platform {

namespace {

// Indexed by Key.
constexpr SDL_Scancode kScancodes[] = {
    SDL_SCANCODE_LEFT,         SDL_SCANCODE_RIGHT,       SDL_SCANCODE_UP,         SDL_SCANCODE_DOWN,
    SDL_SCANCODE_A,            SDL_SCANCODE_B,           SDL_SCANCODE_C,          SDL_SCANCODE_D,
    SDL_SCANCODE_E,            SDL_SCANCODE_F,           SDL_SCANCODE_G,          SDL_SCANCODE_H,
    SDL_SCANCODE_I,            SDL_SCANCODE_J,           SDL_SCANCODE_K,          SDL_SCANCODE_L,
    SDL_SCANCODE_M,            SDL_SCANCODE_N,           SDL_SCANCODE_O,          SDL_SCANCODE_P,
    SDL_SCANCODE_Q,            SDL_SCANCODE_R,           SDL_SCANCODE_S,          SDL_SCANCODE_T,
    SDL_SCANCODE_U,            SDL_SCANCODE_V,           SDL_SCANCODE_W,          SDL_SCANCODE_X,
    SDL_SCANCODE_Y,            SDL_SCANCODE_Z,           SDL_SCANCODE_0,          SDL_SCANCODE_1,
    SDL_SCANCODE_2,            SDL_SCANCODE_3,           SDL_SCANCODE_4,          SDL_SCANCODE_5,
    SDL_SCANCODE_6,            SDL_SCANCODE_7,           SDL_SCANCODE_8,          SDL_SCANCODE_9,
    SDL_SCANCODE_SPACE,        SDL_SCANCODE_RETURN,      SDL_SCANCODE_ESCAPE,     SDL_SCANCODE_TAB,
    SDL_SCANCODE_BACKSPACE,    SDL_SCANCODE_DELETE,      SDL_SCANCODE_INSERT,     SDL_SCANCODE_HOME,
    SDL_SCANCODE_END,          SDL_SCANCODE_PAGEUP,      SDL_SCANCODE_PAGEDOWN,   SDL_SCANCODE_LSHIFT,
    SDL_SCANCODE_RSHIFT,       SDL_SCANCODE_LCTRL,       SDL_SCANCODE_RCTRL,      SDL_SCANCODE_LALT,
    SDL_SCANCODE_RALT,         SDL_SCANCODE_LGUI,        SDL_SCANCODE_RGUI,       SDL_SCANCODE_GRAVE,
    SDL_SCANCODE_MINUS,        SDL_SCANCODE_EQUALS,      SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET,
    SDL_SCANCODE_BACKSLASH,    SDL_SCANCODE_SEMICOLON,   SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_COMMA,
    SDL_SCANCODE_PERIOD,       SDL_SCANCODE_SLASH,       SDL_SCANCODE_KP_ENTER,   SDL_SCANCODE_F1,
    SDL_SCANCODE_F2,           SDL_SCANCODE_F3,          SDL_SCANCODE_F4,         SDL_SCANCODE_F5,
    SDL_SCANCODE_F6,           SDL_SCANCODE_F7,          SDL_SCANCODE_F8,         SDL_SCANCODE_F9,
    SDL_SCANCODE_F10,          SDL_SCANCODE_F11,         SDL_SCANCODE_F12,
};
static_assert(std::size(kScancodes) == static_cast<size_t>(Key::Count), "Key table out of sync");

constexpr const char* kKeyNames[] = {
    "Left", "Right", "Up", "Down", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
    "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "Num0", "Num1", "Num2", "Num3", "Num4", "Num5",
    "Num6", "Num7", "Num8", "Num9", "Space", "Enter", "Escape", "Tab", "Backspace", "Delete", "Insert", "Home",
    "End", "PageUp", "PageDown", "LeftShift", "RightShift", "LeftCtrl", "RightCtrl", "LeftAlt", "RightAlt",
    "LeftSuper", "RightSuper", "Grave", "Minus", "Equals", "LeftBracket", "RightBracket", "Backslash",
    "Semicolon", "Apostrophe", "Comma", "Period", "Slash", "KeypadEnter", "F1", "F2", "F3", "F4", "F5", "F6",
    "F7", "F8", "F9", "F10", "F11", "F12",
};
static_assert(std::size(kKeyNames) == static_cast<size_t>(Key::Count), "Key name table out of sync");

constexpr SDL_GamepadButton kButtons[] = {
    SDL_GAMEPAD_BUTTON_SOUTH,     SDL_GAMEPAD_BUTTON_EAST,          SDL_GAMEPAD_BUTTON_WEST,
    SDL_GAMEPAD_BUTTON_NORTH,     SDL_GAMEPAD_BUTTON_DPAD_UP,       SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
    SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_BACK,     SDL_GAMEPAD_BUTTON_START,
};
static_assert(std::size(kButtons) == static_cast<size_t>(GamepadButton::Count), "Button table out of sync");

constexpr SDL_GamepadAxis kAxes[] = {SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY};
static_assert(std::size(kAxes) == static_cast<size_t>(GamepadAxis::Count), "Axis table out of sync");

constexpr SDL_MouseButtonFlags kMouseMasks[] = {SDL_BUTTON_LMASK, SDL_BUTTON_RMASK, SDL_BUTTON_MMASK};
static_assert(std::size(kMouseMasks) == static_cast<size_t>(MouseButton::Count), "Mouse table out of sync");

float applyDeadzone(float value, float deadzone) {
    const float magnitude = std::fabs(value);
    if (magnitude <= deadzone) {
        return 0.0f;
    }
    const float scaled = std::min(1.0f, (magnitude - deadzone) / (1.0f - deadzone));
    return value < 0.0f ? -scaled : scaled;
}

} // namespace

namespace detail {

namespace {
std::vector<SDL_Gamepad*> gGamepads;
}

void handleGamepadEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        if (SDL_Gamepad* gamepad = SDL_OpenGamepad(event.gdevice.which)) {
            gGamepads.push_back(gamepad);
            log::info("Gamepad connected: {}", SDL_GetGamepadName(gamepad));
        }
    } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        const auto it = std::find_if(gGamepads.begin(), gGamepads.end(), [&](SDL_Gamepad* g) {
            return SDL_GetGamepadID(g) == event.gdevice.which;
        });
        if (it != gGamepads.end()) {
            log::info("Gamepad disconnected: {}", SDL_GetGamepadName(*it));
            SDL_CloseGamepad(*it);
            gGamepads.erase(it);
        }
    }
}

SDL_Gamepad* activeGamepad() {
    return gGamepads.empty() ? nullptr : gGamepads.back();
}

void closeAllGamepads() {
    for (SDL_Gamepad* gamepad : gGamepads) {
        SDL_CloseGamepad(gamepad);
    }
    gGamepads.clear();
}

Key keyFromScancode(SDL_Scancode scancode) {
    const auto it = std::find(std::begin(kScancodes), std::end(kScancodes), scancode);
    return it == std::end(kScancodes) ? Key::Count : static_cast<Key>(it - std::begin(kScancodes));
}

KeyModifiers modifiersFrom(SDL_Keymod mods) {
    return {(mods & SDL_KMOD_SHIFT) != 0, (mods & SDL_KMOD_CTRL) != 0, (mods & SDL_KMOD_ALT) != 0,
            (mods & SDL_KMOD_GUI) != 0};
}

} // namespace detail

std::optional<Key> keyFromName(std::string_view name) {
    if (name == "Shortcut") {
#if defined(__APPLE__)
        return Key::LeftSuper;
#else
        return Key::LeftCtrl;
#endif
    }
    const auto it = std::find(std::begin(kKeyNames), std::end(kKeyNames), name);
    if (it == std::end(kKeyNames)) {
        return std::nullopt;
    }
    return static_cast<Key>(it - std::begin(kKeyNames));
}

namespace {
constexpr const char* kGamepadButtonNames[] = {"South",    "East",      "West",         "North",
                                               "DpadUp",   "DpadDown",  "DpadLeft",     "DpadRight",
                                               "LeftShoulder", "RightShoulder", "Back", "Start"};
static_assert(std::size(kGamepadButtonNames) == static_cast<size_t>(GamepadButton::Count),
              "Gamepad button name table out of sync");
} // namespace

std::optional<GamepadButton> gamepadButtonFromName(std::string_view name) {
    const auto it = std::find(std::begin(kGamepadButtonNames), std::end(kGamepadButtonNames), name);
    if (it == std::end(kGamepadButtonNames)) {
        return std::nullopt;
    }
    return static_cast<GamepadButton>(it - std::begin(kGamepadButtonNames));
}

std::string_view gamepadButtonName(GamepadButton button) {
    const auto index = static_cast<size_t>(button);
    return index < std::size(kGamepadButtonNames) ? std::string_view(kGamepadButtonNames[index]) : std::string_view();
}

std::string_view keyName(Key key) {
    const auto index = static_cast<size_t>(key);
    return index < std::size(kKeyNames) ? std::string_view(kKeyNames[index]) : std::string_view();
}

void Input::update(const FrameEvents& events) {
    wheel_ = 0.0f;
    for (const InputEvent& event : events.input) {
        if (event.type == InputEvent::Type::MouseWheel) {
            wheel_ += event.y;
        }
        if (!event.synthetic) {
            continue;
        }
        if (event.type == InputEvent::Type::Key && event.key != Key::Count) {
            syntheticKeys_[index(event.key)] = event.down;
        } else if (event.type == InputEvent::Type::MouseButton) {
            syntheticButtons_[index(event.button)] = event.down;
        } else if (event.type == InputEvent::Type::MouseMove) {
            syntheticMouse_ = MousePosition{event.x, event.y};
        }
    }

    keys_.previous = keys_.current;
    int count = 0;
    const bool* state = SDL_GetKeyboardState(&count);
    for (size_t i = 0; i < keys_.current.size(); ++i) {
        const int scancode = kScancodes[i];
        keys_.current[i] = (scancode < count && state[scancode]) || syntheticKeys_[i];
    }

    mouseButtons_.previous = mouseButtons_.current;
    const SDL_MouseButtonFlags mouseState = SDL_GetMouseState(&mouse_.x, &mouse_.y);
    if (syntheticMouse_) {
        mouse_ = *syntheticMouse_;
    }
    for (size_t i = 0; i < mouseButtons_.current.size(); ++i) {
        mouseButtons_.current[i] = (mouseState & kMouseMasks[i]) != 0 || syntheticButtons_[i];
    }

    buttons_.previous = buttons_.current;
    SDL_Gamepad* gamepad = detail::activeGamepad();
    gamepadConnected_ = gamepad != nullptr;
    for (size_t i = 0; i < buttons_.current.size(); ++i) {
        buttons_.current[i] = gamepad && SDL_GetGamepadButton(gamepad, kButtons[i]);
    }
    for (size_t i = 0; i < axes_.size(); ++i) {
        const float raw = gamepad ? static_cast<float>(SDL_GetGamepadAxis(gamepad, kAxes[i])) / 32767.0f : 0.0f;
        axes_[i] = applyDeadzone(std::clamp(raw, -1.0f, 1.0f), kStickDeadzone);
    }

    bool keyPressed = false;
    for (size_t i = 0; i < keys_.current.size() && !keyPressed; ++i) {
        keyPressed = keys_.current[i] && !keys_.previous[i];
    }
    bool padUsed = false;
    for (size_t i = 0; i < buttons_.current.size() && !padUsed; ++i) {
        padUsed = buttons_.current[i] && !buttons_.previous[i];
    }
    for (const float a : axes_) {
        padUsed = padUsed || std::fabs(a) > 0.5f;
    }
    if (keyPressed) {
        lastDevice_ = InputDevice::Keyboard;
    } else if (padUsed) {
        lastDevice_ = InputDevice::Gamepad;
    } else if (!gamepadConnected_) {
        lastDevice_ = InputDevice::Keyboard;
    }
}

std::string gamepadButtonLabel(GamepadButton button) {
    SDL_Gamepad* gamepad = detail::activeGamepad();
    const SDL_GamepadType type = gamepad ? SDL_GetGamepadType(gamepad) : SDL_GAMEPAD_TYPE_XBOXONE;
    const bool playstation = type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5;
    const bool nintendo = type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO ||
                          type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR ||
                          type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
                          type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT;
    switch (button) {
    case GamepadButton::South:
    case GamepadButton::East:
    case GamepadButton::West:
    case GamepadButton::North:
        switch (SDL_GetGamepadButtonLabelForType(type, kButtons[static_cast<size_t>(button)])) {
        case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
        case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
        case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
        case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "CROSS";
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "CIRCLE";
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "SQUARE";
        case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "TRIANGLE";
        default: return std::string(gamepadButtonName(button));
        }
    case GamepadButton::DpadUp: return "D-PAD UP";
    case GamepadButton::DpadDown: return "D-PAD DOWN";
    case GamepadButton::DpadLeft: return "D-PAD LEFT";
    case GamepadButton::DpadRight: return "D-PAD RIGHT";
    case GamepadButton::LeftShoulder: return playstation ? "L1" : (nintendo ? "L" : "LB");
    case GamepadButton::RightShoulder: return playstation ? "R1" : (nintendo ? "R" : "RB");
    case GamepadButton::Back: return playstation ? "SHARE" : (nintendo ? "-" : "VIEW");
    case GamepadButton::Start: return playstation ? "OPTIONS" : (nintendo ? "+" : "MENU");
    case GamepadButton::Count: break;
    }
    return {};
}

KeyModifiers Input::modifiers() const {
    if (keyboardCaptured_) {
        return {};
    }
    KeyModifiers m = detail::modifiersFrom(SDL_GetModState());
    const auto held = [this](Key a, Key b) { return syntheticKeys_[index(a)] || syntheticKeys_[index(b)]; };
    m.shift = m.shift || held(Key::LeftShift, Key::RightShift);
    m.ctrl = m.ctrl || held(Key::LeftCtrl, Key::RightCtrl);
    m.alt = m.alt || held(Key::LeftAlt, Key::RightAlt);
    m.super = m.super || held(Key::LeftSuper, Key::RightSuper);
    return m;
}

} // namespace animal::platform
