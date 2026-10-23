#include <animal/core/log.h>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <format>
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

struct FileLog {
    std::mutex mutex;
    SDL_IOStream* stream = nullptr;
    std::string path;
};

FileLog& fileLog() {
    static FileLog instance;
    return instance;
}

const char* levelName(Level level) {
    switch (level) {
    case Level::Debug:
        return "debug";
    case Level::Info:
        return "info";
    case Level::Warn:
        return "WARN";
    case Level::Error:
        return "ERROR";
    }
    return "?";
}

void writeToFile(Level level, std::string_view message) {
    FileLog& f = fileLog();
    const std::lock_guard lock(f.mutex);
    if (!f.stream) {
        return;
    }
    const std::string line = std::format("{:10.3f} {:<5} {}\n", static_cast<double>(SDL_GetTicksNS()) * 1e-9,
                                         levelName(level), message);
    SDL_WriteIO(f.stream, line.data(), line.size());
    if (level >= Level::Warn) {
        SDL_FlushIO(f.stream);
    }
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
    writeToFile(level, text);
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

bool startFileLog(const std::string& directory, const std::string& name, int keep) {
    stopFileLog();
    std::string dir = directory;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
        dir += '/';
    }
    SDL_CreateDirectory(dir.c_str());
    const auto pathOf = [&](int index) {
        return index == 0 ? dir + name + ".log" : dir + name + "." + std::to_string(index) + ".log";
    };
    SDL_RemovePath(pathOf(keep).c_str());
    for (int i = keep - 1; i >= 0; --i) {
        SDL_RenamePath(pathOf(i).c_str(), pathOf(i + 1).c_str());
    }
    FileLog& f = fileLog();
    {
        const std::lock_guard lock(f.mutex);
        f.stream = SDL_IOFromFile(pathOf(0).c_str(), "wb");
        if (!f.stream) {
            return false;
        }
        f.path = pathOf(0);
        const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char date[64] = {};
        std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        const std::string header = std::format("Animal Engine log, started {}\n", date);
        SDL_WriteIO(f.stream, header.data(), header.size());
    }
    return true;
}

void flushFileLog() {
    FileLog& f = fileLog();
    const std::lock_guard lock(f.mutex);
    if (f.stream) {
        SDL_FlushIO(f.stream);
    }
}

void stopFileLog() {
    FileLog& f = fileLog();
    const std::lock_guard lock(f.mutex);
    if (f.stream) {
        SDL_CloseIO(f.stream);
        f.stream = nullptr;
    }
    f.path.clear();
}

const std::string& fileLogPath() {
    return fileLog().path;
}

} // namespace animal::log
