#pragma once

// The player's settings (ADR-0036): volumes, display and controls, in "<user dir>/settings.json".
// Versioned JSON written atomically; values out of range are clamped and unknown inputs dropped, so
// a hand-edited or damaged file never stops the game (it falls back to the defaults).

#include <animal/framework/geometry.h>
#include <animal/platform/input.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace animal {

// The keys and gamepad buttons of one action (the analog stick stays bound as the game defines it).
struct InputBinding {
    std::vector<platform::Key> keys;
    std::vector<platform::GamepadButton> buttons;

    friend bool operator==(const InputBinding&, const InputBinding&) = default;
};

struct Settings {
    static constexpr int kVersion = 1;
    static constexpr int kFrameCaps[] = {0, 30, 60, 120, 144}; // 0 = unlimited

    // Audio, 0..1.
    float masterVolume = 1.0f;
    float effectsVolume = 1.0f;
    float ambienceVolume = 1.0f;
    float musicVolume = 1.0f;

    // Display.
    bool fullscreen = false;
    Vec2i windowSize;                  // points; {0, 0}: chosen from the display
    std::optional<Vec2i> windowPosition; // remembered; none: centred
    bool vsync = true;
    int frameCap = 0;                  // frames per second without vsync (one of kFrameCaps)

    // Controls: only the actions the player rebound, by the game's action names.
    std::map<std::string, InputBinding> bindings;

    friend bool operator==(const Settings&, const Settings&) = default;
};

std::string serializeSettings(const Settings& settings);
// Nullopt (with the reason) for text that is not a settings file of a supported version.
std::optional<Settings> parseSettings(std::string_view text, std::string* error = nullptr);
// The settings at `path`, or the defaults when the file is missing or unusable (the reason is logged).
Settings loadSettings(const std::string& path);
bool saveSettings(const std::string& path, const Settings& settings);

} // namespace animal
