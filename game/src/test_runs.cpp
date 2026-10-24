// Automated runs (ADR-0032, ADR-0034): command-line options for tests, the scripted demo input, and
// the expectations checked when a run ends.

#include "sandbox_game.h"

#include <animal/core/log.h>

#include <algorithm>
#include <format>
#include <cstdlib>
#include <string>

namespace game {

bool SandboxGame::onArgument(std::string_view argument) {
    constexpr std::string_view kStress = "--stress-lights=";
    if (argument.starts_with(kStress)) {
        stressLights_ = static_cast<uint32_t>(std::strtoul(std::string(argument.substr(kStress.size())).c_str(), nullptr, 10));
        return true;
    }
    constexpr std::string_view kStartRoom = "--start-room=";
    if (argument.starts_with(kStartRoom)) {
        startRoomName_ = std::string(argument.substr(kStartRoom.size()));
        return true;
    }
    constexpr std::string_view kExpect = "--expect=";
    if (argument.starts_with(kExpect)) {
        std::string_view list = argument.substr(kExpect.size());
        while (!list.empty()) {
            const std::string_view item = list.substr(0, list.find(','));
            const size_t colon = item.find(':');
            if (colon == std::string_view::npos) {
                animal::log::error("--expect: '{}' is not name:value", item);
                return false;
            }
            expectations_.emplace_back(std::string(item.substr(0, colon)), std::string(item.substr(colon + 1)));
            list = item.size() < list.size() ? list.substr(item.size() + 1) : std::string_view();
        }
        return true;
    }
    // Test runs (with --start-room): progress to begin with, and a trace of the player.
    constexpr std::string_view kFlags = "--flags=";
    if (argument.starts_with(kFlags)) {
        std::string_view list = argument.substr(kFlags.size());
        while (!list.empty()) {
            const std::string_view item = list.substr(0, list.find(','));
            testFlags_.emplace(item);
            list = item.size() < list.size() ? list.substr(item.size() + 1) : std::string_view();
        }
        return true;
    }
    constexpr std::string_view kGems = "--gems=";
    if (argument.starts_with(kGems)) {
        testGems_ = std::strtoul(std::string(argument.substr(kGems.size())).c_str(), nullptr, 10);
        return true;
    }
    constexpr std::string_view kSettings = "--settings=";
    if (argument.starts_with(kSettings)) {
        settingsPath_ = std::string(argument.substr(kSettings.size()));
        return true;
    }
    if (argument == "--title") {
        forceTitle_ = true; // test runs skip the title screen otherwise
        return true;
    }
    if (argument == "--menus") {
        forceMenus_ = true; // the pause menu and the way back to the title, starting in the game
        return true;
    }
    if (argument == "--trace" || argument.starts_with("--trace=")) {
        traceEvery_ = argument.size() > 8 ? std::max(1ul, std::strtoul(std::string(argument.substr(8)).c_str(), nullptr, 10)) : 10;
        return true;
    }
    return dev_.handleArgument(argument);
}

// --demo: a short tour driven by simulation time (reproducible with --deterministic): run right,
// grab the long vine in the entrance and climb a little while swinging, jump off, continue
// along the tunnel into the crystal gallery, and drop through its pit into the smoke pit.
void SandboxGame::applyDemoInput() {
    actions_.applyStrength(MoveRight, time_ < 7.5 ? 1.0f : 0.0f);
    actions_.applyStrength(MoveLeft, 0.0f);
    actions_.applyStrength(MoveUp, time_ >= 0.5 && time_ < 1.3 ? 1.0f : 0.0f);
    actions_.applyStrength(MoveDown, 0.0f);
    actions_.applyStrength(Jump, time_ >= 1.35 && time_ < 1.55 ? 1.0f : 0.0f);
}

// Expectations: room (the room the player ends in), gems (at least N collected), visited (at least N
// rooms entered), flag (a save flag is set: switch:NAME, ability:NAME), finished (the goal was
// reached), health (quarter hearts left), grounded (true / false), respawns (how often the player was
// sent back to a checkpoint).
bool SandboxGame::checkExpectations() {
    const size_t collected = static_cast<size_t>(
        std::ranges::count_if(save_.flags, [this](const std::string& f) { return gemFlags_.count(f) != 0; }));
    bool ok = true;
    for (const auto& [name, expected] : expectations_) {
        std::string actual;
        bool met = false;
        if (name == "room") {
            actual = currentRoom_ ? roomName(currentRoom_->path) : "";
            met = actual == expected;
        } else if (name == "gems") {
            actual = std::to_string(collected);
            met = collected >= static_cast<size_t>(std::strtoul(expected.c_str(), nullptr, 10));
        } else if (name == "grounded") {
            actual = playerState_.grounded ? "true" : "false";
            met = actual == expected;
        } else if (name == "dynamite") {
            actual = std::to_string(dynamite_);
            met = actual == expected;
        } else if (name == "crawlers") {
            actual = std::to_string(cave_.aliveCrawlers());
            met = actual == expected;
        } else if (name == "lanternSwing") {
            actual = cave_.largestLanternSwing() > 0.05f ? "true" : "false"; // about 3 degrees
            met = actual == expected;
        } else if (name == "screen") {
            actual = screenName();
            met = actual == expected;
        } else if (name == "setting") {
            // setting:NAME:VALUE for masterVolume, effectsVolume, fullscreen, vsync, frameCap, language
            // (saved / shown, e.g. "en/en"), brightness,
            // shadows, fillLight, bloom, particles, lightBands, key.ACTION
            const size_t colon = expected.find(':');
            const std::string key = expected.substr(0, colon);
            const std::string want = colon == std::string::npos ? "" : expected.substr(colon + 1);
            if (key == "masterVolume") {
                actual = std::format("{:.1f}", settings_.masterVolume);
            } else if (key == "effectsVolume") {
                actual = std::format("{:.1f}", settings_.effectsVolume);
            } else if (key == "fullscreen") {
                actual = settings_.fullscreen ? "true" : "false";
            } else if (key == "vsync") {
                actual = settings_.vsync ? "true" : "false";
            } else if (key == "frameCap") {
                actual = std::to_string(settings_.frameCap);
            } else if (key == "language") {
                actual = settings_.language + "/" + strings_.language();
            } else if (key == "brightness") {
                actual = std::format("{:.1f}", settings_.brightness);
            } else if (key == "shadows" || key == "fillLight" || key == "bloom" || key == "particles" ||
                       key == "lightBands") {
                const bool value = key == "shadows" ? settings_.shadows
                                 : key == "fillLight" ? settings_.fillLight
                                 : key == "bloom" ? settings_.bloom
                                 : key == "particles" ? settings_.particles
                                                      : settings_.lightBands;
                actual = value ? "true" : "false";
            } else if (key.starts_with("key.")) {
                for (const ActionInfo& info : actionInfos()) {
                    if (key == std::string("key.") + info.name) {
                        const animal::ActionBinding& b = actions_.binding(info.id);
                        actual = b.keys.empty() ? "-" : std::string(animal::platform::keyName(b.keys.front()));
                    }
                }
            }
            met = actual == want;
            actual = key + ":" + actual;
        } else if (name == "health") {
            actual = std::to_string(health_);
            met = actual == expected;
        } else if (name == "flag") {
            met = save_.flags.contains(expected);
            actual = met ? expected : "(not set)";
        } else if (name == "finished") {
            actual = save_.flags.contains(kFinishedFlag) ? "true" : "false";
            met = actual == expected;
        } else if (name == "visited") {
            actual = std::to_string(save_.visited.size());
            met = save_.visited.size() >= static_cast<size_t>(std::strtoul(expected.c_str(), nullptr, 10));
        } else if (name == "respawns") {
            actual = std::to_string(respawns_);
            met = actual == expected;
        } else {
            animal::log::error("Expectation '{}' is unknown (room, gems, visited, flag, finished, health, dynamite, crawlers, lanternSwing, screen, setting, grounded, respawns)", name);
            ok = false;
            continue;
        }
        if (met) {
            animal::log::info("Expectation met: {} = {}", name, actual);
        } else {
            animal::log::error("Expectation FAILED: {} should be {}, is {}", name, expected, actual);
            ok = false;
        }
    }
    return ok;
}

} // namespace game
