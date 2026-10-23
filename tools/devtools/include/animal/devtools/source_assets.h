#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace animal::devtools {

// Where the data the game loads comes from (ADR-0026). The game reads deployed copies
// (<exe>/data/...); developer builds also know the source tree they were built from
// (ANIMAL_SOURCE_DIR), which is the truth that tools edit and watch:
//   data/world/...        <->  assets/source/world/...
//   data/textures/...     <->  assets/source/textures/...
//   data/shaders/X.ashd   <->  shaders/X              (compiled by animal_shaderc)
// Without the source tree (a build moved to another machine) tools only touch the deployed copies.
class SourceAssets {
public:
    void init(const std::string& dataDirectory);

    bool available() const { return !sourceDirectory_.empty(); }
    const std::string& sourceDirectory() const { return sourceDirectory_; } // with trailing '/'
    const std::string& dataDirectory() const { return dataDirectory_; }

    std::optional<std::string> sourceOf(const std::string& dataPath) const;
    std::optional<std::string> dataOf(const std::string& sourcePath) const;
    // A short path for messages: relative to the data directory when inside it.
    std::string displayName(const std::string& path) const;

    // Writes the deployed copy and, when known, the source file (both atomically).
    bool write(const std::string& dataPath, std::string_view text, std::string* error = nullptr) const;
    // Copies a source file over its deployed copy (after an external edit).
    bool deploy(const std::string& sourcePath, std::string* error = nullptr) const;

private:
    std::string dataDirectory_;
    std::string sourceDirectory_;
};

std::string normalizePath(const std::string& path);

} // namespace animal::devtools
