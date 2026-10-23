#pragma once

// Asset Cooker (ADR-0031): source assets -> one deterministic resource pack, plus a report.

#include <animal/core/pack.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace animal::cooker {

struct Options {
    std::string manifest;        // assets/cook.json
    std::string sourceRoot;      // assets/source/ (trailing '/')
    std::string shaderDirectory; // compiled .ashd packages (trailing '/')
    int level = 19;              // zstd
};

struct Category {
    uint32_t files = 0;
    uint64_t sourceBytes = 0; // the source files it was made from
    uint64_t cookedBytes = 0; // entries before compression
    uint64_t packedBytes = 0; // entries as stored (shared contents counted once)
};

struct Result {
    bool ok = false;
    std::vector<std::byte> pack;
    std::string dataVersion;
    std::vector<std::string> errors;   // missing references, invalid data: the cook fails
    std::vector<std::string> warnings; // e.g. unreferenced files
    std::map<std::string, Category> categories; // "shaders", "textures", "world"
    std::vector<std::vector<std::string>> duplicates; // source files with identical contents
    std::vector<std::string> unreferenced;            // source files nothing refers to
    struct {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t materials = 0;
        double coverage = 0.0; // share of the atlas covered by materials
    } atlas;
    PackStats stats;
    std::vector<std::pair<std::string, uint64_t>> entries; // pack path, cooked size
};

Result cook(const Options& options);
std::string reportJson(const Result& result);
std::string reportText(const Result& result);

// Mounts a cooked pack and loads everything the game would, through the runtime file layer.
// Returns the problems found (empty: the pack is complete and valid).
std::vector<std::string> verifyPack(const std::string& packPath, const std::string& manifestPath);

} // namespace animal::cooker
