#include <animal/core/file.h>
#include <animal/core/log.h>
#include <animal/framework/save.h>

#include <json.hpp>

namespace animal {

namespace {

using Json = nlohmann::json;
constexpr const char* kFormat = "animal-save";

std::optional<std::string> readAll(const std::string& path) {
    const auto bytes = readFile(path);
    if (!bytes) {
        return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

bool copyFile(const std::string& from, const std::string& to) {
    const auto text = readAll(from);
    return text && writeFileAtomic(to, text->data(), text->size());
}

} // namespace

std::string serializeSave(const SaveData& save) {
    Json json;
    json["format"] = kFormat;
    json["version"] = SaveData::kVersion;
    json["room"] = save.room;
    json["position"] = {save.position.x, save.position.y};
    json["flags"] = save.flags;
    json["visited"] = save.visited;
    json["playSeconds"] = save.playSeconds;
    return json.dump(2) + "\n";
}

std::optional<SaveData> parseSave(std::string_view text, std::string* error, int* versionOut) {
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
        if (versionOut) {
            *versionOut = version;
        }
        if (version < 1 || version > SaveData::kVersion) {
            return fail("save version " + std::to_string(version) + " is not supported (this game reads 1.." +
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
        if (version >= 2) {
            for (const Json& room : json.at("visited")) {
                save.visited.insert(room.get<std::string>());
            }
            save.playSeconds = json.at("playSeconds").get<double>();
        } else {
            save.visited.insert(save.room); // version 1 had no map: the room it continues in is known
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
    // The backup is only ever a good save: a damaged file never replaces it.
    if (const auto current = readAll(path); current && parseSave(*current)) {
        copyFile(path, path + ".bak");
    }
    const std::string text = serializeSave(save);
    if (!writeFileAtomic(path, text.data(), text.size())) {
        log::error("Could not write save '{}'", path);
        return false;
    }
    return true;
}

SaveLoad loadSave(const std::string& path) {
    SaveLoad result;
    const std::string backup = path + ".bak";
    if (!fileExists(path) && !fileExists(backup)) {
        return result;
    }
    std::string error;
    int version = 0;
    if (const auto text = readAll(path)) {
        if ((result.data = parseSave(*text, &error, &version))) {
            result.status = SaveLoad::Status::Loaded;
            if (version < SaveData::kVersion) {
                result.message = "save migrated from version " + std::to_string(version);
            }
            return result;
        }
        if (version > SaveData::kVersion) {
            result.status = SaveLoad::Status::TooNew;
            result.message = path + " was written by a newer version (" + error + "); it is left untouched";
            return result;
        }
    } else if (fileExists(path)) {
        error = "unreadable";
    } else {
        error = "missing";
    }
    // Damaged (or missing while a backup exists): keep the evidence, fall back to the backup.
    if (fileExists(path)) {
        copyFile(path, path + ".corrupt");
        deleteFile(path);
    }
    std::string backupError;
    if (const auto text = readAll(backup)) {
        if ((result.data = parseSave(*text, &backupError))) {
            result.status = SaveLoad::Status::Recovered;
            result.message = path + ": " + error + "; continuing from the backup";
            return result;
        }
    }
    result.status = SaveLoad::Status::Unusable;
    result.message = path + ": " + error + " and no usable backup; starting a new game";
    return result;
}

} // namespace animal
