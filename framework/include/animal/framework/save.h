#pragma once

#include <animal/core/math.h>

#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace animal {

// Persistent progress. Stored as versioned JSON; written atomically (temp file + rename).
struct SaveData {
    static constexpr int kVersion = 1;

    std::string room;           // room name (file stem) to continue in
    Vec2 position;              // player feet where the room was last entered
    std::set<std::string> flags; // collected items, opened doors, ... ("room:objectId")
};

std::string serializeSave(const SaveData& save);
// Rejects other formats, other versions and malformed data (with a reason in `error`).
std::optional<SaveData> parseSave(std::string_view text, std::string* error = nullptr);

bool writeSave(const std::string& path, const SaveData& save);
// nullopt if there is no save or it cannot be used (logged); the game then starts fresh.
std::optional<SaveData> readSave(const std::string& path);

} // namespace animal
