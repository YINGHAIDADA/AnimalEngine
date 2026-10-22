#include "gamepads.h"

#include <animal/core/log.h>
#include <animal/platform/input.h>

#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

namespace animal::platform {

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

} // namespace detail

namespace {

constexpr SDL_Scancode kScancodes[] = {
    SDL_SCANCODE_LEFT,  SDL_SCANCODE_RIGHT, SDL_SCANCODE_UP,     SDL_SCANCODE_DOWN,   SDL_SCANCODE_W,
    SDL_SCANCODE_A,     SDL_SCANCODE_S,     SDL_SCANCODE_D,      SDL_SCANCODE_Z,      SDL_SCANCODE_X,
    SDL_SCANCODE_C,     SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_F1,
    SDL_SCANCODE_F2,    SDL_SCANCODE_F3,     SDL_SCANCODE_F4,     SDL_SCANCODE_F5,     SDL_SCANCODE_F6,
    SDL_SCANCODE_F7,     SDL_SCANCODE_F8,     SDL_SCANCODE_F9,     SDL_SCANCODE_F10,     SDL_SCANCODE_F12,
};
static_assert(std::size(kScancodes) == static_cast<size_t>(Key::Count), "Key table out of sync");

constexpr SDL_GamepadButton kButtons[] = {
    SDL_GAMEPAD_BUTTON_SOUTH,     SDL_GAMEPAD_BUTTON_EAST,          SDL_GAMEPAD_BUTTON_WEST,
    SDL_GAMEPAD_BUTTON_NORTH,     SDL_GAMEPAD_BUTTON_DPAD_UP,       SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
    SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_BACK,     SDL_GAMEPAD_BUTTON_START,
};
static_assert(std::size(kButtons) == static_cast<size_t>(GamepadButton::Count), "Button table out of sync");

constexpr SDL_GamepadAxis kAxes[] = {SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY};
static_assert(std::size(kAxes) == static_cast<size_t>(GamepadAxis::Count), "Axis table out of sync");

float applyDeadzone(float value, float deadzone) {
    const float magnitude = std::fabs(value);
    if (magnitude <= deadzone) {
        return 0.0f;
    }
    const float scaled = std::min(1.0f, (magnitude - deadzone) / (1.0f - deadzone));
    return value < 0.0f ? -scaled : scaled;
}

} // namespace

void Input::update() {
    keys_.previous = keys_.current;
    int count = 0;
    const bool* state = SDL_GetKeyboardState(&count);
    for (size_t i = 0; i < keys_.current.size(); ++i) {
        const int scancode = kScancodes[i];
        keys_.current[i] = scancode < count && state[scancode];
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
}

} // namespace animal::platform
