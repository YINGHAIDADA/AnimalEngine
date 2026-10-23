#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace animal::devtools {

// Polls modification times of every file under some directories (portable; no OS notification
// APIs). A change is reported once the file has stayed the same for one poll, so a file that an
// editor is still writing is not picked up half-written.
class FileWatcher {
public:
    explicit FileWatcher(double intervalSeconds = 0.25) : interval_(intervalSeconds) {}

    // Takes a snapshot of the directory tree; existing files are not reported.
    void watchDirectory(const std::string& directory);
    // Changed or new files since the last report (at most one scan per interval), sorted.
    std::vector<std::string> poll();
    // The tools wrote this file themselves: take its current state as known (not reported).
    void acknowledge(const std::string& path);

private:
    struct FileState {
        int64_t time = 0;
        uint64_t size = 0;
        bool pending = false; // changed at the last poll; reported once it stays unchanged
    };

    void scan(bool report, std::vector<std::string>& changed);

    std::vector<std::string> directories_;
    std::map<std::string, FileState> files_;
    double interval_;
    uint64_t lastPoll_ = 0;
};

} // namespace animal::devtools
