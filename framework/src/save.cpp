#include <animal/core/file.h>
#include <animal/core/log.h>
#include <animal/framework/save.h>

#include <json.hpp>

namespace animal {

namespace {

using Json = nlohmann::json;
constexpr const char* kFormat = "animal-save";

} // namespace

std::string serializeSave(const SaveData& save) {
    Json json;
    json["format"] = kFormat;
    json["version"] = SaveData::kVersion;
    json["room"] = save.room;
    json["position"] = {save.position.x, save.position.y};
    json["flags"] = save.flags;
    return json.dump(2) + "\n";
}

std::optional<SaveData> parseSave(std::string_view text, std::string* error) {
    auto fail = [error](const std::string& message) -> std::optional<SaveData> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    try {
        const Json json = Json::parse(text.begin(), text.end());
        if (!json.is_object() || json.value("format", "") != kFormat) {
            return fail("not an Animal Engine save");
        }
        const int version = json.value("version", 0);
        if (version != SaveData::kVersion) {
            return fail("save version " + std::to_string(version) + " is not supported (expected " +
                        std::to_string(SaveData::kVersion) + ")");
        }
        SaveData save;
        save.room = json.at("room").get<std::string>();
        const Json& position = json.at("position");
        if (!position.is_array() || position.size() != 2) {
            return fail("\"position\" must be [x, y]");
        }
        save.position = {position[0].get<float>(), position[1].get<float>()};
        for (const Json& flag : json.at("flags")) {
            save.flags.insert(flag.get<std::string>());
        }
        if (save.room.empty()) {
            return fail("\"room\" is empty");
        }
        return save;
    } catch (const Json::exception& e) {
        return fail(std::string("malformed save: ") + e.what());
    }
}

bool writeSave(const std::string& path, const SaveData& save) {
    const std::string text = serializeSave(save);
    if (!writeFileAtomic(path, text.data(), text.size())) {
        log::error("Could not write save '{}'", path);
        return false;
    }
    return true;
}

std::optional<SaveData> readSave(const std::string& path) {
    if (!fileExists(path)) {
        return std::nullopt;
    }
    const auto bytes = readFile(path);
    if (!bytes) {
        log::warn("Could not read save '{}'; starting fresh", path);
        return std::nullopt;
    }
    std::string error;
    auto save = parseSave(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), &error);
    if (!save) {
        log::warn("Ignoring save '{}': {}", path, error);
    }
    return save;
}

} // namespace animal
