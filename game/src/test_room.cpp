#include "test_room.h"

#include <animal/core/log.h>

#include <iterator>

namespace game {

namespace {

// '#' brick, '=' grass ledge, '.' background wall, ' ' empty
constexpr std::string_view kLayout[] = {
    "########################################",
    "#......................................#",
    "#......................................#",
    "#.............######...................#",
    "#......................................#",
    "#......===...................===.......#",
    "#......................................#",
    "#...............===.........###........#",
    "#......................................#",
    "#====..............====================#",
    "########################################",
    "########################################",
};

} // namespace

bool TestRoom::load() {
    height_ = static_cast<int>(std::size(kLayout));
    width_ = static_cast<int>(kLayout[0].size());
    tiles_.clear();
    tiles_.reserve(static_cast<size_t>(width_ * height_));

    for (int y = 0; y < height_; ++y) {
        const std::string_view row = kLayout[y];
        if (static_cast<int>(row.size()) != width_) {
            animal::log::error("TestRoom: row {} has {} tiles, expected {}", y, row.size(), width_);
            return false;
        }
        for (const char c : row) {
            switch (c) {
            case '#':
                tiles_.push_back(Brick);
                break;
            case '=':
                tiles_.push_back(Grass);
                break;
            case '.':
                tiles_.push_back(Background);
                break;
            case ' ':
                tiles_.push_back(Empty);
                break;
            default:
                animal::log::error("TestRoom: unknown tile '{}' at row {}", c, y);
                return false;
            }
        }
    }
    return true;
}

} // namespace game
