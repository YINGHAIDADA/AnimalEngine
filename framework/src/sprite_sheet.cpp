#include <animal/core/log.h>
#include <animal/framework/level.h>
#include <animal/framework/sprite_sheet.h>

#include <json.hpp>

#include <algorithm>
#include <format>

namespace animal {

namespace {

using Json = nlohmann::ordered_json; // hash frames keep the file's order

struct Failure {
    std::string message;
};

int integer(const Json& object, const char* key, const std::string& where) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer()) {
        throw Failure{std::format("{}: \"{}\" must be a whole number", where, key)};
    }
    return it->get<int>();
}

const Json& member(const Json& object, const char* key, const std::string& where) {
    const auto it = object.find(key);
    if (!object.is_object() || it == object.end()) {
        throw Failure{std::format("{}: \"{}\" is missing", where, key)};
    }
    return *it;
}

SpriteFrame parseFrame(const Json& f, const std::string& where, Vec2i imageSize) {
    if (!f.is_object()) {
        throw Failure{where + ": a frame must be an object"};
    }
    if (f.value("rotated", false)) {
        throw Failure{where + ": rotated frames are not supported (turn off \"Rotate\" in the export)"};
    }
    SpriteFrame frame;
    const Json& rect = member(f, "frame", where);
    frame.source = {integer(rect, "x", where), integer(rect, "y", where), integer(rect, "w", where),
                    integer(rect, "h", where)};
    if (frame.source.width <= 0 || frame.source.height <= 0 || frame.source.x < 0 || frame.source.y < 0 ||
        frame.source.x + frame.source.width > imageSize.x || frame.source.y + frame.source.height > imageSize.y) {
        throw Failure{where + ": the frame lies outside the sheet image"};
    }
    frame.sourceSize = {frame.source.width, frame.source.height};
    if (const auto sss = f.find("spriteSourceSize"); sss != f.end()) {
        frame.offset = {integer(*sss, "x", where), integer(*sss, "y", where)};
    }
    if (const auto size = f.find("sourceSize"); size != f.end()) {
        frame.sourceSize = {integer(*size, "w", where), integer(*size, "h", where)};
    }
    const Json& duration = member(f, "duration", where);
    if (!duration.is_number() || duration.get<double>() <= 0.0) {
        throw Failure{where + ": \"duration\" must be a positive number of milliseconds"};
    }
    frame.duration = duration.get<double>() / 1000.0;
    return frame;
}

AnimationClip clipOf(const SpriteSheet& sheet, const Json& tag, const std::string& where) {
    const int from = integer(tag, "from", where);
    const int to = integer(tag, "to", where);
    if (from < 0 || to < from || to >= static_cast<int>(sheet.frames.size())) {
        throw Failure{std::format("{}: frames {}..{} are not in the sheet ({} frames)", where, from, to,
                                  sheet.frames.size())};
    }
    const std::string direction = tag.value("direction", "forward");
    std::vector<int> forward;
    for (int i = from; i <= to; ++i) {
        forward.push_back(i);
    }
    std::vector<int> order;
    if (direction == "forward") {
        order = forward;
    } else if (direction == "reverse") {
        order.assign(forward.rbegin(), forward.rend());
    } else if (direction == "pingpong" || direction == "pingpong_reverse") {
        order = forward;
        for (int i = to - 1; i > from; --i) {
            order.push_back(i);
        }
        if (direction == "pingpong_reverse") {
            std::ranges::reverse(order);
        }
    } else {
        throw Failure{where + ": unknown direction \"" + direction + "\""};
    }

    AnimationClip clip;
    clip.frames = order;
    std::string repeat = "0";
    if (const auto it = tag.find("repeat"); it != tag.end()) {
        repeat = it->is_string() ? it->get<std::string>() : it->dump();
    }
    clip.loop = repeat != "1";
    for (const int index : order) {
        clip.durations.push_back(static_cast<float>(sheet.frames[static_cast<size_t>(index)].duration));
    }
    // Equal durations: a fixed rate, computed in double so 1000/3 ms frames give exactly 3 per second.
    const double first = static_cast<double>(sheet.frames[static_cast<size_t>(order.front())].duration);
    const bool uniform = std::ranges::all_of(order, [&](int i) {
        return sheet.frames[static_cast<size_t>(i)].duration == sheet.frames[static_cast<size_t>(order.front())].duration;
    });
    if (uniform) {
        clip.framesPerSecond = static_cast<float>(1.0 / first);
        clip.durations.clear();
    }
    return clip;
}

} // namespace

const AnimationClip* SpriteSheet::clip(std::string_view name) const {
    for (const Tag& tag : tags) {
        if (tag.name == name) {
            return &tag.clip;
        }
    }
    return nullptr;
}

std::optional<SpriteSheet> parseAsepriteSheet(std::string_view text, const std::string& path, std::string* error) {
    Json json;
    try {
        json = Json::parse(text);
    } catch (const Json::exception& e) {
        if (error) {
            *error = path + ": not valid JSON (" + e.what() + ")";
        }
        return std::nullopt;
    }
    try {
        if (!json.is_object()) {
            throw Failure{path + ": a sprite sheet must be a JSON object"};
        }
        SpriteSheet sheet;
        const Json& meta = member(json, "meta", path);
        const Json& image = member(meta, "image", path + " meta");
        if (!image.is_string() || image.get<std::string>().empty()) {
            throw Failure{path + ": \"meta.image\" must name the sheet image"};
        }
        sheet.image = resolvePath(path, image.get<std::string>());
        const Json& size = member(meta, "size", path + " meta");
        sheet.size = {integer(size, "w", path + " meta.size"), integer(size, "h", path + " meta.size")};

        const Json& frames = member(json, "frames", path);
        if (frames.is_array()) {
            for (size_t i = 0; i < frames.size(); ++i) {
                sheet.frames.push_back(parseFrame(frames[i], std::format("{} frame {}", path, i), sheet.size));
            }
        } else if (frames.is_object()) {
            for (const auto& [name, f] : frames.items()) {
                sheet.frames.push_back(parseFrame(f, path + " frame \"" + name + "\"", sheet.size));
            }
        } else {
            throw Failure{path + ": \"frames\" must be an array or an object"};
        }
        if (sheet.frames.empty()) {
            throw Failure{path + ": the sheet has no frames"};
        }

        if (const auto tags = meta.find("frameTags"); tags != meta.end() && tags->is_array()) {
            for (const Json& tag : *tags) {
                const std::string name = tag.value("name", "");
                const std::string where = path + " tag \"" + name + "\"";
                if (name.empty()) {
                    throw Failure{path + ": a tag has no name"};
                }
                if (sheet.clip(name)) {
                    throw Failure{where + ": the name is used twice"};
                }
                sheet.tags.push_back({name, clipOf(sheet, tag, where)});
            }
        }
        return sheet;
    } catch (const Failure& failure) {
        if (error) {
            *error = failure.message;
        }
    } catch (const Json::exception& e) {
        if (error) {
            *error = path + ": " + e.what();
        }
    }
    return std::nullopt;
}

std::optional<SpriteSheet> loadSpriteSheet(const std::string& path) {
    const std::optional<std::string> text = readText(path);
    if (!text) {
        log::error("Cannot read sprite sheet '{}'", path);
        return std::nullopt;
    }
    std::string error;
    std::optional<SpriteSheet> sheet = parseAsepriteSheet(*text, path, &error);
    if (!sheet) {
        log::error("Invalid sprite sheet: {}", error);
    }
    return sheet;
}

} // namespace animal
