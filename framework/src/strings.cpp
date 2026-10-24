#include <animal/core/log.h>
#include <animal/framework/level.h>
#include <animal/framework/strings.h>

#include <json.hpp>

#include <format>

namespace animal {

std::optional<StringTable> parseStringTable(std::string_view json, const std::string& path, std::string* error) {
    using Json = nlohmann::json;
    const auto fail = [&](const std::string& message) -> std::optional<StringTable> {
        if (error) {
            *error = std::format("{}: {}", path, message);
        }
        return std::nullopt;
    };
    Json root;
    try {
        root = Json::parse(json.begin(), json.end());
    } catch (const Json::exception& e) {
        return fail(std::string("invalid JSON (") + e.what() + ")");
    }
    if (!root.is_object() || root.value("format", "") != "animal-strings") {
        return fail("not a string table");
    }
    if (root.value("version", 0) != 1) {
        return fail("unsupported version");
    }
    StringTable table;
    const auto language = root.find("language");
    const auto name = root.find("name");
    const auto strings = root.find("strings");
    if (language == root.end() || !language->is_string() || name == root.end() || !name->is_string()) {
        return fail("\"language\" and \"name\" must be strings");
    }
    if (strings == root.end() || !strings->is_object()) {
        return fail("\"strings\" must be an object");
    }
    table.language = language->get<std::string>();
    table.name = name->get<std::string>();
    for (const auto& [key, value] : strings->items()) {
        if (!value.is_string()) {
            return fail(std::format("\"{}\" must be a string", key));
        }
        table.strings.emplace(key, value.get<std::string>());
    }
    return table;
}

std::optional<StringTable> loadStringTable(const std::string& path) {
    const std::optional<std::string> text = readText(path);
    if (!text) {
        log::error("Cannot read string table '{}'", path);
        return std::nullopt;
    }
    std::string error;
    std::optional<StringTable> table = parseStringTable(*text, path, &error);
    if (!table) {
        log::error("Invalid string table: {}", error);
    }
    return table;
}

std::string formatText(std::string_view pattern, std::span<const std::string> args) {
    std::string out;
    out.reserve(pattern.size());
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{') {
            const size_t close = pattern.find('}', i);
            if (close != std::string_view::npos && close > i + 1) {
                size_t index = 0;
                bool digits = true;
                for (size_t j = i + 1; j < close; ++j) {
                    digits = digits && pattern[j] >= '0' && pattern[j] <= '9';
                    index = index * 10 + static_cast<size_t>(pattern[j] - '0');
                }
                if (digits && index < args.size()) {
                    out += args[index];
                    i = close;
                    continue;
                }
            }
        }
        out += pattern[i];
    }
    return out;
}

void Strings::set(StringTable language, std::optional<StringTable> fallback) {
    language_ = std::move(language);
    fallback_ = std::move(fallback);
}

std::string Strings::get(std::string_view key) const {
    if (const auto it = language_.strings.find(key); it != language_.strings.end()) {
        return it->second;
    }
    if (fallback_) {
        if (const auto it = fallback_->strings.find(key); it != fallback_->strings.end()) {
            return it->second;
        }
    }
    return std::string(key);
}

bool Strings::has(std::string_view key) const {
    return language_.strings.contains(key) || (fallback_ && fallback_->strings.contains(key));
}

} // namespace animal
