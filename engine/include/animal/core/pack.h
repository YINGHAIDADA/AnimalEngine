#pragma once

// Resource packs (.pak, ADR-0031): the cooked data of a release in one file.
//
//   header   "APAK", format version, entry count, data version length, TOC offset / size,
//            content hash (of the TOC, which holds every entry's hash)
//   data     the data version string, then each entry's stored bytes (identical contents once)
//   TOC      per entry: path (UTF-8, '/' separated, relative to the data directory), offset,
//            stored size, size, compression, hash of the uncompressed bytes
//
// Little-endian; the writer sorts entries by path and stores no time stamps, so the same input
// always gives the same bytes.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace animal {

inline constexpr uint32_t kPackVersion = 1;

enum class PackCompression : uint32_t {
    None = 0,
    Zstd = 1,
};

struct PackEntry {
    std::string path;
    uint64_t offset = 0;
    uint64_t storedSize = 0;
    uint64_t size = 0;
    PackCompression compression = PackCompression::None;
    uint64_t hash = 0;
};

// 64-bit FNV-1a: entry integrity and duplicate detection (not security).
uint64_t contentHash(const void* data, size_t size);

class Pack {
public:
    // Reads the header and table of contents; the contents stay on disk until read.
    static std::unique_ptr<Pack> open(const std::string& path, std::string* error = nullptr);
    ~Pack();
    Pack(const Pack&) = delete;
    Pack& operator=(const Pack&) = delete;

    const PackEntry* find(std::string_view path) const;
    // Decompresses and checks the entry's hash.
    std::optional<std::vector<std::byte>> read(const PackEntry& entry, std::string* error = nullptr) const;

    const std::vector<PackEntry>& entries() const { return entries_; }
    const std::string& dataVersion() const { return dataVersion_; }
    uint64_t hash() const { return hash_; }
    uint64_t fileSize() const { return fileSize_; }

private:
    Pack() = default;

    void* stream_ = nullptr; // SDL_IOStream
    mutable std::mutex mutex_;
    std::vector<PackEntry> entries_; // sorted by path
    std::string dataVersion_;
    uint64_t hash_ = 0;
    uint64_t fileSize_ = 0;
};

// --- Writing (the cooker and tests) -------------------------------------------------------------

struct PackInput {
    std::string path;
    std::vector<std::byte> data;
    bool compress = true;
};

struct PackStats {
    uint64_t inputBytes = 0;  // all entries, uncompressed
    uint64_t storedBytes = 0; // data section (after compression and de-duplication)
    uint32_t sharedEntries = 0; // entries whose contents were already stored for another path
};

// zstd `level` 1..22. Paths must be unique.
std::vector<std::byte> writePack(std::vector<PackInput> inputs, std::string_view dataVersion, int level = 19,
                                 PackStats* stats = nullptr);

} // namespace animal
