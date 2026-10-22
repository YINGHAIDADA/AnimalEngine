#pragma once

#include <animal/core/math.h>
#include <animal/rhi/rhi.h>

namespace animal {

// Integer-pixel camera (PROJECT.md §7.2): the center is rounded to whole pixels before the
// view is built, so the canvas never samples between texels.
struct Camera2D {
    Vec2 center; // world position shown at the middle of the canvas

    Vec2 topLeft(rhi::Extent2D canvas) const {
        const Vec2 snapped = roundToPixel(center);
        return {snapped.x - static_cast<float>(canvas.width / 2), snapped.y - static_cast<float>(canvas.height / 2)};
    }

    Mat4 viewProjection(rhi::Extent2D canvas) const {
        const Vec2 origin = topLeft(canvas);
        return Mat4::orthographic(0.0f, static_cast<float>(canvas.width), 0.0f, static_cast<float>(canvas.height)) *
               Mat4::translation(-origin.x, -origin.y);
    }
};

} // namespace animal
