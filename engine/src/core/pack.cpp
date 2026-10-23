#include <animal/core/pack.h>

#include <SDL3/SDL_iostream.h>
#include <zstd.h>

#include <algorithm>
#include <cstring>

namespace animal {

namespace {

constexpr char kMagic[4] = {'A', 'P', 'A', 'K'};
constexpr size_t kHeaderSize = 4 + 4 + 4 + 4 + 8 + 8 + 8;

uint32_t u32At(const std::byte* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

uint64_t u64At(const std::byte* p) {
    uint64_t v = 0;
    std::memcpy(&v, p, 8);
    return v;
}

bool readAt(SDL_IOStream* io, uint64_t offset, void* out, size_t size) {
    return SDL_SeekIO(io, static_cast<Sint64>(offset), SDL_IO_SEEK_SET) >= 0 && SDL_ReadIO(io, out, size) == size;
}

} // namespace

uint64_t contentHash(const void* data, size_t size) {
    uint64_t hash = 14695981039346656037ull;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::unique_ptr<Pack> Pack::open(const std::string& path, std::string* error) {
    const auto fail = [&](const std::string& message) -> std::unique_ptr<Pack> {
        if (error) {
            *error = path + ": " + message;
        }
        return nullptr;
    };
    SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
    if (!io) {
        return fail(SDL_GetError());
    }
    std::unique_ptr<Pack> pack(new Pack());
    pack->stream_ = io;
    pack->fileSize_ = static_cast<uint64_t>(std::max<Sint64>(0, SDL_GetIOSize(io)));

    std::byte header[kHeaderSize];
    if (!readAt(io, 0, header, kHeaderSize) || std::memcmp(header, kMagic, 4) != 0) {
        return fail("not a resource pack");
    }
    const uint32_t version = u32At(header + 4);
    if (version != kPackVersion) {
        return fail("pack format version " + std::to_string(version) + ", this build reads version " +
                    std::to_string(kPackVersion));
    }
    const uint32_t count = u32At(header + 8);
    const uint32_t versionLength = u32At(header + 12);
    const uint64_t tocOffset = u64At(header + 16);
    const uint64_t tocSize = u64At(header + 24);
    pack->hash_ = u64At(header + 32);
    if (tocOffset + tocSize > pack->fileSize_ || kHeaderSize + versionLength > pack->fileSize_) {
        return fail("truncated (table of contents outside the file)");
    }
    pack->dataVersion_.resize(versionLength);
    std::vector<std::byte> toc(tocSize);
    if (!readAt(io, kHeaderSize, pack->dataVersion_.data(), versionLength) ||
        !readAt(io, tocOffset, toc.data(), toc.size())) {
        return fail("cannot read the table of contents");
    }
    if (contentHash(toc.data(), toc.size()) != pack->hash_) {
        return fail("table of contents is damaged (hash mismatch)");
    }
    size_t at = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (at + 4 > toc.size()) {
            return fail("table of contents is truncated");
        }
        const uint32_t pathLength = u32At(toc.data() + at);
        at += 4;
        if (at + pathLength + 8 * 3 + 4 + 8 > toc.size()) {
            return fail("table of contents is truncated");
        }
        PackEntry e;
        e.path.assign(reinterpret_cast<const char*>(toc.data() + at), pathLength);
        at += pathLength;
        e.offset = u64At(toc.data() + at);
        e.storedSize = u64At(toc.data() + at + 8);
        e.size = u64At(toc.data() + at + 16);
        e.compression = static_cast<PackCompression>(u32At(toc.data() + at + 24));
        e.hash = u64At(toc.data() + at + 28);
        at += 36;
        if (e.offset + e.storedSize > pack->fileSize_ ||
            (e.compression != PackCompression::None && e.compression != PackCompression::Zstd)) {
            return fail("entry \"" + e.path + "\" is invalid");
        }
        pack->entries_.push_back(std::move(e));
    }
    std::ranges::sort(pack->entries_, {}, &PackEntry::path);
    return pack;
}

Pack::~Pack() {
    if (stream_) {
        SDL_CloseIO(static_cast<SDL_IOStream*>(stream_));
    }
}

const PackEntry* Pack::find(std::string_view path) const {
    const auto it = std::ranges::lower_bound(entries_, path, {}, &PackEntry::path);
    return it != entries_.end() && it->path == path ? &*it : nullptr;
}

std::optional<std::vector<std::byte>> Pack::read(const PackEntry& entry, std::string* error) const {
    const auto fail = [&](const std::string& message) -> std::optional<std::vector<std::byte>> {
        if (error) {
            *error = entry.path + " (pack): " + message;
        }
        return std::nullopt;
    };
    std::vector<std::byte> stored(entry.storedSize);
    {
        const std::lock_guard lock(mutex_);
        if (!readAt(static_cast<SDL_IOStream*>(stream_), entry.offset, stored.data(), stored.size())) {
            return fail("cannot read");
        }
    }
    std::vector<std::byte> data;
    if (entry.compression == PackCompression::Zstd) {
        data.resize(entry.size);
        const size_t n = ZSTD_decompress(data.data(), data.size(), stored.data(), stored.size());
        if (ZSTD_isError(n) || n != entry.size) {
            return fail(std::string("cannot decompress: ") + (ZSTD_isError(n) ? ZSTD_getErrorName(n) : "wrong size"));
        }
    } else {
        data = std::move(stored);
    }
    if (contentHash(data.data(), data.size()) != entry.hash) {
        return fail("damaged (hash mismatch)");
    }
    return data;
}

} // namespace animal
