// The map screen (M / Back): the rooms entered so far, laid out as in the world with their terrain,
// doors and switches, the gems still to find in them and where the player is. The current room is
// drawn in full colour, the others darker.

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
constexpr animal::Color kRoomFill = animal::Color::rgba(32, 42, 56);
constexpr animal::Color kRock = animal::Color::rgba(166, 148, 134);
constexpr animal::Color kHazard = animal::Color::rgba(236, 62, 48);
constexpr animal::Color kWater = animal::Color::rgba(56, 120, 184, 190);
constexpr animal::Color kDoor = animal::Color::rgba(150, 162, 214);
constexpr animal::Color kGate = animal::Color::rgba(90, 226, 192);
constexpr uint8_t kOpenDoorAlpha = 60;
constexpr animal::Color kSwitchOff = animal::Color::rgba(214, 74, 60);
constexpr animal::Color kSwitchOn = animal::Color::rgba(110, 236, 120);
constexpr animal::Color kGem = animal::Color::rgba(255, 214, 90);
constexpr animal::Color kPlayer = animal::Color::rgba(250, 250, 255);
constexpr animal::Color kPlayerEdge = animal::Color::rgba(10, 10, 16);
constexpr float kOtherRoomBrightness = 0.45f;

// A square of `size` pixels centred on `p`, on whole pixels.
animal::RectF square(animal::Vec2 p, float size) {
    return {std::round(p.x - size * 0.5f), std::round(p.y - size * 0.5f), size, size};
}

// Draws one room: everything in room pixels, mapped to whole screen pixels so neighbouring tiles
// meet without gaps; rooms other than the current one darker.
class RoomPainter {
public:
    RoomPainter(animal::Renderer2D& renderer, const animal::WorldMapLayout& map, const animal::WorldRoom& worldRoom,
                bool current)
        : renderer_(renderer), map_(map), origin_{static_cast<float>(worldRoom.rect.x), static_cast<float>(worldRoom.rect.y)},
          brightness_(current ? 1.0f : kOtherRoomBrightness) {}

    void fill(float x0, float y0, float x1, float y1, animal::Color color) const {
        const float left = screenX(x0);
        const float top = screenY(y0);
        renderer_.fillRect({left, top, screenX(x1) - left, screenY(y1) - top}, shade(color));
    }
    void dot(animal::Vec2 p, float size, animal::Color color) const {
        renderer_.fillRect(square(map_.toScreen(origin_ + p), size), shade(color));
    }

private:
    float screenX(float x) const { return std::round(map_.toScreen({origin_.x + x, 0.0f}).x); }
    float screenY(float y) const { return std::round(map_.toScreen({0.0f, origin_.y + y}).y); }
    animal::Color shade(animal::Color c) const {
        const auto scale = [this](uint8_t v) { return static_cast<uint8_t>(std::lround(v * brightness_)); };
        return animal::Color::rgba(scale(c.r), scale(c.g), scale(c.b), c.a);
    }

    animal::Renderer2D& renderer_;
    const animal::WorldMapLayout& map_;
    animal::Vec2 origin_;
    float brightness_;
};

// Water, then solid tiles and hazards in runs along each row.
void drawTerrain(const RoomPainter& paint, const animal::Room& room) {
    for (const animal::LevelObject* water : room.objectsOfType("water")) {
        paint.fill(water->position.x, water->position.y, water->position.x + water->size.x,
                   water->position.y + water->size.y, kWater);
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
            paint.fill(static_cast<float>(tx) * size, static_cast<float>(ty) * size, static_cast<float>(end) * size,
                       static_cast<float>(ty + 1) * size, hazard ? kHazard : kRock);
            tx = end;
        }
    }
}

} // namespace

void SandboxGame::drawMap(animal::Renderer2D& renderer) {
    const animal::PresentLayout& screen = renderer.presentLayout();
    const float u = static_cast<float>(renderer.overlayScale());
    const float margin = 6.0f * u;
    const float line = renderer.textLineHeight(font_);
    const animal::RectF canvas{static_cast<float>(screen.x), static_cast<float>(screen.y),
                               static_cast<float>(screen.width), static_cast<float>(screen.height)};
    renderer.fillRect(canvas, kShade);

    renderer.drawText(font_, {canvas.x + margin, canvas.y + margin}, text("map.title"), kTitle);
    const std::string rooms = text("map.rooms", save_.visited.size(), world_.rooms.size());
    renderer.drawText(font_, {canvas.x + canvas.width - margin - renderer.textWidth(font_, rooms), canvas.y + margin},
                      rooms, kTitle);
    const std::string hint = text("map.close", prompt(Map));
    renderer.drawText(font_, {canvas.x + (canvas.width - renderer.textWidth(font_, hint)) * 0.5f,
                              canvas.y + canvas.height - margin - line},
                      hint, kHint);

    const float band = margin * 2.0f + line;
    const animal::RectF area{canvas.x + margin, canvas.y + band, canvas.width - margin * 2.0f,
                             canvas.height - band * 2.0f};
    const animal::WorldMapLayout map = animal::layoutWorldMap(world_, area, 0.5f * u);
    const size_t gems = collectedGems();
    for (size_t i = 0; i < world_.rooms.size(); ++i) {
        const animal::WorldRoom& worldRoom = world_.rooms[i];
        const std::string name = roomName(worldRoom.path);
        const animal::Room* room = roomData(worldRoom);
        if (!save_.visited.contains(name) || !room) {
            continue;
        }
        const RoomPainter paint(renderer, map, worldRoom, &worldRoom == currentRoom_);
        paint.fill(0.0f, 0.0f, static_cast<float>(worldRoom.rect.width), static_cast<float>(worldRoom.rect.height),
                   kRoomFill);
        drawTerrain(paint, *room);
        for (const animal::LevelObject* door : room->objectsOfType("door")) {
            const std::string flag = door->properties.getString("flag");
            const int needs = door->properties.getInt("gems");
            const bool open = (!flag.empty() && save_.flags.contains(Mechanisms::switchFlag(flag))) ||
                              (needs > 0 && gems >= static_cast<size_t>(needs));
            animal::Color color = needs > 0 ? kGate : kDoor;
            color.a = open ? kOpenDoorAlpha : 255;
            paint.fill(door->position.x, door->position.y, door->position.x + door->size.x,
                       door->position.y + door->size.y, color);
        }
        for (const animal::LevelObject* lever : room->objectsOfType("switch")) {
            const bool on = save_.flags.contains(Mechanisms::switchFlag(lever->properties.getString("flag")));
            paint.fill(lever->position.x - 3.0f, lever->position.y - 4.0f, lever->position.x + 3.0f, lever->position.y,
                       on ? kSwitchOn : kSwitchOff);
        }
        for (const animal::LevelObject* gem : room->objectsOfType("gem")) {
            if (!save_.flags.contains(CaveRoom::gemFlag(name, gem->id))) {
                paint.dot(gem->position, 2.0f * u, kGem);
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
