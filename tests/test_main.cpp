#include "test_harness.h"

int main() {
    using namespace animal::test;
    for (const TestCase& test : registry()) {
        const int before = failures();
        test.body();
        std::printf("[%s] %s\n", failures() == before ? " OK " : "FAIL", test.name);
    }
    std::printf("%zu tests, %d failed checks\n", registry().size(), failures());
    return failures() == 0 ? 0 : 1;
}
