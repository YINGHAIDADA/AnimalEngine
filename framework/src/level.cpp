#include <animal/core/file.h>
#include <animal/core/log.h>
#include <animal/framework/level.h>

#include <json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace animal {

namespace {

using Json = nlohmann::json;

constexpr uint32_t kFlipFlags = 0xF0000000u; // Tiled's flip / rotation bits on gids

std::string resolvePath(const std::string& base, const std::string& relative) {
    namespace fs = std::filesystem;
    const fs::path basePath(std::u8string(base.begin(), base.end()));
    const fs::path relativePath(std::u8string(relative.begin(), relative.end()));
    const std::u8string out = (basePath.parent_path() / relativePath).lexically_normal().generic_u8string();
    return {out.begin(), out.end()};
}

// Collects the first error with its context; every accessor returns a fallback after a failure.
class Reader {
public:
    explicit Reader(std::string context) : context_(std::move(context)) {}

    bool ok() const { return error_.empty(); }
    const std::string& error() const { return error_; }

    void fail(const std::string& message) {
        if (error_.empty()) {
            error_ = context_ + ": " + message;
        }
    }

    const Json* member(const Json& object, const char* key, bool required = true) {
        if (!object.is_object() || !object.contains(key)) {
            if (required) {
                fail(std::string("missing \"") + key + "\"");
            }
            return nullptr;
        }
        return &object.at(key);
    }

    int integer(const Json& object, const char* key, int fallback = 0, bool required = true) {
        const Json* v = member(object, key, required);
        if (!v) {
            return fallback;
        }
        if (!v->is_number_integer()) {
            fail(std::string("\"") + key + "\" must be an integer");
            return fallback;
        }
        return v->get<int>();
    }

    float number(const Json& object, const char* key, float fallback = 0.0f, bool required = true) {
        const Json* v = member(object, key, required);
        if (!v) {
            return fallback;
        }
        if (!v->is_number()) {
            fail(std::string("\"") + key + "\" must be a number");
            return fallback;
        }
        return v->get<float>();
    }

    std::string string(const Json& object, const char* key, const std::string& fallback = {}, bool required = true) {
        const Json* v = member(object, key, required);
        if (!v) {
            return fallback;
        }
        if (!v->is_string()) {
            fail(std::string("\"") + key + "\" must be a string");
            return fallback;
        }
        return v->get<std::string>();
    }

private:
    std::string context_;
    std::string error_;
};

std::optional<Color> parseColor(const std::string& text) {
    // Tiled writes "#AARRGGBB", or "#RRGGBB" when fully opaque.
    unsigned a = 255;
    unsigned r = 0;
    unsigned g = 0;
    unsigned b = 0;
    if (text.size() == 9 && text[0] == '#' && std::sscanf(text.c_str() + 1, "%2x%2x%2x%2x", &a, &r, &g, &b) == 4) {
        return Color::rgba(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b),
                           static_cast<uint8_t>(a));
    }
    if (text.size() == 7 && text[0] == '#' && std::sscanf(text.c_str() + 1, "%2x%2x%2x", &r, &g, &b) == 3) {
        return Color::rgba(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
    }
    return std::nullopt;
}

Properties parseProperties(const Json& owner, Reader& reader) {
    Properties properties;
    const Json* list = reader.member(owner, "properties", false);
    if (!list) {
        return properties;
    }
    if (!list->is_array()) {
        reader.fail("\"properties\" must be an array");
        return properties;
    }
    for (const Json& p : *list) {
        const std::string name = reader.string(p, "name");
        const std::string type = reader.string(p, "type", "string", false);
        const Json* value = reader.member(p, "value");
        if (!reader.ok() || !value) {
            return properties;
        }
        if (type == "bool" && value->is_boolean()) {
            properties.set(name, value->get<bool>());
        } else if (type == "int" && value->is_number_integer()) {
            properties.set(name, value->get<int>());
        } else if (type == "float" && value->is_number()) {
            properties.set(name, value->get<float>());
        } else if ((type == "string" || type == "file") && value->is_string()) {
            properties.set(name, value->get<std::string>());
        } else if (type == "color" && value->is_string()) {
            const auto color = parseColor(value->get<std::string>());
            if (!color) {
                reader.fail("property \"" + name + "\" has an invalid color");
                return properties;
            }
            properties.set(name, *color);
        } else {
            reader.fail("property \"" + name + "\" has unsupported type \"" + type + "\"");
            return properties;
        }
    }
    return properties;
}

// Tile properties of one tileset (embedded or the parsed external .tsj), indexed by local id.
bool parseTileset(const Json& tileset, int tileSize, std::vector<Properties>& out, Reader& reader) {
    const int width = reader.integer(tileset, "tilewidth");
    const int height = reader.integer(tileset, "tileheight");
    const int count = reader.integer(tileset, "tilecount");
    if (!reader.ok()) {
        return false;
    }
    if (width != tileSize || height != tileSize) {
        reader.fail("tileset tiles are " + std::to_string(width) + "x" + std::to_string(height) + ", map uses " +
                    std::to_string(tileSize));
        return false;
    }
    if (count <= 0 || count > 256) {
        reader.fail("tileset must have 1..256 tiles");
        return false;
    }
    out.assign(static_cast<size_t>(count), Properties{});
    if (const Json* tiles = reader.member(tileset, "tiles", false)) {
        for (const Json& tile : *tiles) {
            const int id = reader.integer(tile, "id");
            if (!reader.ok()) {
                return false;
            }
            if (id < 0 || id >= count) {
                reader.fail("tile id " + std::to_string(id) + " outside the tileset");
                return false;
            }
            out[static_cast<size_t>(id)] = parseProperties(tile, reader);
        }
    }
    return reader.ok();
}

} // namespace

