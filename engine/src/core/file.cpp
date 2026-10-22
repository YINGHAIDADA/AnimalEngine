#include <animal/core/file.h>

#include <SDL3/SDL_error.h>
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

} // namespace animal
