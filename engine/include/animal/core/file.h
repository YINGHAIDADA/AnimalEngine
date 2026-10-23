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

class Pack;

// Resource packs (ADR-0031): once a pack is mounted at a directory (e.g. the data directory), every
// path under that directory is read from the pack only (readFile, fileExists), so a release cannot
// depend on loose files by accident. Paths elsewhere (saves, settings) still use the disk.
bool mountPack(const std::string& packPath, const std::string& mountDirectory, std::string* error = nullptr);
void unmountPacks();
const Pack* packMountedAt(const std::string& path); // the pack serving `path`, if any

} // namespace animal
