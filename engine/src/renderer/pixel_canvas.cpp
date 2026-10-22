#include <animal/renderer/pixel_canvas.h>

#include <algorithm>

namespace animal {

PresentLayout computePresentLayout(rhi::Extent2D canvas, rhi::Extent2D window) {
    PresentLayout layout;
    if (canvas.width == 0 || canvas.height == 0) {
        return layout;
    }
    layout.scale = std::max(1u, std::min(window.width / canvas.width, window.height / canvas.height));
    layout.width = canvas.width * layout.scale;
    layout.height = canvas.height * layout.scale;
    layout.x = (static_cast<int32_t>(window.width) - static_cast<int32_t>(layout.width)) / 2;
    layout.y = (static_cast<int32_t>(window.height) - static_cast<int32_t>(layout.height)) / 2;
    return layout;
}

std::optional<Vec2> windowToCanvas(const PresentLayout& layout, rhi::Extent2D canvas, Vec2 windowPixel) {
    const float scale = static_cast<float>(layout.scale);
    const Vec2 p{(windowPixel.x - static_cast<float>(layout.x)) / scale,
                 (windowPixel.y - static_cast<float>(layout.y)) / scale};
    if (p.x < 0.0f || p.y < 0.0f || p.x >= static_cast<float>(canvas.width) ||
        p.y >= static_cast<float>(canvas.height)) {
        return std::nullopt;
    }
    return p;
}

} // namespace animal
