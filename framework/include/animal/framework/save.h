#pragma once

#include <animal/core/math.h>

#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace animal {

// Persistent progress (ADR-0011, ADR-0032). Stored as versioned JSON; written atomically (temp file +
// rename) after keeping the previous good save as "<path>.bak".
struct SaveData {
    static constexpr int kVersion = 2;

    std::string room;            // room name (file stem) to continue in
    Vec2 position;               // player feet where the room was last entered
    std::set<std::string> flags; // collected items, opened doors, ... ("room:objectId")
    std::set<std::string> visited; // rooms entered at least once (the map); version 2
    double playSeconds = 0.0;      // version 2
};

std::string serializeSave(const SaveData& save);
// Accepts the current version and migrates older ones; rejects other formats, newer versions and
// malformed data (with a reason in `error`). `version` receives the file's version when readable.
std::optional<SaveData> parseSave(std::string_view text, std::string* error = nullptr, int* version = nullptr);

// Keeps the existing save as "<path>.bak" when it is valid, then writes atomically.
bool writeSave(const std::string& path, const SaveData& save);

struct SaveLoad {
    enum class Status {
        Missing,   // no save: a new game
        Loaded,
        Recovered, // the save was damaged; its backup was used (the damaged file is kept as .corrupt)
        TooNew,    // written by a newer version: do not overwrite it
        Unusable,  // save and backup both damaged (kept as .corrupt): a new game
    };
    Status status = Status::Missing;
    std::optional<SaveData> data;
    std::string message; // what happened, for the log / the player
};
SaveLoad loadSave(const std::string& path);

} // namespace animal
