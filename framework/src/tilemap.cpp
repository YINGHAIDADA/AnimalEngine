#include <animal/framework/tilemap.h>

#include <cmath>

namespace animal {

namespace {

// Floor division, correct for negative pixel coordinates.
int32_t floorDiv(int32_t value, int32_t divisor) {
    const int32_t q = value / divisor;
    return (value % divisor != 0 && ((value < 0) != (divisor < 0))) ? q - 1 : q;
}

} // namespace

TileMap::TileMap(int32_t width, int32_t height, int32_t tileSize)
    : width_(width), height_(height), tileSize_(tileSize), tiles_(static_cast<size_t>(width * height), 0) {}

TileId TileMap::tile(int32_t tx, int32_t ty) const {
    return inBounds(tx, ty) ? tiles_[static_cast<size_t>(ty * width_ + tx)] : TileId{0};
}

void TileMap::setTile(int32_t tx, int32_t ty, TileId id) {
    if (inBounds(tx, ty)) {
        tiles_[static_cast<size_t>(ty * width_ + tx)] = id;
    }
}

bool TileMap::overlapsSolid(const IntRect& rect) const {
    if (rect.width <= 0 || rect.height <= 0) {
        return false;
    }
    const int32_t x0 = floorDiv(rect.x, tileSize_);
    const int32_t y0 = floorDiv(rect.y, tileSize_);
    const int32_t x1 = floorDiv(rect.right() - 1, tileSize_);
    const int32_t y1 = floorDiv(rect.bottom() - 1, tileSize_);
    for (int32_t ty = y0; ty <= y1; ++ty) {
        for (int32_t tx = x0; tx <= x1; ++tx) {
            if (isSolid(tx, ty)) {
                return true;
            }
        }
    }
    for (const IntRect& solid : dynamic_) {
        if (solid.width > 0 && solid.height > 0 && solid.overlaps(rect)) {
            return true;
        }
    }
    return false;
}

std::optional<TileMap::OverheadHit> TileMap::firstSolidAbove(Vec2 from, float reach) const {
    if (reach <= 0.0f) {
        return std::nullopt;
    }
    const int32_t x = static_cast<int32_t>(std::lround(from.x));
    const int32_t start = static_cast<int32_t>(std::floor(from.y)) - 1; // the pixel above the probe
    const int32_t end = start - static_cast<int32_t>(std::floor(reach));
    for (int32_t y = start; y >= end; --y) {
        const IntRect pixel{x, y, 1, 1};
        for (size_t i = 0; i < dynamic_.size(); ++i) {
            const IntRect& solid = dynamic_[i];
            if (solid.width > 0 && solid.height > 0 && solid.overlaps(pixel)) {
                OverheadHit hit;
                hit.point = {static_cast<float>(x), static_cast<float>(y)};
                hit.dynamic = static_cast<int>(i);
                hit.local = {static_cast<float>(x - solid.x), static_cast<float>(y - solid.y)};
                return hit;
            }
        }
        if (isSolid(floorDiv(x, tileSize_), floorDiv(y, tileSize_))) {
            OverheadHit hit;
            hit.point = {static_cast<float>(x), static_cast<float>(y)};
            return hit;
        }
    }
    return std::nullopt;
}

bool TileMap::overlapsHazard(const IntRect& rect) const {
    if (rect.width <= 0 || rect.height <= 0) {
        return false;
    }
    const int32_t x0 = floorDiv(rect.x, tileSize_);
    const int32_t y0 = floorDiv(rect.y, tileSize_);
    const int32_t x1 = floorDiv(rect.right() - 1, tileSize_);
    const int32_t y1 = floorDiv(rect.bottom() - 1, tileSize_);
    const int32_t half = tileSize_ / 2;
    for (int32_t ty = y0; ty <= y1; ++ty) {
        for (int32_t tx = x0; tx <= x1; ++tx) {
            const Hazard hazard = inBounds(tx, ty) ? hazard_[tile(tx, ty)] : Hazard::None;
            if (hazard == Hazard::None) {
                continue;
            }
            // One pixel of slack at the sides: brushing past the tip does not count.
            IntRect hurt{tx * tileSize_ + 1, ty * tileSize_ + 1, tileSize_ - 2, tileSize_ - 2};
            if (hazard == Hazard::Up) {
                hurt = {tx * tileSize_ + 1, ty * tileSize_ + half, tileSize_ - 2, tileSize_ - half};
            } else if (hazard == Hazard::Down) {
                hurt = {tx * tileSize_ + 1, ty * tileSize_, tileSize_ - 2, tileSize_ - half};
            }
            if (hurt.overlaps(rect)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace animal
