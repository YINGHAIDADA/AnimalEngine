#pragma once

#include <cstdint>

namespace animal {

struct Vec2i {
    int32_t x = 0;
    int32_t y = 0;

    constexpr Vec2i operator+(Vec2i o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2i operator-(Vec2i o) const { return {x - o.x, y - o.y}; }
    friend constexpr bool operator==(Vec2i, Vec2i) = default;
};

// Integer rectangle in pixels; covers [x, x + width) × [y, y + height).
struct IntRect {
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;

    constexpr int32_t right() const { return x + width; }
    constexpr int32_t bottom() const { return y + height; }
    constexpr IntRect offset(Vec2i d) const { return {x + d.x, y + d.y, width, height}; }
    constexpr bool overlaps(const IntRect& o) const {
        return x < o.right() && o.x < right() && y < o.bottom() && o.y < bottom();
    }
    friend constexpr bool operator==(const IntRect&, const IntRect&) = default;
};

} // namespace animal
