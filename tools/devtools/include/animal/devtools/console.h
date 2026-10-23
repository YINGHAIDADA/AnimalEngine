#pragma once

#include <animal/core/log.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

namespace animal::devtools {

// Keeps the most recent log messages (a log sink) and shows them in a window.
class Console {
public:
    static constexpr size_t kMaxLines = 2000;

    Console() = default;
    ~Console();
    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;

    void attach();
    void detach();

    void draw(bool* open);
    // Warnings / errors logged since the window was last looked at (shown in the menu bar).
    uint32_t unseenWarnings() const { return unseenWarnings_; }
    uint32_t unseenErrors() const { return unseenErrors_; }

private:
    struct Line {
        log::Level level;
        std::string text;
    };

    void add(log::Level level, std::string_view text);

    std::mutex mutex_;
    std::deque<Line> lines_;
    int sink_ = 0;
    uint32_t unseenWarnings_ = 0;
    uint32_t unseenErrors_ = 0;
    bool scrollToBottom_ = true;
    bool showDebug_ = false;
    char filter_[128] = {};
};

} // namespace animal::devtools
