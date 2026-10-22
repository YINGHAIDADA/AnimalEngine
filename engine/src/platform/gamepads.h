#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>

namespace animal::platform::detail {

// Opens / closes gamepads as they are connected; the most recently connected one is active.
void handleGamepadEvent(const SDL_Event& event);
SDL_Gamepad* activeGamepad();
void closeAllGamepads();

} // namespace animal::platform::detail