// --- Properties -------------------------------------------------------------------------------

bool Properties::getBool(const std::string& name, bool fallback) const {
    const auto it = values_.find(name);
    return it != values_.end() && std::holds_alternative<bool>(it->second) ? std::get<bool>(it->second) : fallback;
}

float Properties::getFloat(const std::string& name, float fallback) const {
    const auto it = values_.find(name);
    if (it == values_.end()) {
        return fallback;
    }
    if (std::holds_alternative<float>(it->second)) {
        return std::get<float>(it->second);
    }
    if (std::holds_alternative<int>(it->second)) {
        return static_cast<float>(std::get<int>(it->second));
    }
    return fallback;
}

int Properties::getInt(const std::string& name, int fallback) const {
    const auto it = values_.find(name);
    return it != values_.end() && std::holds_alternative<int>(it->second) ? std::get<int>(it->second) : fallback;
}

std::string Properties::getString(const std::string& name, const std::string& fallback) const {
    const auto it = values_.find(name);
    return it != values_.end() && std::holds_alternative<std::string>(it->second) ? std::get<std::string>(it->second)
                                                                                 : fallback;
}

Color Properties::getColor(const std::string& name, Color fallback) const {
    const auto it = values_.find(name);
    return it != values_.end() && std::holds_alternative<Color>(it->second) ? std::get<Color>(it->second) : fallback;
}

// --- Room ------------------------------------------------------------------------------------

const Properties& Room::propertiesOf(TileId id) const {
    static const Properties kNone;
    return id < tileProperties.size() ? tileProperties[id] : kNone;
}

std::vector<const LevelObject*> Room::objectsOfType(std::string_view type) const {
    std::vector<const LevelObject*> out;
    for (const LevelObject& object : objects) {
        if (object.type == type) {
            out.push_back(&object);
        }
    }
    return out;
}

namespace {

std::optional<Room> parseRoomImpl(std::string_view text, const std::string& path, const TextReader& read,
                                  std::string* error) {
    auto failWith = [error](const std::string& message) -> std::optional<Room> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };

    Json map;
    try {
        map = Json::parse(text.begin(), text.end());
    } catch (const Json::parse_error& e) {
        return failWith(path + ": invalid JSON: " + e.what());
    }

