#include <animal/framework/world_map.h>

#include <algorithm>
#include <cmath>

namespace animal {

WorldMapLayout layoutWorldMap(const World& world, const RectF& area, float maxScale) {
    WorldMapLayout layout;
    if (world.rooms.empty()) {
        return layout;
    }
    int left = world.rooms.front().rect.x;
    int top = world.rooms.front().rect.y;
    int right = left;
    int bottom = top;
    for (const WorldRoom& room : world.rooms) {
        left = std::min(left, room.rect.x);
        top = std::min(top, room.rect.y);
        right = std::max(right, room.rect.x + room.rect.width);
        bottom = std::max(bottom, room.rect.y + room.rect.height);
    }
    const float width = static_cast<float>(std::max(1, right - left));
    const float height = static_cast<float>(std::max(1, bottom - top));
    layout.scale = std::min({area.width / width, area.height / height, maxScale});
    layout.origin = {std::round(area.x + (area.width - width * layout.scale) * 0.5f - left * layout.scale),
                     std::round(area.y + (area.height - height * layout.scale) * 0.5f - top * layout.scale)};
    layout.rooms.reserve(world.rooms.size());
    for (const WorldRoom& room : world.rooms) {
        const Vec2 a = layout.toScreen({static_cast<float>(room.rect.x), static_cast<float>(room.rect.y)});
        const Vec2 b = layout.toScreen({static_cast<float>(room.rect.x + room.rect.width),
                                        static_cast<float>(room.rect.y + room.rect.height)});
        const float x0 = std::round(a.x);
        const float y0 = std::round(a.y);
        layout.rooms.push_back({x0, y0, std::round(b.x) - x0, std::round(b.y) - y0});
    }
    return layout;
}

} // namespace animal
