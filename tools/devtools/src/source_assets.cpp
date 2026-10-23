#include <animal/devtools/source_assets.h>

#include <animal/core/file.h>
#include <animal/core/log.h>

#include <filesystem>
#include <iterator>

namespace animal::devtools {

namespace {

namespace fs = std::filesystem;

struct Root {
    const char* data;   // under the data directory
    const char* source; // under the source directory
    const char* dataSuffix; // appended to deployed names (compiled shaders)
};

constexpr Root kRoots[] = {
    {"world/", "assets/source/world/", ""},
    {"textures/", "assets/source/textures/", ""},
    {"shaders/", "shaders/", ".ashd"},
};

fs::path toPath(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::string fromPath(const fs::path& p) {
    const std::u8string s = p.generic_u8string();
    return {s.begin(), s.end()};
}

} // namespace

std::string normalizePath(const std::string& path) {
    return fromPath(toPath(path).lexically_normal());
}

void SourceAssets::init(const std::string& dataDirectory) {
    dataDirectory_ = normalizePath(dataDirectory);
    if (!dataDirectory_.empty() && dataDirectory_.back() != '/') {
        dataDirectory_ += '/';
    }
    sourceDirectory_.clear();
#if defined(ANIMAL_SOURCE_DIR)
    std::string source = normalizePath(ANIMAL_SOURCE_DIR);
    if (!source.empty() && source.back() != '/') {
        source += '/';
    }
    std::error_code ec;
    if (fs::is_directory(toPath(source + "assets/source"), ec)) {
        sourceDirectory_ = source;
        log::info("Developer tools: source assets in {}", sourceDirectory_);
    } else {
        log::warn("Developer tools: source tree {} not found; edits only change the deployed data", source);
    }
#endif
}

std::optional<std::string> SourceAssets::sourceOf(const std::string& dataPath) const {
    if (!available()) {
        return std::nullopt;
    }
    const std::string path = normalizePath(dataPath);
    if (!path.starts_with(dataDirectory_)) {
        return std::nullopt;
    }
    const std::string relative = path.substr(dataDirectory_.size());
    for (const Root& root : kRoots) {
        const std::string_view suffix = root.dataSuffix;
        if (relative.starts_with(root.data) && relative.ends_with(suffix)) {
            const std::string inner = relative.substr(std::char_traits<char>::length(root.data));
            return sourceDirectory_ + root.source + inner.substr(0, inner.size() - suffix.size());
        }
    }
    return std::nullopt;
}

std::optional<std::string> SourceAssets::dataOf(const std::string& sourcePath) const {
    if (!available()) {
        return std::nullopt;
    }
    const std::string path = normalizePath(sourcePath);
    if (!path.starts_with(sourceDirectory_)) {
        return std::nullopt;
    }
    const std::string relative = path.substr(sourceDirectory_.size());
    for (const Root& root : kRoots) {
        if (relative.starts_with(root.source)) {
            return dataDirectory_ + root.data + relative.substr(std::char_traits<char>::length(root.source)) +
                   root.dataSuffix;
        }
    }
    return std::nullopt;
}

std::string SourceAssets::displayName(const std::string& path) const {
    const std::string normalized = normalizePath(path);
    if (normalized.starts_with(dataDirectory_)) {
        return normalized.substr(dataDirectory_.size());
    }
    if (available() && normalized.starts_with(sourceDirectory_)) {
        return normalized.substr(sourceDirectory_.size());
    }
    return normalized;
}

bool SourceAssets::write(const std::string& dataPath, std::string_view text, std::string* error) const {
    const auto fail = [error](std::string message) {
        if (error) {
            *error = std::move(message);
        }
        return false;
    };
    if (const std::optional<std::string> source = sourceOf(dataPath)) {
        if (!writeFileAtomic(*source, text.data(), text.size())) {
            return fail("cannot write " + *source);
        }
    }
    if (!writeFileAtomic(dataPath, text.data(), text.size())) {
        return fail("cannot write " + dataPath);
    }
    return true;
}

bool SourceAssets::deploy(const std::string& sourcePath, std::string* error) const {
    const std::optional<std::string> target = dataOf(sourcePath);
    if (!target) {
        if (error) {
            *error = sourcePath + " is not under a deployed source directory";
        }
        return false;
    }
    std::string readError;
    const auto bytes = readFile(sourcePath, &readError);
    if (!bytes) {
        if (error) {
            *error = readError;
        }
        return false;
    }
    std::error_code ec;
    fs::create_directories(toPath(*target).parent_path(), ec);
    if (!writeFileAtomic(*target, bytes->data(), bytes->size())) {
        if (error) {
            *error = "cannot write " + *target;
        }
        return false;
    }
    return true;
}

} // namespace animal::devtools
