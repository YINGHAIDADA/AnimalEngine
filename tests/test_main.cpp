#include "test_harness.h"

#include <exception>

int main() {
    using namespace animal::test;
    for (const TestCase& test : registry()) {
        const int before = failures();
        // An exception fails its test instead of ending the run (and, on MSVC Debug, instead of an
        // abort() dialog that would wait forever on a build machine).
        try {
            test.body();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "  exception: %s\n", e.what());
            ++failures();
        } catch (...) {
            std::fprintf(stderr, "  unknown exception\n");
            ++failures();
        }
        std::printf("[%s] %s\n", failures() == before ? " OK " : "FAIL", test.name);
        std::fflush(stdout); // keep the log complete if a test crashes
    }
    std::printf("%zu tests, %d failed checks\n", registry().size(), failures());
    return failures() == 0 ? 0 : 1;
}
