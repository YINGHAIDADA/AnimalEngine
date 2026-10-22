#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace animal {

// Paths are UTF-8 on every platform.
std::optional<std::vector<std::byte>> readFile(const std::string& path, std::string* error = nullptr);
bool fileExists(const std::string& path);
bool deleteFile(const std::string& path);
// Writes to "<path>.tmp" then renames over `path`, so a crash never leaves a half-written file.
bool writeFileAtomic(const std::string& path, const void* data, size_t size);

} // namespace animal
