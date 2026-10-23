#pragma once

#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace animal::log {

enum class Level {
    Debug,
    Info,
    Warn,
    Error,
};

void write(Level level, std::string_view message);
void setMinimumLevel(Level level);

// Extra receivers of every message that passes the minimum level (e.g. an in-game console).
// Called on the logging thread; sinks must not log themselves.
using Sink = std::function<void(Level level, std::string_view message)>;
int addSink(Sink sink);
void removeSink(int id);

// Copies every message to <directory>/<name>.log (ADR-0032), keeping the previous `keep` runs as
// <name>.1.log ... <name>.<keep>.log. Warnings and errors are flushed at once.
bool startFileLog(const std::string& directory, const std::string& name, int keep = 5);
void flushFileLog();
void stopFileLog();
const std::string& fileLogPath(); // empty when not logging to a file

template <typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace animal::log
