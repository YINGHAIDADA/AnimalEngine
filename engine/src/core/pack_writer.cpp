// Pack writing lives in its own object file: only the cooker and the tests link the zstd compressor.
#include <animal/core/pack.h>

#include <zstd.h>

#include <algorithm>
#include <cstring>
#include <map>

namespace animal {

namespace {

void put32(std::vector<std::byte>& out, uint32_t v) {
    const size_t at = out.size();
    out.resize(at + 4);
    std::memcpy(out.data() + at, &v, 4);
}

void put64(std::vector<std::byte>& out, uint64_t v) {
    const size_t at = out.size();
    out.resize(at + 8);
    std::memcpy(out.data() + at, &v, 8);
}

void put64At(std::vector<std::byte>& out, size_t at, uint64_t v) {
    std::memcpy(out.data() + at, &v, 8);
}

} // namespace

std::vector<std::byte> writePack(std::vector<PackInput> inputs, std::string_view dataVersion, int level,
                                 PackStats* stats) {
    std::ranges::sort(inputs, {}, &PackInput::path);
    PackStats s;
    std::vector<std::byte> out;
    out.reserve(64 * 1024);
    // Header (offsets and hash filled in at the end), then the data version.
    out.insert(out.end(), {std::byte{'A'}, std::byte{'P'}, std::byte{'A'}, std::byte{'K'}});
    put32(out, kPackVersion);
    put32(out, static_cast<uint32_t>(inputs.size()));
    put32(out, static_cast<uint32_t>(dataVersion.size()));
    put64(out, 0); // TOC offset
    put64(out, 0); // TOC size
    put64(out, 0); // content hash
    const auto* version = reinterpret_cast<const std::byte*>(dataVersion.data());
    out.insert(out.end(), version, version + dataVersion.size());

    struct Stored {
        uint64_t offset;
        uint64_t storedSize;
        PackCompression compression;
    };
    std::map<std::pair<uint64_t, uint64_t>, std::vector<std::pair<const PackInput*, Stored>>> byContent;
    std::vector<PackEntry> entries;
    for (const PackInput& input : inputs) {
        s.inputBytes += input.data.size();
        PackEntry e;
        e.path = input.path;
        e.size = input.data.size();
        e.hash = contentHash(input.data.data(), input.data.size());
        // Identical contents (same hash, size, bytes and compression choice) are stored once.
        auto& candidates = byContent[{e.hash, e.size}];
        const auto same = std::ranges::find_if(candidates, [&](const auto& c) {
            return c.first->data == input.data && (c.second.compression != PackCompression::None) == input.compress;
        });
        if (same != candidates.end()) {
            e.offset = same->second.offset;
            e.storedSize = same->second.storedSize;
            e.compression = same->second.compression;
            ++s.sharedEntries;
        } else {
            std::vector<std::byte> stored;
            e.compression = PackCompression::None;
            if (input.compress && !input.data.empty()) {
                stored.resize(ZSTD_compressBound(input.data.size()));
                const size_t n = ZSTD_compress(stored.data(), stored.size(), input.data.data(), input.data.size(), level);
                if (!ZSTD_isError(n) && n < input.data.size()) {
                    stored.resize(n);
                    e.compression = PackCompression::Zstd;
                }
            }
            if (e.compression == PackCompression::None) {
                stored = input.data;
            }
            e.offset = out.size();
            e.storedSize = stored.size();
            out.insert(out.end(), stored.begin(), stored.end());
            s.storedBytes += stored.size();
            candidates.push_back({&input, {e.offset, e.storedSize, e.compression}});
        }
        entries.push_back(std::move(e));
    }

    std::vector<std::byte> toc;
    for (const PackEntry& e : entries) {
        put32(toc, static_cast<uint32_t>(e.path.size()));
        const auto* p = reinterpret_cast<const std::byte*>(e.path.data());
        toc.insert(toc.end(), p, p + e.path.size());
        put64(toc, e.offset);
        put64(toc, e.storedSize);
        put64(toc, e.size);
        put32(toc, static_cast<uint32_t>(e.compression));
        put64(toc, e.hash);
    }
    const uint64_t tocOffset = out.size();
    out.insert(out.end(), toc.begin(), toc.end());
    put64At(out, 16, tocOffset);
    put64At(out, 24, toc.size());
    put64At(out, 32, contentHash(toc.data(), toc.size()));
    if (stats) {
        *stats = s;
    }
    return out;
}

} // namespace animal
