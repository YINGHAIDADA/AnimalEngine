#pragma once

#include <animal/devtools/json_editor.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace animal::devtools {

// A Tiled world file (.world) open for editing (ADR-0029): the room layout. Like TiledMap it edits
// the JSON document itself and writes Tiled's style, so an unchanged world round-trips exactly.
// Validation (overlaps, sizes) is left to the game's own parser.
class TiledWorld {
public:
    struct Map {
        std::string fileName; // relative to the world file, e.g. "rooms/cave_entrance.tmj"
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    static std::optional<TiledWorld> parse(std::string_view text, std::string* error = nullptr);
    std::string serialize() const;

    std::vector<Map> maps() const;
    const Map* find(std::string_view fileName) const;
    bool move(std::string_view fileName, int x, int y);
    bool add(const Map& map); // false if the file is already in the world
    bool remove(std::string_view fileName);

private:
    JsonDocument doc_;
    std::vector<Map> cache_;
    void refresh();
};

} // namespace animal::devtools
