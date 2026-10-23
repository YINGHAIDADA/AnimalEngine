// The map screen (M / Back): the rooms entered so far, laid out as in the world, with the gems still
// to find in them and where the player is.

#include "sandbox_game.h"

#include <animal/framework/world_map.h>
#include <animal/renderer/renderer2d.h>

#include <cmath>
#include <format>

namespace game {

namespace {

constexpr animal::Color kShade = animal::Color::rgba(6, 6, 12, 215);
constexpr animal::Color kTitle = animal::Color::rgba(150, 240, 215);
constexpr animal::Color kHint = animal::Color::rgba(120, 130, 140);
constexpr animal::Color kRoomFill = animal::Color::rgba(34, 46, 58);
constexpr animal::Color kRoomEdge = animal::Color::rgba(96, 128, 140);
constexpr animal::Color kCurrentFill = animal::Color::rgba(52, 80, 90);
constexpr animal::Color kCurrentEdge = animal::Color::rgba(150, 240, 215);
constexpr animal::Color kGem = animal::Color::rgba(255, 214, 90);
constexpr animal::Color kPlayer = animal::Color::rgba(250, 250, 255);
constexpr animal::Color kPlayerEdge = animal::Color::rgba(10, 10, 16);

// A square of `size` pixels centred on `p`, on whole pixels.
animal::RectF square(animal::Vec2 p, float size) {
    return {std::round(p.x - size * 0.5f), std::round(p.y - size * 0.5f), size, size};
}

} // namespace

void SandboxGame::drawMap(animal::Renderer2D& renderer) {
    const animal::PresentLayout& screen = renderer.presentLayout();
    const float u = static_cast<float>(renderer.overlayScale());
    const float margin = 6.0f * u;
    const float line = renderer.textLineHeight();
    const animal::RectF canvas{static_cast<float>(screen.x), static_cast<float>(screen.y),
                               static_cast<float>(screen.width), static_cast<float>(screen.height)};
    renderer.fillRect(canvas, kShade);

    renderer.drawText({canvas.x + margin, canvas.y + margin}, "MAP", kTitle);
    const std::string rooms = std::format("ROOMS {}/{}", save_.visited.size(), world_.rooms.size());
    renderer.drawText({canvas.x + canvas.width - margin - renderer.textWidth(rooms), canvas.y + margin}, rooms, kTitle);
    const std::string hint = usingGamepad_ ? "BACK CLOSE" : "M CLOSE";
    renderer.drawText({canvas.x + (canvas.width - renderer.textWidth(hint)) * 0.5f,
                       canvas.y + canvas.height - margin - line},
                      hint, kHint);

    const float band = margin * 2.0f + line;
    const animal::RectF area{canvas.x + margin, canvas.y + band, canvas.width - margin * 2.0f,
                             canvas.height - band * 2.0f};
    const animal::WorldMapLayout map = animal::layoutWorldMap(world_, area, 0.3f * u);
    for (size_t i = 0; i < world_.rooms.size(); ++i) {
        const animal::WorldRoom& worldRoom = world_.rooms[i];
        const std::string name = roomName(worldRoom.path);
        if (!save_.visited.contains(name)) {
            continue;
        }
        const bool current = &worldRoom == currentRoom_;
        renderer.fillRect(map.rooms[i], current ? kCurrentFill : kRoomFill);
        renderer.strokeRect(map.rooms[i], current ? kCurrentEdge : kRoomEdge, u);
        const animal::Room* room = roomData(worldRoom);
        if (!room) {
            continue;
        }
        const animal::Vec2 roomOrigin{static_cast<float>(worldRoom.rect.x), static_cast<float>(worldRoom.rect.y)};
        for (const animal::LevelObject* gem : room->objectsOfType("gem")) {
            if (!save_.flags.contains(CaveRoom::gemFlag(name, gem->id))) {
                renderer.fillRect(square(map.toScreen(roomOrigin + gem->position), 2.0f * u), kGem);
            }
        }
    }

    const animal::Entity* player = entities_.get(player_);
    if (player && currentRoom_) {
        const animal::Vec2 feet = player->body.exactPosition() + animal::Vec2{3.0f, 3.0f};
        const animal::Vec2 at = map.toScreen(
            {static_cast<float>(currentRoom_->rect.x) + feet.x, static_cast<float>(currentRoom_->rect.y) + feet.y});
        renderer.fillRect(square(at, 5.0f * u), kPlayerEdge);
        renderer.fillRect(square(at, 3.0f * u), kPlayer);
    }
}

} // namespace game
