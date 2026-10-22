#include "gl_loader.h"

#include <animal/core/log.h>

#include <SDL3/SDL_video.h>

namespace animal::rhi::gl {

#define ANIMAL_GL_DEFINE(type, name) type name = nullptr;
ANIMAL_GL_FUNCTIONS(ANIMAL_GL_DEFINE)
#undef ANIMAL_GL_DEFINE

bool loadFunctions() {
    bool ok = true;
#define ANIMAL_GL_LOAD(type, name)                                          \
    name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));            \
    if (!name) {                                                            \
        log::error("OpenGL entry point missing: {}", #name);               \
        ok = false;                                                         \
    }
    ANIMAL_GL_FUNCTIONS(ANIMAL_GL_LOAD)
#undef ANIMAL_GL_LOAD
    return ok;
}

} // namespace animal::rhi::gl
