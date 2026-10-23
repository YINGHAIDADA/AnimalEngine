// The map screen (M / Back): the rooms entered so far, laid out as in the world with their terrain,
// the gems still to find in them and where the player is.

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
constexpr animal::Color kRoomFill = animal::Color::rgba(22, 26, 36);
constexpr animal::Color kRoomEdge = animal::Color::rgba(96, 128, 140);
constexpr animal::Color kCurrentFill = animal::Color::rgba(30, 40, 52);
constexpr animal::Color kRock = animal::Color::rgba(128, 112, 104);
constexpr animal::Color kCurrentRock = animal::Color::rgba(164, 146, 132);
constexpr animal::Color kHazard = animal::Color::rgba(230, 60, 48);
constexpr animal::Color kWater = animal::Color::rgba(50, 110, 170, 170);
constexpr animal::Color kCurrentEdge = animal::Color::rgba(150, 240, 215);
constexpr animal::Color kGem = animal::Color::rgba(255, 214, 90);
constexpr animal::Color kPlayer = animal::Color::rgba(250, 250, 255);
constexpr animal::Color kPlayerEdge = animal::Color::rgba(10, 10, 16);

// A square of `size` pixels centred on `p`, on whole pixels.
animal::RectF square(animal::Vec2 p, float size) {
    return {std::round(p.x - size * 0.5f), std::round(p.y - size * 0.5f), size, size};
}

// A room's terrain on the map: water, solid tiles and hazards, in runs along each row, on whole pixels
// so neighbouring tiles meet without gaps.
void drawTerrain(animal::Renderer2D& renderer, const animal::WorldMapLayout& map, const animal::WorldRoom& worldRoom,
                 const animal::Room& room, bool current) {
    const auto screenX = [&](float x) { return std::round(map.toScreen({static_cast<float>(worldRoom.rect.x) + x, 0.0f}).x); };
    const auto screenY = [&](float y) { return std::round(map.toScreen({0.0f, static_cast<float>(worldRoom.rect.y) + y}).y); };
    const auto area = [&](float x0, float y0, float x1, float y1) {
        const float left = screenX(x0);
        const float top = screenY(y0);
        return animal::RectF{left, top, screenX(x1) - left, screenY(y1) - top};
    };
    for (const animal::LevelObject* water : room.objectsOfType("water")) {
        renderer.fillRect(area(water->position.x, water->position.y, water->position.x + water->size.x,
                               water->position.y + water->size.y),
                          kWater);
    }
    const animal::TileMap& tiles = room.tiles;
    const float size = static_cast<float>(tiles.tileSize());
    for (int ty = 0; ty < tiles.height(); ++ty) {
        for (int tx = 0; tx < tiles.width();) {
            const animal::TileId id = tiles.tile(tx, ty);
            const bool solid = tiles.isSolidId(id);
            const bool hazard = tiles.hazardOf(id) != animal::Hazard::None;
            if (!solid && !hazard) {
                ++tx;
                continue;
            }
            int end = tx + 1;
            while (end < tiles.width() && tiles.isSolidId(tiles.tile(end, ty)) == solid &&
                   (tiles.hazardOf(tiles.tile(end, ty)) != animal::Hazard::None) == hazard) {
                ++end;
            }
            const animal::Color color = hazard ? kHazard : (current ? kCurrentRock : kRock);
            renderer.fillRect(area(static_cast<float>(tx) * size, static_cast<float>(ty) * size,
                                   static_cast<float>(end) * size, static_cast<float>(ty + 1) * size),
                              color);
            tx = end;
        }
    }
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
    const animal::WorldMapLayout map = animal::layoutWorldMap(world_, area, 0.5f * u);
    for (size_t i = 0; i < world_.rooms.size(); ++i) {
        const animal::WorldRoom& worldRoom = world_.rooms[i];
        const std::string name = roomName(worldRoom.path);
        if (!save_.visited.contains(name)) {
            continue;
        }
        const bool current = &worldRoom == currentRoom_;
        renderer.fillRect(map.rooms[i], current ? kCurrentFill : kRoomFill);
        const animal::Room* room = roomData(worldRoom);
        if (room) {
            drawTerrain(renderer, map, worldRoom, *room, current);
        }
        renderer.strokeRect(map.rooms[i], current ? kCurrentEdge : kRoomEdge, current ? u : std::max(1.0f, u * 0.5f));
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
