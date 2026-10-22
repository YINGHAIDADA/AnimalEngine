#pragma once

// Rooms and worlds authored in Tiled (ADR-0010): .tmj maps, .tsj tilesets, .world layouts.

#include <animal/core/math.h>
#include <animal/framework/geometry.h>
#include <animal/framework/tilemap.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace animal {

// Tiled custom properties (bool, int, float, string/file, color).
class Properties {
public:
    using Value = std::variant<bool, int, float, std::string, Color>;

    void set(const std::string& name, Value value) { values_[name] = std::move(value); }
    bool has(const std::string& name) const { return values_.count(name) != 0; }

    bool getBool(const std::string& name, bool fallback = false) const;
    float getFloat(const std::string& name, float fallback = 0.0f) const; // int values convert
    int getInt(const std::string& name, int fallback = 0) const;
    std::string getString(const std::string& name, const std::string& fallback = {}) const;
    Color getColor(const std::string& name, Color fallback = Color::white()) const;

    const Value* find(const std::string& name) const {
        const auto it = values_.find(name);
        return it != values_.end() ? &it->second : nullptr;
    }
    std::vector<std::string> names() const {
        std::vector<std::string> result;
        for (const auto& [name, value] : values_) {
            result.push_back(name);
        }
        return result;
    }

private:
    std::map<std::string, Value> values_;
};

struct LevelObject {
    int id = 0;
    std::string type; // Tiled "class" (or legacy "type")
    std::string name;
    Vec2 position;    // room pixels; top-left for rectangles, the point itself for points
    Vec2 size;
    bool point = false;
    Properties properties;
};

struct Room {
    std::string path;
    TileMap tiles;                         // solidity from the tileset's "solid" tile property
    std::vector<Properties> tileProperties; // indexed by TileId (0 = no tile)
    std::vector<LevelObject> objects;
    Properties properties;                 // map properties

    const Properties& propertiesOf(TileId id) const;
    std::vector<const LevelObject*> objectsOfType(std::string_view type) const;
};

struct WorldRoom {
    std::string path; // resolved, relative to the working directory / data root
    IntRect rect;     // world pixels
};

struct World {
    std::vector<WorldRoom> rooms;
    const WorldRoom* roomContaining(Vec2i point) const;
};

// Reads a UTF-8 text file; nullopt if it cannot be read.
using TextReader = std::function<std::optional<std::string>(const std::string& path)>;

// Parse from text. `path` is used to resolve relative references (external tilesets, maps)
// and in error messages. On failure returns nullopt and describes the problem in `error`.
std::optional<Room> parseRoom(std::string_view json, const std::string& path, const TextReader& read,
                              std::string* error);
std::optional<World> parseWorld(std::string_view json, const std::string& path, std::string* error);

// Shared by the loaders of Tiled and related data files.
std::string resolvePath(const std::string& base, const std::string& relative); // relative to base's directory
std::optional<Color> parseColor(const std::string& text);                      // "#RRGGBB" or "#AARRGGBB"
std::optional<std::string> readText(const std::string& path);                  // nullopt if unreadable

// Load from files; errors are logged.
std::optional<Room> loadRoom(const std::string& path);
std::optional<World> loadWorld(const std::string& path);

} // namespace animal
