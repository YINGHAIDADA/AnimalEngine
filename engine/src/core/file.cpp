#include <animal/core/file.h>
#include <animal/core/log.h>
#include <animal/core/pack.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>

namespace animal {

namespace {

struct Mount {
    std::string directory; // normalized, ends with '/'
    std::unique_ptr<Pack> pack;
};

std::mutex gMountMutex;
std::vector<Mount> gMounts;

std::string normalized(const std::string& path) {
    const std::filesystem::path p(std::u8string(path.begin(), path.end()));
    const std::u8string s = p.lexically_normal().generic_u8string();
    return {s.begin(), s.end()};
}

// The pack and the path inside it for a path under a mount, else nullptr.
const Pack* lookup(const std::string& path, std::string& inside) {
    if (gMounts.empty()) {
        return nullptr;
    }
    const std::string p = normalized(path);
    for (const Mount& m : gMounts) {
        if (p.starts_with(m.directory)) {
            inside = p.substr(m.directory.size());
            return m.pack.get();
        }
    }
    return nullptr;
}

} // namespace

bool mountPack(const std::string& packPath, const std::string& mountDirectory, std::string* error) {
    std::unique_ptr<Pack> pack = Pack::open(packPath, error);
    if (!pack) {
        return false;
    }
    std::string directory = normalized(mountDirectory);
    if (!directory.empty() && directory.back() != '/') {
        directory += '/';
    }
    log::info("Mounted {} at {} ({} entries, data version \"{}\", {} KB)", packPath, directory,
              pack->entries().size(), pack->dataVersion(), pack->fileSize() / 1024);
    const std::lock_guard lock(gMountMutex);
    gMounts.push_back({std::move(directory), std::move(pack)});
    return true;
}

void unmountPacks() {
    const std::lock_guard lock(gMountMutex);
    gMounts.clear();
}

const Pack* packMountedAt(const std::string& path) {
    const std::lock_guard lock(gMountMutex);
    std::string inside;
    return lookup(path, inside);
}

std::optional<std::vector<std::byte>> readFile(const std::string& path, std::string* error) {
    {
        const std::lock_guard lock(gMountMutex);
        std::string inside;
        if (const Pack* pack = lookup(path, inside)) {
            const PackEntry* entry = pack->find(inside);
            if (!entry) {
                if (error) {
                    *error = inside + " is not in the resource pack";
                }
                return std::nullopt;
            }
            return pack->read(*entry, error);
        }
    }
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) {
        if (error) {
            *error = SDL_GetError();
        }
        return std::nullopt;
    }
    std::vector<std::byte> bytes(size);
    std::memcpy(bytes.data(), data, size);
    SDL_free(data);
    return bytes;
}

bool fileExists(const std::string& path) {
    {
        const std::lock_guard lock(gMountMutex);
        std::string inside;
        if (const Pack* pack = lookup(path, inside)) {
            return pack->find(inside) != nullptr;
        }
    }
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

bool deleteFile(const std::string& path) {
    return SDL_RemovePath(path.c_str());
}

bool writeFileAtomic(const std::string& path, const void* data, size_t size) {
    const std::string temp = path + ".tmp";
    if (!SDL_SaveFile(temp.c_str(), data, size)) {
        return false;
    }
    if (!SDL_RenamePath(temp.c_str(), path.c_str())) {
        SDL_RemovePath(temp.c_str());
        return false;
    }
    return true;
}

} // namespace animal
