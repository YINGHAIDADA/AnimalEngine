#pragma once

// String tables (ADR-0041): the text the player reads, one table per language, looked up by key.
//   strings/<language>.strings.json:
//   {"format": "animal-strings", "version": 1, "language": "zh-Hans", "name": "简体中文",
//    "strings": {"menu.continue": "继续", "message.gate": "闸门需要 {0} 颗宝石（已找到 {1} 颗）"}}
// `name` is the language's own name (shown in the language menu). "{0}", "{1}", ... are arguments.

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace animal {

struct StringTable {
    std::string language;
    std::string name;
    std::map<std::string, std::string, std::less<>> strings;
};

std::optional<StringTable> parseStringTable(std::string_view json, const std::string& path, std::string* error);
std::optional<StringTable> loadStringTable(const std::string& path); // logs errors

// Replaces "{0}", "{1}", ... with the arguments; other braces are kept.
std::string formatText(std::string_view pattern, std::span<const std::string> args);

class Strings {
public:
    // `fallback` answers keys the language lacks (a table added later may be incomplete).
    void set(StringTable language, std::optional<StringTable> fallback = std::nullopt);

    // The language's text, else the fallback's, else the key itself (so a missing entry shows up).
    std::string get(std::string_view key) const;
    bool has(std::string_view key) const;
    std::string format(std::string_view key, std::span<const std::string> args) const {
        return formatText(get(key), args);
    }
    const std::string& language() const { return language_.language; }
    const std::string& name() const { return language_.name; } // the language's own name

private:
    StringTable language_;
    std::optional<StringTable> fallback_;
};

} // namespace animal