    Reader reader(path);
    if (reader.string(map, "orientation") != "orthogonal") {
        reader.fail("only orthogonal maps are supported");
    }
    if (const Json* infinite = reader.member(map, "infinite", false); infinite && infinite->is_boolean() &&
                                                                       infinite->get<bool>()) {
        reader.fail("infinite maps are not supported; disable \"Infinite\" in the map properties");
    }
    const int width = reader.integer(map, "width");
    const int height = reader.integer(map, "height");
    const int tileSize = reader.integer(map, "tilewidth");
    if (reader.integer(map, "tileheight") != tileSize) {
        reader.fail("tiles must be square");
    }
    if (!reader.ok()) {
        return failWith(reader.error());
    }

    Room room;
    room.path = path;
    room.tiles = TileMap(width, height, tileSize);
    room.properties = parseProperties(map, reader);

    // Exactly one tileset (embedded, or external via "source").
    int firstGid = 1;
    const Json* tilesets = reader.member(map, "tilesets");
    if (!reader.ok() || !tilesets->is_array() || tilesets->size() != 1) {
        reader.fail("exactly one tileset is supported");
        return failWith(reader.error());
    }
    const Json& tilesetRef = (*tilesets)[0];
    firstGid = reader.integer(tilesetRef, "firstgid");
    if (tilesetRef.contains("source")) {
        const std::string source = resolvePath(path, reader.string(tilesetRef, "source"));
        const std::optional<std::string> tilesetText = read(source);
        if (!tilesetText) {
            return failWith(path + ": cannot read tileset \"" + source + "\"");
        }
        Json tileset;
        try {
            tileset = Json::parse(*tilesetText);
        } catch (const Json::parse_error& e) {
            return failWith(source + ": invalid JSON: " + e.what());
        }
        Reader tilesetReader(source);
        if (!parseTileset(tileset, tileSize, room.tileProperties, tilesetReader)) {
            return failWith(tilesetReader.error());
        }
    } else if (!parseTileset(tilesetRef, tileSize, room.tileProperties, reader)) {
        return failWith(reader.error());
    }
    for (size_t id = 0; id < room.tileProperties.size(); ++id) {
        room.tiles.setSolid(static_cast<TileId>(id), room.tileProperties[id].getBool("solid"));
    }

    bool sawTiles = false;
    const Json* layers = reader.member(map, "layers");
    if (!layers || !layers->is_array()) {
        reader.fail("\"layers\" must be an array");
        return failWith(reader.error());
    }
    for (const Json& layer : *layers) {
        const std::string type = reader.string(layer, "type");
        const std::string name = reader.string(layer, "name", "", false);
        if (type == "tilelayer") {
            if (sawTiles) {
                reader.fail("only one tile layer is supported (layer \"" + name + "\")");
                break;
            }
            sawTiles = true;
            if (layer.contains("encoding") && reader.string(layer, "encoding") != "csv") {
                reader.fail("tile layer \"" + name + "\" must use CSV encoding (Map > Properties > Tile Layer Format)");
                break;
            }
            const Json* data = reader.member(layer, "data");
            if (!reader.ok() || !data->is_array() || data->size() != static_cast<size_t>(width * height)) {
                reader.fail("tile layer \"" + name + "\" data does not match the map size");
                break;
            }
            for (int i = 0; i < width * height; ++i) {
                const Json& cell = (*data)[static_cast<size_t>(i)];
                if (!cell.is_number_unsigned()) {
                    reader.fail("tile layer \"" + name + "\" contains a non-numeric gid");
                    break;
                }
                const uint32_t raw = cell.get<uint32_t>();
                if (raw & kFlipFlags) {
                    log::warn("{}: flipped tiles are not supported; flip ignored at ({}, {})", path, i % width,
                              i / width);
                }
                const uint32_t gid = raw & ~kFlipFlags;
                if (gid == 0) {
                    continue;
                }
                const int id = static_cast<int>(gid) - firstGid;
                if (id < 0 || id >= static_cast<int>(room.tileProperties.size())) {
                    reader.fail("tile gid " + std::to_string(gid) + " is not in the tileset");
                    break;
                }
                room.tiles.setTile(i % width, i / width, static_cast<TileId>(id));
            }
        } else if (type == "objectgroup") {
            const Json* objects = reader.member(layer, "objects");
            if (!objects || !objects->is_array()) {
                reader.fail("object layer \"" + name + "\" has no \"objects\" array");
                break;
            }
            for (const Json& o : *objects) {
                LevelObject object;
                object.id = reader.integer(o, "id");
                object.name = reader.string(o, "name", "", false);
                object.type = o.contains("class") ? reader.string(o, "class") : reader.string(o, "type", "", false);
                object.position = {reader.number(o, "x"), reader.number(o, "y")};
                object.size = {reader.number(o, "width", 0.0f, false), reader.number(o, "height", 0.0f, false)};
                object.point = o.contains("point") && o.at("point").get<bool>();
                object.properties = parseProperties(o, reader);
                room.objects.push_back(std::move(object));
            }
        }
        if (!reader.ok()) {
            break;
        }
    }
    if (reader.ok() && !sawTiles) {
        reader.fail("the map has no tile layer");
    }
    if (!reader.ok()) {
        return failWith(reader.error());
    }
    return room;
}

} // namespace

