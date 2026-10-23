#include <animal/devtools/file_watcher.h>

#include <animal/devtools/source_assets.h>
#include <animal/platform/platform.h>

#include <filesystem>

namespace animal::devtools {

namespace {

namespace fs = std::filesystem;

bool stateOf(const fs::path& path, int64_t& time, uint64_t& size) {
    std::error_code ec;
    const auto t = fs::last_write_time(path, ec);
    if (ec) {
        return false;
    }
    const auto s = fs::file_size(path, ec);
    if (ec) {
        return false;
    }
    time = static_cast<int64_t>(t.time_since_epoch().count());
    size = static_cast<uint64_t>(s);
    return true;
}

std::string key(const fs::path& path) {
    const std::u8string s = path.lexically_normal().generic_u8string();
    return {s.begin(), s.end()};
}

} // namespace

void FileWatcher::watchDirectory(const std::string& directory) {
    directories_.push_back(directory);
    std::vector<std::string> ignored;
    scan(false, ignored);
}

std::vector<std::string> FileWatcher::poll() {
    std::vector<std::string> changed;
    const uint64_t now = platform::nowNanoseconds();
    if (lastPoll_ != 0 && static_cast<double>(now - lastPoll_) * 1e-9 < interval_) {
        return changed;
    }
    lastPoll_ = now;
    scan(true, changed);
    return changed;
}

void FileWatcher::acknowledge(const std::string& path) {
    FileState& state = files_[normalizePath(path)];
    stateOf(fs::path(std::u8string(path.begin(), path.end())), state.time, state.size);
    state.pending = false;
}

void FileWatcher::scan(bool report, std::vector<std::string>& changed) {
    for (const std::string& directory : directories_) {
        std::error_code ec;
        fs::recursive_directory_iterator it(fs::path(std::u8string(directory.begin(), directory.end())), ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec)) {
                continue;
            }
            const std::u8string u8 = it->path().filename().u8string();
            const std::string name(u8.begin(), u8.end());
            if (name.empty() || name[0] == '.' || name.ends_with(".tmp") || name.ends_with("~")) {
                continue; // hidden, editor backups and atomic-write temporaries
            }
            int64_t time = 0;
            uint64_t size = 0;
            if (!stateOf(it->path(), time, size)) {
                continue;
            }
            const std::string path = key(it->path());
            const auto found = files_.find(path);
            if (found == files_.end()) {
                files_[path] = {time, size, report};
                continue;
            }
            FileState& state = found->second;
            if (state.time != time || state.size != size) {
                state = {time, size, true}; // wait one more poll for the writer to finish
            } else if (state.pending) {
                state.pending = false;
                if (report) {
                    changed.push_back(path);
                }
            }
        }
    }
}

} // namespace animal::devtools
