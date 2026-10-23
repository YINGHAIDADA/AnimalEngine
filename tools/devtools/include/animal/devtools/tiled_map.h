#pragma once

#include <animal/devtools/json_editor.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace animal::devtools {

// A Tiled map (.tmj) open for editing (ADR-0029). It edits the JSON document itself, so fields the
// engine ignores and Tiled's own bookkeeping survive a save, and Tiled can keep editing the file.
// serialize() writes Tiled's JSON style (one-space indent, ASCII), so unchanged parts stay
// byte-identical.
class TiledMap {
public:
    static std::optional<TiledMap> parse(std::string_view text, std::string* error = nullptr);
    std::string serialize() const;

    JsonDocument& document() { return doc_; }
    const JsonDocument& document() const { return doc_; }
    int width() const;  // in tiles
    int height() const;
    int tileWidth() const;
    int tileHeight() const;

    // Tile layers with plain (uncompressed) data.
    std::vector<std::string> tileLayers() const;
    uint32_t tile(const std::string& layer, int x, int y) const; // global id, 0 = empty; 0 outside
    bool setTile(const std::string& layer, int x, int y, uint32_t gid); // false outside / unknown layer

    // Objects of every object layer, in drawing order, by their stable Tiled id.
    std::vector<int> objectIds() const;
    JsonDocument* object(int id);
    const JsonDocument* object(int id) const;
    std::vector<std::string> objectLayers() const;
    // Adds an object to an object layer (the first one when `layer` is empty), giving it the map's
    // next object id; returns that id, or 0 when there is no such layer.
    int addObject(JsonDocument object, const std::string& layer = {});
    bool removeObject(int id);

    // Tiled custom properties: [{"name", "type", "value"}] on the map, a layer or an object.
    static const JsonDocument* property(const JsonDocument& owner, std::string_view name);
    static void setProperty(JsonDocument& owner, const std::string& name, const std::string& type, JsonDocument value);
    static bool removeProperty(JsonDocument& owner, std::string_view name);

private:
    JsonDocument* layer(const std::string& name, std::string_view type);
    const JsonDocument* layer(const std::string& name, std::string_view type) const;

    JsonDocument doc_;
};

} // namespace animal::devtools