std::optional<Room> parseRoom(std::string_view text, const std::string& path, const TextReader& read,
                              std::string* error) {
    try {
        return parseRoomImpl(text, path, read, error);
    } catch (const Json::exception& e) {
        if (error) {
            *error = path + ": unexpected value: " + e.what();
        }
        return std::nullopt;
    }
}

// --- World -----------------------------------------------------------------------------------

const WorldRoom* World::roomContaining(Vec2i point) const {
    for (const WorldRoom& room : rooms) {
        const IntRect& r = room.rect;
        if (point.x >= r.x && point.x < r.right() && point.y >= r.y && point.y < r.bottom()) {
            return &room;
        }
    }
    return nullptr;
}

namespace {

std::optional<World> parseWorldImpl(std::string_view text, const std::string& path, std::string* error) {
    auto failWith = [error](const std::string& message) -> std::optional<World> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    Json json;
    try {
        json = Json::parse(text.begin(), text.end());
    } catch (const Json::parse_error& e) {
        return failWith(path + ": invalid JSON: " + e.what());
    }
    Reader reader(path);
    World world;
    const Json* maps = reader.member(json, "maps");
    if (!reader.ok() || !maps->is_array()) {
        reader.fail("\"maps\" must be an array");
        return failWith(reader.error());
    }
    for (const Json& m : *maps) {
        WorldRoom room;
        room.path = resolvePath(path, reader.string(m, "fileName"));
        room.rect = {reader.integer(m, "x"), reader.integer(m, "y"), reader.integer(m, "width"),
                     reader.integer(m, "height")};
        if (!reader.ok()) {
            return failWith(reader.error());
        }
        for (const WorldRoom& other : world.rooms) {
            if (other.rect.overlaps(room.rect)) {
                return failWith(path + ": rooms \"" + other.path + "\" and \"" + room.path + "\" overlap");
            }
        }
        world.rooms.push_back(std::move(room));
    }
    return world;
}

} // namespace

std::optional<World> parseWorld(std::string_view text, const std::string& path, std::string* error) {
    try {
        return parseWorldImpl(text, path, error);
    } catch (const Json::exception& e) {
        if (error) {
            *error = path + ": unexpected value: " + e.what();
        }
        return std::nullopt;
    }
}

// --- Files -----------------------------------------------------------------------------------

namespace {

std::optional<std::string> readText(const std::string& path) {
    const auto bytes = readFile(path);
    if (!bytes) {
        return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

} // namespace

std::optional<Room> loadRoom(const std::string& path) {
    const auto text = readText(path);
    if (!text) {
        log::error("Cannot read room '{}'", path);
        return std::nullopt;
    }
    std::string error;
    auto room = parseRoom(*text, path, readText, &error);
    if (!room) {
        log::error("Invalid room: {}", error);
    }
    return room;
}

std::optional<World> loadWorld(const std::string& path) {
    const auto text = readText(path);
    if (!text) {
        log::error("Cannot read world '{}'", path);
        return std::nullopt;
    }
    std::string error;
    auto world = parseWorld(*text, path, &error);
    if (!world) {
        log::error("Invalid world: {}", error);
    }
    return world;
}

} // namespace animal
