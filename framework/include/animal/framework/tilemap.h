#pragma once

#include <animal/framework/geometry.h>

#include <array>
#include <cstdint>
#include <vector>

namespace animal {

using TileId = uint8_t;

// Grid of tile ids plus a per-id property table. Coordinates outside the map are
// non-solid so bodies can leave a room (room transitions, M1b).
class TileMap {
public:
    TileMap() = default;
    TileMap(int32_t width, int32_t height, int32_t tileSize);

    int32_t width() const { return width_; }
    int32_t height() const { return height_; }
    int32_t tileSize() const { return tileSize_; }
    IntRect pixelBounds() const { return {0, 0, width_ * tileSize_, height_ * tileSize_}; }

    bool inBounds(int32_t tx, int32_t ty) const { return tx >= 0 && ty >= 0 && tx < width_ && ty < height_; }
    TileId tile(int32_t tx, int32_t ty) const;
    void setTile(int32_t tx, int32_t ty, TileId id);

    void setSolid(TileId id, bool solid) { solid_[id] = solid; }
    bool isSolidId(TileId id) const { return solid_[id]; }
    bool isSolid(int32_t tx, int32_t ty) const { return inBounds(tx, ty) && solid_[tile(tx, ty)]; }

    // True if any solid tile overlaps the pixel rectangle.
    bool overlapsSolid(const IntRect& rect) const;

private:
    int32_t width_ = 0;
    int32_t height_ = 0;
    int32_t tileSize_ = 8;
    std::vector<TileId> tiles_;
    std::array<bool, 256> solid_{};
};

} // namespace animal
