#pragma once

#include <animal/framework/geometry.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace animal {

using TileId = uint8_t;

// Tiles that hurt (tile property "hazard"): spikes pointing up sit on the floor and hurt in the tile's
// lower half, spikes pointing down hang from the ceiling and hurt in its upper half.
enum class Hazard : uint8_t { None, Up, Down, Full };

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

    void setHazard(TileId id, Hazard hazard) { hazard_[id] = hazard; }
    Hazard hazardOf(TileId id) const { return hazard_[id]; }

    // Moving platforms, closed doors, ...: solid for every body, like solid tiles. A rectangle with
    // no area is ignored (a platform that is gone, an open door).
    void setDynamicSolids(std::vector<IntRect> rects) { dynamic_ = std::move(rects); }
    std::span<IntRect> dynamicSolids() { return dynamic_; }
    std::span<const IntRect> dynamicSolids() const { return dynamic_; }

    // True if any solid tile or dynamic solid overlaps the pixel rectangle.
    bool overlapsSolid(const IntRect& rect) const;
    // True if the rectangle touches the hurting part of a hazard tile.
    bool overlapsHazard(const IntRect& rect) const;

private:
    int32_t width_ = 0;
    int32_t height_ = 0;
    int32_t tileSize_ = 8;
    std::vector<TileId> tiles_;
    std::array<bool, 256> solid_{};
    std::array<Hazard, 256> hazard_{};
    std::vector<IntRect> dynamic_;
};

} // namespace animal
