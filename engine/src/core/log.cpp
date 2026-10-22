#include <animal/core/log.h>

#include <SDL3/SDL_log.h>

#include <atomic>
#include <string>

namespace animal::log {

namespace {

std::atomic<Level> gMinimumLevel{Level::Debug};

SDL_LogPriority toSdlPriority(Level level) {
    switch (level) {
    case Level::Debug:
        return SDL_LOG_PRIORITY_DEBUG;
    case Level::Info:
        return SDL_LOG_PRIORITY_INFO;
    case Level::Warn:
        return SDL_LOG_PRIORITY_WARN;
    case Level::Error:
        return SDL_LOG_PRIORITY_ERROR;
    }
    return SDL_LOG_PRIORITY_INFO;
}

} // namespace

void setMinimumLevel(Level level) {
    gMinimumLevel = level;
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, toSdlPriority(level));
}

void write(Level level, std::string_view message) {
    if (level < gMinimumLevel.load()) {
        return;
    }
    const std::string text(message);
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, toSdlPriority(level), "%s", text.c_str());
}

} // namespace animal::log
