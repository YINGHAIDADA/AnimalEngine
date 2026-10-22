#pragma once

#include <cstdint>

namespace animal {

// PCG32: small, fast, and identical on every platform and compiler (unlike <random>
// distributions), so seeded simulations stay reproducible.
class Random {
public:
    explicit Random(uint64_t seed = 0x853c49e6748fea9bULL, uint64_t stream = 0xda3e39cb94b95bdbULL) {
        increment_ = (stream << 1u) | 1u;
        next();
        state_ += seed;
        next();
    }

    uint32_t next() {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + increment_;
        const auto xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }

    // [0, 1)
    float uniform() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
    float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
    // [0, n)
    uint32_t below(uint32_t n) { return n == 0 ? 0 : next() % n; }

private:
    uint64_t state_ = 0;
    uint64_t increment_ = 0;
};

} // namespace animal
