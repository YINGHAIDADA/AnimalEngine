#include <animal/core/file.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include <cstring>

namespace animal {

std::optional<std::vector<std::byte>> readFile(const std::string& path, std::string* error) {
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
