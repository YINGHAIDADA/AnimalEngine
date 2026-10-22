#include "room_layout.h"

#include <animal/core/log.h>

#include <iterator>
#include <string_view>

namespace game {

namespace {

// '#' wall, '=' grass-topped ground, '.' background wall (non-solid), ' ' empty.
// Platforms are 3 tiles apart vertically and 2 tiles horizontally (one comfortable jump);
// the block on the lower right leaves a 1-tile-high tunnel underneath.
constexpr std::string_view kLayout[] = {
    "########################################",
    "#......................................#",
    "#......................................#",
    "#......................................#",
    "#......................................#",
    "#.........................======.......#",
    "#.........................######.......#",
    "#......................................#",
    "#.................======...............#",
    "#.................######...............#",
    "#......................................#",
    "#..........=====.......................#",
    "#..........#####.......................#",
    "#......................................#",
    "#.................=====........=====...#",
    "#.................#####........#####...#",
    "#......................................#",
    "#.........................=============#",
    "#.........................##############",
    "#......................................#",
    "#======================================#",
    "########################################",
};
static_assert(std::size(kLayout) == kRoomHeight, "room height must be 22 tiles");

} // namespace

std::optional<animal::TileMap> buildTestRoom() {
    animal::TileMap map(kRoomWidth, kRoomHeight, kTileSize);
    map.setSolid(TileWall, true);
    map.setSolid(TileGround, true);

    for (int ty = 0; ty < kRoomHeight; ++ty) {
        const std::string_view row = kLayout[ty];
        if (static_cast<int>(row.size()) != kRoomWidth) {
            animal::log::error("Test room: row {} has {} tiles, expected {}", ty, row.size(), kRoomWidth);
            return std::nullopt;
        }
        for (int tx = 0; tx < kRoomWidth; ++tx) {
            Tile tile = TileEmpty;
            switch (row[static_cast<size_t>(tx)]) {
            case '#':
                tile = TileWall;
                break;
            case '=':
                tile = TileGround;
                break;
            case '.':
                tile = TileBackground;
                break;
            case ' ':
                break;
            default:
                animal::log::error("Test room: unknown tile '{}' at ({}, {})", row[static_cast<size_t>(tx)], tx, ty);
                return std::nullopt;
            }
            map.setTile(tx, ty, tile);
        }
    }
    return map;
}

} // namespace game
