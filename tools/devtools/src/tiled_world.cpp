#include <animal/devtools/tiled_world.h>

#include <algorithm>

namespace animal::devtools {

std::optional<TiledWorld> TiledWorld::parse(std::string_view text, std::string* error) {
    TiledWorld world;
    try {
        world.doc_ = JsonDocument::parse(text);
    } catch (const JsonDocument::exception& e) {
        if (error) {
            *error = std::string("not valid JSON: ") + e.what();
        }
        return std::nullopt;
    }
    if (!world.doc_.is_object() || !world.doc_.contains("maps") || !world.doc_["maps"].is_array()) {
        if (error) {
            *error = "not a Tiled world (no \"maps\" array)";
        }
        return std::nullopt;
    }
    world.refresh();
    return world;
}

std::string TiledWorld::serialize() const {
    return doc_.dump(1, ' ', true) + "\n";
}

void TiledWorld::refresh() {
    cache_.clear();
    for (const JsonDocument& m : doc_["maps"]) {
        Map map;
        map.fileName = m.value("fileName", "");
        map.x = m.value("x", 0);
        map.y = m.value("y", 0);
        map.width = m.value("width", 0);
        map.height = m.value("height", 0);
        cache_.push_back(std::move(map));
    }
}

std::vector<TiledWorld::Map> TiledWorld::maps() const {
    return cache_;
}

const TiledWorld::Map* TiledWorld::find(std::string_view fileName) const {
    const auto it = std::ranges::find_if(cache_, [fileName](const Map& m) { return m.fileName == fileName; });
    return it == cache_.end() ? nullptr : &*it;
}

bool TiledWorld::move(std::string_view fileName, int x, int y) {
    for (JsonDocument& m : doc_["maps"]) {
        if (m.value("fileName", "") == fileName) {
            m["x"] = x;
            m["y"] = y;
            refresh();
            return true;
        }
    }
    return false;
}

bool TiledWorld::add(const Map& map) {
    if (find(map.fileName)) {
        return false;
    }
    doc_["maps"].push_back(JsonDocument{{"fileName", map.fileName},
                                        {"x", map.x},
                                        {"y", map.y},
                                        {"width", map.width},
                                        {"height", map.height}});
    refresh();
    return true;
}

bool TiledWorld::remove(std::string_view fileName) {
    JsonDocument& maps = doc_["maps"];
    for (size_t i = 0; i < maps.size(); ++i) {
        if (maps[i].value("fileName", "") == fileName) {
            maps.erase(i);
            refresh();
            return true;
        }
    }
    return false;
}

} // namespace animal::devtools
