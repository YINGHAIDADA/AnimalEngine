// Player settings (ADR-0036).

#include <animal/core/file.h>
#include <animal/framework/settings.h>

#include "test_harness.h"

#include <filesystem>
#include <string>

namespace {

using namespace animal;
using platform::GamepadButton;
using platform::Key;

TEST(settings_round_trip) {
    Settings s;
    s.masterVolume = 0.5f;
    s.musicVolume = 0.0f;
    s.fullscreen = true;
    s.windowSize = {960, 540};
    s.windowPosition = Vec2i{-1200, 40}; // a second display to the left
    s.vsync = false;
    s.frameCap = 144;
    s.brightness = 1.3f;
    s.shadows = false;
    s.fillLight = false;
    s.bloom = false;
    s.particles = false;
    s.lightBands = false;
    s.bindings["jump"] = {{Key::C, Key::Space}, {GamepadButton::East}};
    const auto back = parseSettings(serializeSettings(s));
    CHECK(back.has_value() && *back == s);
    CHECK(parseSettings(serializeSettings(Settings{})) == Settings{});
}

TEST(settings_clamp_and_drop_what_they_cannot_use) {
    const auto s = parseSettings(R"({"format": "animal-settings", "version": 1,
        "audio": {"master": 3.0, "effects": -1, "music": "loud"},
        "display": {"frameCap": 75, "windowSize": [100, 100], "windowPosition": [1, "x"]},
        "graphics": {"brightness": 9, "shadows": "no", "bloom": false},
        "controls": {"jump": {"keys": ["Space", "NoSuchKey"], "buttons": ["South", 7]}, "empty": {"keys": []}}})");
    CHECK(s.has_value());
    CHECK(s->masterVolume == 1.0f && s->effectsVolume == 0.0f && s->musicVolume == 1.0f);
    CHECK(s->frameCap == 0);                       // not one of the choices
    CHECK(s->windowSize == Vec2i{} && !s->windowPosition);
    CHECK(s->brightness == Settings::kMaxBrightness);
    CHECK(s->shadows && !s->bloom && s->fillLight); // a mistyped switch keeps its default
    const InputBinding jump{{Key::Space}, {GamepadButton::South}};
    CHECK(s->bindings.size() == 1 && s->bindings.at("jump") == jump);

    std::string error;
    CHECK(!parseSettings("{nope", &error) && error.find("JSON") != std::string::npos);
    CHECK(!parseSettings(R"({"format": "animal-save", "version": 1})", &error));
    CHECK(!parseSettings(R"({"format": "animal-settings", "version": 99})", &error) &&
          error.find("version") != std::string::npos);
}

TEST(settings_load_falls_back_to_the_defaults) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "animal_settings_test";
    fs::create_directories(dir);
    const std::string path = (dir / "settings.json").generic_string();
    std::error_code ec;
    fs::remove(path, ec);
    CHECK(loadSettings(path) == Settings{}); // missing
    Settings s;
    s.effectsVolume = 0.25f;
    CHECK(saveSettings(path, s) && loadSettings(path) == s);
    CHECK(writeFileAtomic(path, "garbage", 7));
    CHECK(loadSettings(path) == Settings{}); // damaged
    fs::remove_all(dir, ec);
}

TEST(gamepad_button_names_round_trip) {
    for (int i = 0; i < static_cast<int>(GamepadButton::Count); ++i) {
        const auto button = static_cast<GamepadButton>(i);
        CHECK(platform::gamepadButtonFromName(platform::gamepadButtonName(button)) == button);
    }
    CHECK(!platform::gamepadButtonFromName("Turbo"));
}

} // namespace
