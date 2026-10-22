#pragma once

#include <animal/core/math.h>
#include <animal/rhi/rhi.h>

#include <cstdint>
#include <optional>

namespace animal {

// Where the low-resolution canvas lands in the window, in window pixels.
struct PresentLayout {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t scale = 1;

    friend constexpr bool operator==(const PresentLayout&, const PresentLayout&) = default;
};

// Largest integer scale that fits the window; the canvas is centered and the remainder is
// letterboxed. Windows smaller than the canvas use scale 1 and crop around the center.
PresentLayout computePresentLayout(rhi::Extent2D canvas, rhi::Extent2D window);

// Window pixel -> canvas pixel. Returns nullopt for points in the letterbox area.
std::optional<Vec2> windowToCanvas(const PresentLayout& layout, rhi::Extent2D canvas, Vec2 windowPixel);

} // namespace animal
