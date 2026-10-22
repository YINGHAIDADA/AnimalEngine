#pragma once

#include <animal/framework/tilemap.h>

#include <optional>

namespace game {

// Tile ids; they index the tileset texture horizontally.
enum Tile : animal::TileId {
    TileEmpty = 0,
    TileWall = 1,
    TileGround = 2,
    TileBackground = 3,
    TileCrystal = 4,
};

constexpr int kTileSize = 8;
constexpr int kRoomWidth = 40;  // ADR-0006: 40 × 22 tiles = 320 × 176 px
constexpr int kRoomHeight = 22;

// M1a test room, built from an in-code layout. Replaced by Tiled rooms in M1b.
std::optional<animal::TileMap> buildTestRoom();

} // namespace game
