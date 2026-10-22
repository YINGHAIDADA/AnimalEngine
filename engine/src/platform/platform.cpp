#include <animal/core/log.h>
#include <animal/platform/platform.h>

#include <SDL3/SDL.h>

namespace animal::platform {

bool initialize() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        log::error("SDL_Init failed: {}", SDL_GetError());
        return false;
    }
    log::info("SDL {}.{}.{} ({})", SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
              SDL_VERSIONNUM_MICRO(SDL_GetVersion()), SDL_GetPlatform());
    return true;
}

void shutdown() {
    SDL_Quit();
}

FrameEvents pollEvents() {
    FrameEvents events;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            events.quitRequested = true;
        }
    }
    return events;
}

uint64_t nowNanoseconds() {
    return SDL_GetTicksNS();
}

std::string executableDirectory() {
    const char* base = SDL_GetBasePath();
    return base ? std::string(base) : std::string("./");
}

} // namespace animal::platform
