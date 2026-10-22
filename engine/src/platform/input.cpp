#include <animal/platform/input.h>

#include <SDL3/SDL_keyboard.h>

#include <iterator>

namespace animal::platform {

namespace {

constexpr SDL_Scancode kScancodes[] = {
    SDL_SCANCODE_LEFT,  SDL_SCANCODE_RIGHT, SDL_SCANCODE_UP,     SDL_SCANCODE_DOWN, SDL_SCANCODE_W,
    SDL_SCANCODE_A,     SDL_SCANCODE_S,     SDL_SCANCODE_D,      SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN,
    SDL_SCANCODE_ESCAPE, SDL_SCANCODE_F1,   SDL_SCANCODE_F12,
};
static_assert(std::size(kScancodes) == static_cast<size_t>(Key::Count), "Key table out of sync");

} // namespace

void Input::update() {
    previous_ = current_;
    int count = 0;
    const bool* state = SDL_GetKeyboardState(&count);
    for (size_t i = 0; i < current_.size(); ++i) {
        const int scancode = kScancodes[i];
        current_[i] = scancode < count && state[scancode];
    }
}

} // namespace animal::platform
