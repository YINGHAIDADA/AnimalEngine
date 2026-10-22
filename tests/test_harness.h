#pragma once

// Minimal dependency-free test harness: TEST(name) { CHECK(expr); }

#include <cmath>
#include <cstdio>
#include <functional>
#include <utility>
#include <vector>

namespace animal::test {

struct TestCase {
    const char* name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Register {
    Register(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

inline bool near(float a, float b, float epsilon = 1e-5f) {
    return std::fabs(a - b) < epsilon;
}

} // namespace animal::test

#define TEST(name)                                                       \
    static void name();                                                  \
    static const ::animal::test::Register name##_registration(#name, name); \
    static void name()

#define CHECK(expr)                                                                          \
    do {                                                                                     \
        if (!(expr)) {                                                                       \
            std::fprintf(stderr, "  CHECK failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__); \
            ++::animal::test::failures();                                                    \
        }                                                                                    \
    } while (false)
