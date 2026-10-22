#pragma once

namespace animal::detail {
[[noreturn]] void assertFailed(const char* expression, const char* file, int line);
}

#if defined(ANIMAL_DEBUG)
#define ANIMAL_ASSERT(expr)                                                \
    do {                                                                   \
        if (!(expr)) {                                                     \
            ::animal::detail::assertFailed(#expr, __FILE__, __LINE__);     \
        }                                                                  \
    } while (false)
#else
#define ANIMAL_ASSERT(expr) \
    do {                    \
        (void)sizeof(expr); \
    } while (false)
#endif
