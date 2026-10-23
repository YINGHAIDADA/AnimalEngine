#include "cooker.h"

#include <animal/audio/audio.h>
#include <animal/core/file.h>
#include <animal/core/log.h>
#include <animal/framework/effects.h>
#include <animal/framework/foliage.h>
#include <animal/framework/level.h>
#include <animal/framework/look.h>
#include <animal/framework/sprite_sheet.h>
#include <animal/resources/loaders.h>
#include <animal/rhi/shader_package.h>

#include <json.hpp>

#include <algorithm>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <set>

namespace animal::cooker {

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

enum class Kind { World, Room, Tileset, Look, Effect, Plant, Tuning, SpriteSheet, Shader, Sound, Json };

struct Item {
    Kind kind = Kind::Json;
    std::vector<std::byte> source;
    std::vector<std::byte> cooked;
};

std::string text(const std::vector<std::byte>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::vector<std::byte> bytes(const std::string& s) {
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

std::string normalized(const std::string& path) {
    const fs::path p(std::u8string(path.begin(), path.end()));
    const std::u8string s = p.lexically_normal().generic_u8string();
    return {s.begin(), s.end()};
}

std::string withSlash(std::string dir) {
    dir = normalized(dir);
    if (!dir.empty() && dir.back() != '/') {
        dir += '/';
    }
    return dir;
}

std::string categoryOf(const std::string& key) {
    return key.substr(0, key.find('/'));
}

Kind kindOf(const std::string& key) {
    if (key.ends_with(".world")) return Kind::World;
    if (key.ends_with(".tmj")) return Kind::Room;
    if (key.ends_with(".tsj")) return Kind::Tileset;
    if (key.ends_with(".look.json")) return Kind::Look;
    if (key.ends_with(".effect.json")) return Kind::Effect;
    if (key.ends_with(".plant.json")) return Kind::Plant;
    if (key.ends_with("tuning.json")) return Kind::Tuning;
    if (key.ends_with(".wav") || key.ends_with(".ogg")) return Kind::Sound;
    return Kind::Json;
}

// Tiled bookkeeping the game never reads.
void stripEditorFields(Json& json) {
    for (const char* key : {"tiledversion", "editorsettings"}) {
        json.erase(key);
    }
}

// Every Tiled property of type "file", anywhere in the document.
void collectFileProperties(const Json& node, std::vector<std::string>& out) {
    if (node.is_object()) {
        if (const auto props = node.find("properties"); props != node.end() && props->is_array()) {
            for (const Json& p : *props) {
                if (p.value("type", "") == "file" && p.contains("value") && p["value"].is_string() &&
                    !p["value"].get<std::string>().empty()) {
                    out.push_back(p["value"].get<std::string>());
                }
            }
        }
        for (const auto& [key, child] : node.items()) {
            collectFileProperties(child, out);
        }
    } else if (node.is_array()) {
        for (const Json& child : node) {
            collectFileProperties(child, out);
        }
    }
}

struct Atlas {
    uint32_t width = 0;
    uint32_t height = 0;
    struct Placed {
        std::string name;
        uint32_t x, y;
        Image albedo, normal, emissive;
        bool hasNormal = false;
        bool hasEmissive = false;
    };
    std::vector<Placed> placed;
};

// Shelf packing in a deterministic order (tallest first, then widest, then by name); the width is
// the smallest power of two that fits the widest image and roughly a square of the total area.
void pack(Atlas& atlas) {
    std::ranges::sort(atlas.placed, [](const Atlas::Placed& a, const Atlas::Placed& b) {
        if (a.albedo.height != b.albedo.height) return a.albedo.height > b.albedo.height;
        if (a.albedo.width != b.albedo.width) return a.albedo.width > b.albedo.width;
        return a.name < b.name;
    });
    uint64_t area = 0;
    uint32_t widest = 1;
    for (const Atlas::Placed& p : atlas.placed) {
        area += uint64_t{p.albedo.width} * p.albedo.height;
        widest = std::max(widest, p.albedo.width);
    }
    uint32_t width = 1;
    while (width < widest || uint64_t{width} * width < area) {
        width *= 2;
    }
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t shelf = 0;
    for (Atlas::Placed& p : atlas.placed) {
        if (x + p.albedo.width > width) {
            x = 0;
            y += shelf;
            shelf = 0;
        }
        p.x = x;
        p.y = y;
        x += p.albedo.width;
        shelf = std::max(shelf, p.albedo.height);
    }
    atlas.width = width;
    atlas.height = std::max(1u, y + shelf);
}

Image page(const Atlas& atlas, int which) {
    // Uncovered pixels: transparent albedo, flat normal, no emission (the renderer's defaults).
    static constexpr uint8_t kFill[3][4] = {{0, 0, 0, 0}, {128, 128, 255, 255}, {0, 0, 0, 255}};
    Image out;
    out.width = atlas.width;
    out.height = atlas.height;
    out.pixels.resize(size_t{out.width} * out.height * 4);
    for (size_t i = 0; i < out.pixels.size(); i += 4) {
        std::memcpy(&out.pixels[i], kFill[which], 4);
    }
    for (const Atlas::Placed& p : atlas.placed) {
        const Image* src = which == 0 ? &p.albedo : which == 1 ? (p.hasNormal ? &p.normal : nullptr)
                                                               : (p.hasEmissive ? &p.emissive : nullptr);
        for (uint32_t row = 0; row < p.albedo.height; ++row) {
            uint8_t* dst = &out.pixels[(size_t{p.y + row} * out.width + p.x) * 4];
            if (src) {
                std::memcpy(dst, &src->pixels[size_t{row} * src->width * 4], size_t{src->width} * 4);
            } else {
                for (uint32_t col = 0; col < p.albedo.width; ++col) {
                    std::memcpy(dst + col * 4, kFill[which], 4);
                }
            }
        }
    }
    return out;
}

class Cook {
public:
    explicit Cook(const Options& options)
        : options_(options), sourceRoot_(withSlash(options.sourceRoot)), shaderDir_(withSlash(options.shaderDirectory)) {}

    Result run();

private:
    std::string absolute(const std::string& key) const { return sourceRoot_ + key; }
    std::optional<std::string> keyOf(const std::string& path) const {
        const std::string p = normalized(path);
        if (!p.starts_with(sourceRoot_)) {
            return std::nullopt;
        }
        return p.substr(sourceRoot_.size());
    }
    void error(std::string message) { result_.errors.push_back(std::move(message)); }
    void enqueue(const std::string& key, Kind kind, const std::string& from);
    bool load(const std::string& key, Kind kind);
    void process(const std::string& key);
    void cookAtlas(const Json& manifest);
    void validateCooked();
    void findUnreferencedAndDuplicates();
    TextReader cookedReader() const;

    Options options_;
    std::string sourceRoot_;
    std::string shaderDir_;
    Result result_;
    std::map<std::string, Item> items_; // by pack path
    std::deque<std::string> queue_;
    std::set<std::string> consumed_;    // source files read (by key; shaders as "shaders/x.ashd")
    std::string worldKey_;
};

void Cook::enqueue(const std::string& key, Kind kind, const std::string& from) {
    if (items_.contains(key)) {
        return;
    }
    if (!fileExists(absolute(key))) {
        error(std::format("{}: refers to {}, which does not exist", from, key));
        return;
    }
    if (load(key, kind)) {
        queue_.push_back(key);
    }
}

bool Cook::load(const std::string& key, Kind kind) {
    std::string readError;
    const auto data = readFile(absolute(key), &readError);
    if (!data) {
        error(std::format("{}: {}", key, readError));
        return false;
    }
    items_[key] = {kind, *data, {}};
    consumed_.insert(key);
    return true;
}

TextReader Cook::cookedReader() const {
    return [this](const std::string& path) -> std::optional<std::string> {
        const std::optional<std::string> key = keyOf(path);
        if (!key) {
            return std::nullopt;
        }
        const auto it = items_.find(*key);
        if (it == items_.end()) {
            return std::nullopt;
        }
        return text(it->second.cooked.empty() ? it->second.source : it->second.cooked);
    };
}

// Validates a source file with the game's own parser and follows its references.
void Cook::process(const std::string& key) {
    Item& item = items_.at(key);
    const std::string path = absolute(key);
    const std::string source = text(item.source);
    std::string parseError;
    const auto sourceReader = [](const std::string& p) { return readText(p); };
    switch (item.kind) {
    case Kind::World: {
        const std::optional<World> world = parseWorld(source, path, &parseError);
        if (!world) {
            error(parseError);
            return;
        }
        for (const WorldRoom& room : world->rooms) {
            if (const auto roomKey = keyOf(room.path)) {
                enqueue(*roomKey, Kind::Room, key);
            } else {
                error(std::format("{}: room {} is outside the source tree", key, room.path));
            }
        }
        break;
    }
    case Kind::Room: {
        if (!parseRoom(source, path, sourceReader, &parseError)) {
            error(parseError);
            return;
        }
        const Json json = Json::parse(source);
        if (const auto tilesets = json.find("tilesets"); tilesets != json.end()) {
            for (const Json& t : *tilesets) {
                if (t.contains("source")) {
                    if (const auto k = keyOf(resolvePath(path, t["source"].get<std::string>()))) {
                        enqueue(*k, Kind::Tileset, key);
                    }
                }
            }
        }
        std::vector<std::string> files;
        collectFileProperties(json, files);
        for (const std::string& file : files) {
            if (const auto k = keyOf(resolvePath(path, file))) {
                enqueue(*k, kindOf(*k), key);
            } else {
                error(std::format("{}: file property {} points outside the source tree", key, file));
            }
        }
        break;
    }
    case Kind::Look: {
        if (!parseLook(source, path, sourceReader, Look::engineDefaults(), &parseError)) {
            error(parseError);
            return;
        }
        const Json json = Json::parse(source);
        if (json.contains("base") && json["base"].is_string()) {
            if (const auto k = keyOf(resolvePath(path, json["base"].get<std::string>()))) {
                enqueue(*k, Kind::Look, key);
            }
        }
        break;
    }
    case Kind::Effect:
        if (!parseEffect(source, path, &parseError)) {
            error(parseError);
        }
        break;
    case Kind::Plant:
        if (!parsePlant(source, path, &parseError)) {
            error(parseError);
        }
        break;
    case Kind::SpriteSheet:
        if (!parseAsepriteSheet(source, path, &parseError)) {
            error(parseError);
        }
        break;
    case Kind::Tuning:
    case Kind::Tileset:
    case Kind::Json:
        try {
            const Json json = Json::parse(source);
            if (item.kind == Kind::Tuning && json.value("format", "") != "animal-tuning") {
                error(key + ": not a tuning file");
            }
        } catch (const Json::exception& e) {
            error(std::format("{}: not valid JSON ({})", key, e.what()));
        }
        break;
    case Kind::Shader:
        if (!rhi::parseShaderPackage(item.source, &parseError)) {
            error(std::format("{}: {}", key, parseError));
        }
        item.cooked = item.source; // already a compiled binary package
        return;
    case Kind::Sound:
        if (!audio::decodeSound(item.source.data(), item.source.size(), audio::AudioSystem::kSampleRate, &parseError)) {
            error(std::format("{}: {}", key, parseError));
        }
        item.cooked = item.source; // decoded at load time (WAV / Ogg Vorbis)
        return;
    }
    // Compact JSON: no whitespace, no editor bookkeeping.
    try {
        Json json = Json::parse(source);
        if (item.kind == Kind::World || item.kind == Kind::Room || item.kind == Kind::Tileset) {
            stripEditorFields(json);
        }
        item.cooked = bytes(json.dump());
    } catch (const Json::exception&) {
        item.cooked = item.source; // already reported above
    }
}

void Cook::cookAtlas(const Json& manifest) {
    const std::string atlasKey = manifest.value("atlas", "textures/atlas");
    const std::string atlasDir = atlasKey.substr(0, atlasKey.find_last_of('/') + 1);
    Atlas atlas;
    for (const Json& m : manifest.value("materials", Json::array())) {
        const std::string base = m.get<std::string>();
        if (base.substr(0, base.find_last_of('/') + 1) != atlasDir) {
            error(std::format("material {} is not in the atlas folder {}", base, atlasDir));
            continue;
        }
        Atlas::Placed placed;
        placed.name = base.substr(atlasDir.size());
        std::string imageError;
        const auto albedo = loadImage(absolute(base + ".png"), &imageError);
        if (!albedo) {
            error(std::format("{}.png: {}", base, imageError));
            continue;
        }
        placed.albedo = *albedo;
        consumed_.insert(base + ".png");
        for (const auto& [suffix, image, has] : {std::tuple{"_n", &placed.normal, &placed.hasNormal},
                                                 std::tuple{"_e", &placed.emissive, &placed.hasEmissive}}) {
            const std::string key = base + suffix + ".png";
            if (!fileExists(absolute(key))) {
                continue;
            }
            const auto companion = loadImage(absolute(key), &imageError);
            if (!companion || companion->width != albedo->width || companion->height != albedo->height) {
                error(std::format("{}: unreadable or not the size of its albedo", key));
                continue;
            }
            *image = *companion;
            *has = true;
            consumed_.insert(key);
        }
        atlas.placed.push_back(std::move(placed));
    }
    if (atlas.placed.empty()) {
        return;
    }
    pack(atlas);
    const std::string pages[3] = {atlasKey + ".tex", atlasKey + "_n.tex", atlasKey + "_e.tex"};
    for (int i = 0; i < 3; ++i) {
        items_[pages[i]] = {Kind::Json, {}, encodeCookedImage(page(atlas, i))};
    }
    Json index = {{"format", "animal-atlas"}, {"version", 1}, {"size", {atlas.width, atlas.height}}};
    const std::string name = atlasKey.substr(atlasDir.size());
    index["pages"] = {{"albedo", name + ".tex"}, {"normal", name + "_n.tex"}, {"emissive", name + "_e.tex"}};
    index["materials"] = Json::object();
    uint64_t covered = 0;
    std::vector<const Atlas::Placed*> byName;
    for (const Atlas::Placed& p : atlas.placed) {
        byName.push_back(&p);
        covered += uint64_t{p.albedo.width} * p.albedo.height;
    }
    std::ranges::sort(byName, {}, &Atlas::Placed::name);
    for (const Atlas::Placed* p : byName) {
        index["materials"][p->name] = {p->x, p->y, p->albedo.width, p->albedo.height};
    }
    items_[atlasDir + "atlas.json"] = {Kind::Json, {}, bytes(index.dump())};
    result_.atlas = {atlas.width, atlas.height, static_cast<uint32_t>(atlas.placed.size()),
                     static_cast<double>(covered) / (double(atlas.width) * atlas.height)};
}

// The cooked (compact) data must load exactly like the source did.
void Cook::validateCooked() {
    const TextReader reader = cookedReader();
    for (const auto& [key, item] : items_) {
        const std::string path = absolute(key);
        const std::string cooked = text(item.cooked);
        std::string parseError;
        bool ok = true;
        switch (item.kind) {
        case Kind::World:
            ok = parseWorld(cooked, path, &parseError).has_value();
            break;
        case Kind::Room:
            ok = parseRoom(cooked, path, reader, &parseError).has_value();
            break;
        case Kind::Look:
            ok = parseLook(cooked, path, reader, Look::engineDefaults(), &parseError).has_value();
            break;
        case Kind::Effect:
            ok = parseEffect(cooked, path, &parseError).has_value();
            break;
        case Kind::Plant:
            ok = parsePlant(cooked, path, &parseError).has_value();
            break;
        case Kind::SpriteSheet:
            ok = parseAsepriteSheet(cooked, path, &parseError).has_value();
            break;
        default:
            break;
        }
        if (!ok) {
            error(std::format("{} (cooked): {}", key, parseError));
        }
    }
}

void Cook::findUnreferencedAndDuplicates() {
    std::map<std::pair<uint64_t, uint64_t>, std::vector<std::string>> byContent;
    const auto visit = [&](const std::string& dir, const std::string& keyPrefix, const std::string& rootForKey) {
        std::error_code ec;
        std::vector<std::string> files;
        for (fs::recursive_directory_iterator it(fs::path(std::u8string(dir.begin(), dir.end())), ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().filename().u8string().starts_with(u8".")) {
                continue;
            }
            const std::u8string u8 = it->path().lexically_normal().generic_u8string();
            files.emplace_back(u8.begin(), u8.end());
        }
        std::ranges::sort(files);
        for (const std::string& file : files) {
            const std::string key = keyPrefix + file.substr(rootForKey.size());
            if (!consumed_.contains(key)) {
                result_.unreferenced.push_back(key);
                continue;
            }
            if (const auto data = readFile(file)) {
                byContent[{contentHash(data->data(), data->size()), data->size()}].push_back(key);
            }
        }
    };
    visit(sourceRoot_ + "world", "", sourceRoot_);
    visit(sourceRoot_ + "textures", "", sourceRoot_);
    visit(sourceRoot_ + "sounds", "", sourceRoot_);
    visit(shaderDir_, "shaders/", shaderDir_);
    for (auto& [content, keys] : byContent) {
        if (keys.size() > 1) {
            result_.duplicates.push_back(keys);
        }
    }
    for (const std::string& key : result_.unreferenced) {
        result_.warnings.push_back(key + ": not referenced (not packed)");
    }
}

Result Cook::run() {
    const auto manifestText = readText(options_.manifest);
    Json manifest;
    try {
        manifest = Json::parse(manifestText.value_or(""));
    } catch (const Json::exception& e) {
        error(std::format("{}: cannot read the manifest ({})", options_.manifest, e.what()));
        return result_;
    }
    if (manifest.value("format", "") != "animal-cook" || manifest.value("version", 0) != 1) {
        error(options_.manifest + ": not a cook manifest (format animal-cook, version 1)");
        return result_;
    }
    result_.dataVersion = manifest.value("dataVersion", "");

    worldKey_ = manifest.value("world", "");
    if (!worldKey_.empty()) {
        enqueue(worldKey_, Kind::World, options_.manifest);
    }
    for (const Json& f : manifest.value("files", Json::array())) {
        enqueue(f.get<std::string>(), kindOf(f.get<std::string>()), options_.manifest);
    }
    for (const Json& f : manifest.value("spriteSheets", Json::array())) {
        enqueue(f.get<std::string>(), Kind::SpriteSheet, options_.manifest);
    }
    for (const Json& f : manifest.value("sounds", Json::array())) {
        enqueue(f.get<std::string>(), Kind::Sound, options_.manifest);
    }
    for (const Json& s : manifest.value("shaders", Json::array())) {
        const std::string name = s.get<std::string>() + ".ashd";
        std::string readError;
        const auto data = readFile(shaderDir_ + name, &readError);
        if (!data) {
            error(std::format("shader {}: {}", name, readError));
            continue;
        }
        items_["shaders/" + name] = {Kind::Shader, *data, {}};
        consumed_.insert("shaders/" + name);
        queue_.push_back("shaders/" + name);
    }
    while (!queue_.empty()) {
        const std::string key = queue_.front();
        queue_.pop_front();
        process(key);
    }
    cookAtlas(manifest);
    validateCooked();
    findUnreferencedAndDuplicates();

    std::vector<PackInput> inputs;
    for (const auto& [key, item] : items_) {
        Category& c = result_.categories[categoryOf(key)];
        ++c.files;
        c.sourceBytes += item.source.size();
        c.cookedBytes += item.cooked.size();
        result_.entries.emplace_back(key, item.cooked.size());
        inputs.push_back({key, item.cooked, true});
    }
    // Atlas sources count as texture source bytes.
    for (const std::string& key : consumed_) {
        if (key.starts_with("textures/") && key.ends_with(".png")) {
            if (const auto data = readFile(absolute(key))) {
                result_.categories["textures"].sourceBytes += data->size();
            }
        }
    }
    result_.pack = writePack(std::move(inputs), result_.dataVersion, options_.level, &result_.stats);
    // Stored bytes per category, from the written table of contents (shared contents once).
    std::set<uint64_t> counted;
    const std::string tmp = (fs::temp_directory_path() / "animal_cook_probe.pak").generic_string();
    if (writeFileAtomic(tmp, result_.pack.data(), result_.pack.size())) {
        if (const auto pack = Pack::open(tmp)) {
            for (const PackEntry& e : pack->entries()) {
                if (counted.insert(e.offset).second) {
                    result_.categories[categoryOf(e.path)].packedBytes += e.storedSize;
                }
            }
        }
        deleteFile(tmp);
    }
    result_.ok = result_.errors.empty();
    return result_;
}

} // namespace

Result cook(const Options& options) {
    return Cook(options).run();
}

std::string reportJson(const Result& r) {
    Json report = {{"format", "animal-cook-report"}, {"version", 1}, {"ok", r.ok}, {"dataVersion", r.dataVersion}};
    report["packBytes"] = r.pack.size();
    Json categories = Json::object();
    uint64_t source = 0;
    uint64_t cooked = 0;
    for (const auto& [name, c] : r.categories) {
        categories[name] = {{"files", c.files}, {"sourceBytes", c.sourceBytes}, {"cookedBytes", c.cookedBytes},
                            {"packedBytes", c.packedBytes}};
        source += c.sourceBytes;
        cooked += c.cookedBytes;
    }
    report["categories"] = categories;
    report["totals"] = {{"sourceBytes", source}, {"cookedBytes", cooked}, {"packBytes", r.pack.size()},
                        {"sharedEntries", r.stats.sharedEntries}};
    report["atlas"] = {{"width", r.atlas.width}, {"height", r.atlas.height}, {"materials", r.atlas.materials},
                       {"coverage", std::round(r.atlas.coverage * 1000.0) / 1000.0}};
    report["duplicates"] = r.duplicates;
    report["unreferenced"] = r.unreferenced;
    report["errors"] = r.errors;
    Json entries = Json::array();
    for (const auto& [path, size] : r.entries) {
        entries.push_back({{"path", path}, {"bytes", size}});
    }
    report["entries"] = entries;
    return report.dump(1) + "\n";
}

std::string reportText(const Result& r) {
    std::string out = std::format("Cook {} (data version \"{}\")\n", r.ok ? "succeeded" : "FAILED", r.dataVersion);
    out += std::format("  {:<10} {:>6} {:>12} {:>12} {:>12}\n", "", "files", "source", "cooked", "packed");
    uint64_t source = 0;
    uint64_t cooked = 0;
    uint64_t packed = 0;
    for (const auto& [name, c] : r.categories) {
        out += std::format("  {:<10} {:>6} {:>12} {:>12} {:>12}\n", name, c.files, c.sourceBytes, c.cookedBytes,
                           c.packedBytes);
        source += c.sourceBytes;
        cooked += c.cookedBytes;
        packed += c.packedBytes;
    }
    out += std::format("  {:<10} {:>6} {:>12} {:>12} {:>12}   pack file {} bytes\n", "total", r.entries.size(), source,
                       cooked, packed, r.pack.size());
    if (r.atlas.materials > 0) {
        out += std::format("  atlas {}x{} with {} materials ({:.0f}% covered)\n", r.atlas.width, r.atlas.height,
                           r.atlas.materials, r.atlas.coverage * 100.0);
    }
    for (const auto& group : r.duplicates) {
        std::string list;
        for (const std::string& key : group) {
            list += (list.empty() ? "" : ", ") + key;
        }
        out += "  duplicate contents: " + list + "\n";
    }
    for (const std::string& key : r.unreferenced) {
        out += "  unreferenced: " + key + "\n";
    }
    for (const std::string& e : r.errors) {
        out += "  ERROR " + e + "\n";
    }
    return out;
}

std::vector<std::string> verifyPack(const std::string& packPath, const std::string& manifestPath) {
    std::vector<std::string> problems;
    const std::string root = "/animal-verify/";
    std::string mountError;
    if (!mountPack(packPath, root, &mountError)) {
        return {mountError};
    }
    const Pack* pack = packMountedAt(root + "x");
    for (const PackEntry& e : pack->entries()) {
        std::string readError;
        if (!pack->read(e, &readError)) {
            problems.push_back(readError);
        }
    }
    Json manifest;
    try {
        manifest = Json::parse(readText(manifestPath).value_or("{}"));
    } catch (const Json::exception& e) {
        problems.push_back(std::string("manifest: ") + e.what());
    }
    // What the game loads, through the mounted pack.
    if (const std::string worldKey = manifest.value("world", ""); !worldKey.empty()) {
        if (const auto world = loadWorld(root + worldKey)) {
            for (const WorldRoom& room : world->rooms) {
                const auto loaded = loadRoom(room.path);
                if (!loaded) {
                    problems.push_back(room.path + ": does not load from the pack");
                    continue;
                }
                if (const std::string look = loaded->properties.getString("look"); !look.empty() &&
                    !loadLook(resolvePath(room.path, look), Look::engineDefaults())) {
                    problems.push_back(room.path + ": its look does not load from the pack");
                }
            }
        } else {
            problems.push_back(worldKey + ": the world does not load from the pack");
        }
    }
    for (const Json& f : manifest.value("files", Json::array())) {
        if (!fileExists(root + f.get<std::string>())) {
            problems.push_back(f.get<std::string>() + ": missing from the pack");
        }
    }
    for (const Json& f : manifest.value("spriteSheets", Json::array())) {
        if (!loadSpriteSheet(root + f.get<std::string>())) {
            problems.push_back(f.get<std::string>() + ": does not load from the pack");
        }
    }
    for (const Json& s : manifest.value("shaders", Json::array())) {
        const auto data = readFile(root + "shaders/" + s.get<std::string>() + ".ashd");
        std::string parseError;
        if (!data || !rhi::parseShaderPackage(*data, &parseError)) {
            problems.push_back(s.get<std::string>() + ": shader missing or invalid in the pack");
        }
    }
    for (const Json& f : manifest.value("sounds", Json::array())) {
        const auto data = readFile(root + f.get<std::string>());
        std::string decodeError;
        if (!data || !audio::decodeSound(data->data(), data->size(), audio::AudioSystem::kSampleRate, &decodeError)) {
            problems.push_back(f.get<std::string>() + ": sound missing or unreadable in the pack");
        }
    }
    const std::string atlas = manifest.value("atlas", "textures/atlas");
    for (const char* suffix : {".tex", "_n.tex", "_e.tex"}) {
        std::string imageError;
        if (!loadImage(root + atlas + suffix, &imageError)) {
            problems.push_back(atlas + suffix + ": " + imageError);
        }
    }
    unmountPacks();
    return problems;
}

} // namespace animal::cooker
