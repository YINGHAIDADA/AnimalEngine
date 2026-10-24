#include <animal/core/file.h>
#include <animal/core/log.h>
#include <animal/framework/settings.h>

#include <json.hpp>

#include <algorithm>
#include <cmath>

namespace animal {

namespace {

using Json = nlohmann::json;
constexpr const char* kFormat = "animal-settings";

float volume(const Json& json, const char* name, float fallback) {
    const auto it = json.find(name);
    if (it == json.end() || !it->is_number()) {
        return fallback;
    }
    const float v = it->get<float>();
    return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : fallback;
}

} // namespace

std::string serializeSettings(const Settings& s) {
    Json json;
    json["format"] = kFormat;
    json["version"] = Settings::kVersion;
    json["audio"] = {{"master", s.masterVolume}, {"effects", s.effectsVolume}, {"ambience", s.ambienceVolume},
                     {"music", s.musicVolume}};
    Json display = {{"fullscreen", s.fullscreen}, {"vsync", s.vsync}, {"frameCap", s.frameCap}};
    if (s.windowSize.x > 0 && s.windowSize.y > 0) {
        display["windowSize"] = {s.windowSize.x, s.windowSize.y};
    }
    if (s.windowPosition) {
        display["windowPosition"] = {s.windowPosition->x, s.windowPosition->y};
    }
    json["display"] = display;
    json["graphics"] = {{"brightness", s.brightness}, {"shadows", s.shadows}, {"fillLight", s.fillLight},
                        {"bloom", s.bloom}, {"particles", s.particles}, {"lightBands", s.lightBands}};
    Json controls = Json::object();
    for (const auto& [action, binding] : s.bindings) {
        Json keys = Json::array();
        for (const platform::Key key : binding.keys) {
            keys.push_back(std::string(platform::keyName(key)));
        }
        Json buttons = Json::array();
        for (const platform::GamepadButton button : binding.buttons) {
            buttons.push_back(std::string(platform::gamepadButtonName(button)));
        }
        controls[action] = {{"keys", keys}, {"buttons", buttons}};
    }
    json["controls"] = controls;
    return json.dump(2) + "\n";
}

std::optional<Settings> parseSettings(std::string_view text, std::string* error) {
    auto fail = [error](const std::string& message) -> std::optional<Settings> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    Json json;
    try {
        json = Json::parse(text.begin(), text.end());
    } catch (const Json::parse_error& e) {
        return fail(std::string("invalid JSON: ") + e.what());
    }
    if (!json.is_object() || json.value("format", "") != kFormat) {
        return fail("not an Animal Engine settings file");
    }
    const int version = json.value("version", 0);
    if (version < 1 || version > Settings::kVersion) {
        return fail("settings version " + std::to_string(version) + " is not supported");
    }
    Settings s;
    if (const auto audio = json.find("audio"); audio != json.end() && audio->is_object()) {
        s.masterVolume = volume(*audio, "master", s.masterVolume);
        s.effectsVolume = volume(*audio, "effects", s.effectsVolume);
        s.ambienceVolume = volume(*audio, "ambience", s.ambienceVolume);
        s.musicVolume = volume(*audio, "music", s.musicVolume);
    }
    if (const auto display = json.find("display"); display != json.end() && display->is_object()) {
        s.fullscreen = display->value("fullscreen", s.fullscreen);
        s.vsync = display->value("vsync", s.vsync);
        const int cap = display->value("frameCap", 0);
        s.frameCap = std::ranges::find(Settings::kFrameCaps, cap) != std::end(Settings::kFrameCaps) ? cap : 0;
        const auto pair = [&](const char* name) -> std::optional<Vec2i> {
            const auto it = display->find(name);
            if (it == display->end() || !it->is_array() || it->size() != 2 || !(*it)[0].is_number_integer() ||
                !(*it)[1].is_number_integer()) {
                return std::nullopt;
            }
            return Vec2i{(*it)[0].get<int>(), (*it)[1].get<int>()};
        };
        if (const auto size = pair("windowSize"); size && size->x >= 320 && size->y >= 180) {
            s.windowSize = *size;
        }
        s.windowPosition = pair("windowPosition");
    }
    if (const auto graphics = json.find("graphics"); graphics != json.end() && graphics->is_object()) {
        const auto flag = [&](const char* name, bool fallback) {
            const auto it = graphics->find(name);
            return it != graphics->end() && it->is_boolean() ? it->get<bool>() : fallback;
        };
        if (const auto it = graphics->find("brightness"); it != graphics->end() && it->is_number()) {
            const float v = it->get<float>();
            if (std::isfinite(v)) {
                s.brightness = std::clamp(v, Settings::kMinBrightness, Settings::kMaxBrightness);
            }
        }
        s.shadows = flag("shadows", s.shadows);
        s.fillLight = flag("fillLight", s.fillLight);
        s.bloom = flag("bloom", s.bloom);
        s.particles = flag("particles", s.particles);
        s.lightBands = flag("lightBands", s.lightBands);
    }
    if (const auto controls = json.find("controls"); controls != json.end() && controls->is_object()) {
        for (const auto& [action, entry] : controls->items()) {
            if (!entry.is_object()) {
                continue;
            }
            InputBinding binding;
            for (const Json& name : entry.value("keys", Json::array())) {
                if (const auto key = name.is_string() ? platform::keyFromName(name.get<std::string>()) : std::nullopt) {
                    binding.keys.push_back(*key);
                }
            }
            for (const Json& name : entry.value("buttons", Json::array())) {
                if (const auto button =
                        name.is_string() ? platform::gamepadButtonFromName(name.get<std::string>()) : std::nullopt) {
                    binding.buttons.push_back(*button);
                }
            }
            if (!binding.keys.empty() || !binding.buttons.empty()) {
                s.bindings[action] = std::move(binding);
            }
        }
    }
    return s;
}

Settings loadSettings(const std::string& path) {
    const auto bytes = readFile(path);
    if (!bytes) {
        return {};
    }
    std::string error;
    const auto settings =
        parseSettings(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), &error);
    if (!settings) {
        log::warn("{}: {}; using the default settings", path, error);
        return {};
    }
    return *settings;
}

bool saveSettings(const std::string& path, const Settings& settings) {
    const std::string text = serializeSettings(settings);
    return writeFileAtomic(path, text.data(), text.size());
}

} // namespace animal
