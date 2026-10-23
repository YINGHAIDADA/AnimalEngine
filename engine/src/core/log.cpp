#include <animal/core/log.h>

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace animal::log {

namespace {

std::atomic<Level> gMinimumLevel{Level::Debug};

struct Sinks {
    std::mutex mutex;
    std::vector<std::pair<int, Sink>> entries;
    int nextId = 1;
};

Sinks& sinks() {
    static Sinks instance;
    return instance;
}

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
    Sinks& s = sinks();
    const std::lock_guard lock(s.mutex);
    for (const auto& [id, sink] : s.entries) {
        sink(level, text);
    }
}

int addSink(Sink sink) {
    Sinks& s = sinks();
    const std::lock_guard lock(s.mutex);
    const int id = s.nextId++;
    s.entries.emplace_back(id, std::move(sink));
    return id;
}

void removeSink(int id) {
    Sinks& s = sinks();
    const std::lock_guard lock(s.mutex);
    std::erase_if(s.entries, [id](const auto& entry) { return entry.first == id; });
}

} // namespace animal::log
