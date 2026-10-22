#pragma once

#include <cmath>
#include <cstdint>

namespace animal {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2& operator+=(Vec2 o) {
        x += o.x;
        y += o.y;
        return *this;
    }
    friend constexpr bool operator==(Vec2, Vec2) = default;
};

inline float length(Vec2 v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

inline Vec2 roundToPixel(Vec2 v) {
    return {std::round(v.x), std::round(v.y)};
}

struct RectF {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    friend constexpr bool operator==(const RectF&, const RectF&) = default;
};

// Straight (non-premultiplied) 8-bit RGBA.
struct Color {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;

    static constexpr Color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return {r, g, b, a}; }
    static constexpr Color white() { return {255, 255, 255, 255}; }
    static constexpr Color black() { return {0, 0, 0, 255}; }

    constexpr Color premultiplied() const {
        return {static_cast<uint8_t>((r * a + 127) / 255), static_cast<uint8_t>((g * a + 127) / 255),
                static_cast<uint8_t>((b * a + 127) / 255), a};
    }

    friend constexpr bool operator==(Color, Color) = default;
};

// Column-major 4x4 matrix, matching GLSL / std140 layout.
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static constexpr Mat4 identity() { return {}; }

    // Vulkan clip conventions: `top` maps to y = -1, depth range [0, 1].
    static constexpr Mat4 orthographic(float left, float right, float top, float bottom) {
        Mat4 r;
        r.m[0] = 2.0f / (right - left);
        r.m[5] = 2.0f / (bottom - top);
        r.m[10] = 1.0f;
        r.m[12] = -(right + left) / (right - left);
        r.m[13] = -(bottom + top) / (bottom - top);
        return r;
    }

    static constexpr Mat4 translation(float x, float y) {
        Mat4 r;
        r.m[12] = x;
        r.m[13] = y;
        return r;
    }

    constexpr Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += m[k * 4 + row] * o.m[col * 4 + k];
                }
                r.m[col * 4 + row] = sum;
            }
        }
        return r;
    }

    constexpr Vec2 transformPoint(Vec2 p) const {
        return {m[0] * p.x + m[4] * p.y + m[12], m[1] * p.x + m[5] * p.y + m[13]};
    }
};

} // namespace animal
