#include <animal/devtools/tiled_map.h>

namespace animal::devtools {

namespace {

int intOr(const JsonDocument& object, const char* key, int fallback) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number_integer() ? it->get<int>() : fallback;
}

} // namespace

std::optional<TiledMap> TiledMap::parse(std::string_view text, std::string* error) {
    TiledMap map;
    try {
        map.doc_ = JsonDocument::parse(text);
    } catch (const JsonDocument::exception& e) {
        if (error) {
            *error = std::string("not valid JSON: ") + e.what();
        }
        return std::nullopt;
    }
    if (!map.doc_.is_object() || map.doc_.value("type", "") != "map" || !map.doc_.contains("layers") ||
        !map.doc_["layers"].is_array()) {
        if (error) {
            *error = "not a Tiled map";
        }
        return std::nullopt;
    }
    return map;
}

std::string TiledMap::serialize() const {
    return doc_.dump(1, ' ', true) + "\n";
}

int TiledMap::width() const {
    return intOr(doc_, "width", 0);
}

int TiledMap::height() const {
    return intOr(doc_, "height", 0);
}

int TiledMap::tileWidth() const {
    return intOr(doc_, "tilewidth", 8);
}

int TiledMap::tileHeight() const {
    return intOr(doc_, "tileheight", 8);
}

JsonDocument* TiledMap::layer(const std::string& name, std::string_view type) {
    return const_cast<JsonDocument*>(static_cast<const TiledMap*>(this)->layer(name, type));
}

const JsonDocument* TiledMap::layer(const std::string& name, std::string_view type) const {
    for (const JsonDocument& l : doc_["layers"]) {
        if (l.value("type", "") == type && (name.empty() || l.value("name", "") == name)) {
            return &l;
        }
    }
    return nullptr;
}

std::vector<std::string> TiledMap::tileLayers() const {
    std::vector<std::string> names;
    for (const JsonDocument& l : doc_["layers"]) {
        if (l.value("type", "") == "tilelayer" && l.contains("data") && l["data"].is_array()) {
            names.push_back(l.value("name", ""));
        }
    }
    return names;
}

uint32_t TiledMap::tile(const std::string& layerName, int x, int y) const {
    const JsonDocument* l = layer(layerName, "tilelayer");
    const int w = l ? intOr(*l, "width", 0) : 0;
    const int h = l ? intOr(*l, "height", 0) : 0;
    if (!l || x < 0 || y < 0 || x >= w || y >= h || !(*l)["data"].is_array()) {
        return 0;
    }
    const JsonDocument& v = (*l)["data"][static_cast<size_t>(y * w + x)];
    return v.is_number_unsigned() || v.is_number_integer() ? v.get<uint32_t>() : 0;
}

bool TiledMap::setTile(const std::string& layerName, int x, int y, uint32_t gid) {
    JsonDocument* l = layer(layerName, "tilelayer");
    const int w = l ? intOr(*l, "width", 0) : 0;
    const int h = l ? intOr(*l, "height", 0) : 0;
    if (!l || x < 0 || y < 0 || x >= w || y >= h || !(*l)["data"].is_array() ||
        (*l)["data"].size() != static_cast<size_t>(w * h)) {
        return false;
    }
    (*l)["data"][static_cast<size_t>(y * w + x)] = gid;
    return true;
}

std::vector<int> TiledMap::objectIds() const {
    std::vector<int> ids;
    for (const JsonDocument& l : doc_["layers"]) {
        if (l.value("type", "") == "objectgroup" && l.contains("objects")) {
            for (const JsonDocument& o : l["objects"]) {
                ids.push_back(intOr(o, "id", 0));
            }
        }
    }
    return ids;
}

JsonDocument* TiledMap::object(int id) {
    return const_cast<JsonDocument*>(static_cast<const TiledMap*>(this)->object(id));
}

const JsonDocument* TiledMap::object(int id) const {
    for (const JsonDocument& l : doc_["layers"]) {
        if (l.value("type", "") == "objectgroup" && l.contains("objects")) {
            for (const JsonDocument& o : l["objects"]) {
                if (intOr(o, "id", 0) == id) {
                    return &o;
                }
            }
        }
    }
    return nullptr;
}

std::vector<std::string> TiledMap::objectLayers() const {
    std::vector<std::string> names;
    for (const JsonDocument& l : doc_["layers"]) {
        if (l.value("type", "") == "objectgroup") {
            names.push_back(l.value("name", ""));
        }
    }
    return names;
}

int TiledMap::addObject(JsonDocument object, const std::string& layerName) {
    JsonDocument* l = layer(layerName, "objectgroup");
    if (!l) {
        return 0;
    }
    int next = intOr(doc_, "nextobjectid", 1);
    for (const int id : objectIds()) {
        next = std::max(next, id + 1); // never reuse an id (they are save keys)
    }
    object["id"] = next;
    doc_["nextobjectid"] = next + 1;
    (*l)["objects"].push_back(std::move(object));
    return next;
}

bool TiledMap::removeObject(int id) {
    for (JsonDocument& l : doc_["layers"]) {
        if (l.value("type", "") != "objectgroup" || !l.contains("objects")) {
            continue;
        }
        JsonDocument& objects = l["objects"];
        for (size_t i = 0; i < objects.size(); ++i) {
            if (intOr(objects[i], "id", 0) == id) {
                objects.erase(i);
                return true;
            }
        }
    }
    return false;
}

const JsonDocument* TiledMap::property(const JsonDocument& owner, std::string_view name) {
    const auto it = owner.find("properties");
    if (it == owner.end() || !it->is_array()) {
        return nullptr;
    }
    for (const JsonDocument& p : *it) {
        if (p.value("name", "") == name) {
            return &p;
        }
    }
    return nullptr;
}

void TiledMap::setProperty(JsonDocument& owner, const std::string& name, const std::string& type, JsonDocument value) {
    if (!owner.contains("properties") || !owner["properties"].is_array()) {
        owner["properties"] = JsonDocument::array();
    }
    for (JsonDocument& p : owner["properties"]) {
        if (p.value("name", "") == name) {
            p["type"] = type;
            p["value"] = std::move(value);
            return;
        }
    }
    owner["properties"].push_back(JsonDocument{{"name", name}, {"type", type}, {"value", std::move(value)}});
}

bool TiledMap::removeProperty(JsonDocument& owner, std::string_view name) {
    if (!owner.contains("properties") || !owner["properties"].is_array()) {
        return false;
    }
    JsonDocument& properties = owner["properties"];
    for (size_t i = 0; i < properties.size(); ++i) {
        if (properties[i].value("name", "") == name) {
            properties.erase(i);
            if (properties.empty()) {
                owner.erase("properties");
            }
            return true;
        }
    }
    return false;
}

} // namespace animal::devtools
