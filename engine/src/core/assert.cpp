#include <animal/core/assert.h>
#include <animal/core/log.h>

#include <SDL3/SDL_assert.h>

#include <cstdlib>

namespace animal::detail {

void assertFailed(const char* expression, const char* file, int line) {
    log::error("Assertion failed: {} ({}:{})", expression, file, line);
    SDL_TriggerBreakpoint();
    std::abort();
}

} // namespace animal::detail
