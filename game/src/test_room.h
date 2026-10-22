#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace game {

// M0 placeholder room. Moves to framework/ as data-driven Room in M1.
class TestRoom {
public:
    static constexpr int kTileSize = 16;

    enum Tile : uint8_t {
        Empty = 0,
        Brick = 1,
        Grass = 2,
        Background = 3,
    };

    bool load();

    int width() const { return width_; }
    int height() const { return height_; }
    int pixelWidth() const { return width_ * kTileSize; }
    int pixelHeight() const { return height_ * kTileSize; }
    Tile at(int x, int y) const { return tiles_[static_cast<size_t>(y * width_ + x)]; }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<Tile> tiles_;
};

} // namespace game
