// The player's settings (ADR-0036): loaded at start-up, applied to the window, the device, the frame
// cap and the audio, and saved when they change and on exit (with the window's placement).

#include "sandbox_game.h"

#include <animal/core/log.h>
#include <animal/platform/platform.h>
#include <animal/platform/window.h>
#include <animal/rhi/rhi.h>

namespace game {

void SandboxGame::loadSettings(animal::EngineContext& context) {
    const bool testRun = context.demo || context.deterministic || context.customData || !startRoomName_.empty();
    if (settingsPath_.empty() && !testRun) {
        if (const std::string dir = animal::platform::userDataDirectory("AnimalEngine", "Sandbox"); !dir.empty()) {
            settingsPath_ = dir + "settings.json";
        }
    }
    if (!settingsPath_.empty()) {
        settings_ = animal::loadSettings(settingsPath_);
    }
    savedSettings_ = settings_;
    if (context.window) {
        if (settings_.windowSize.x > 0 && settings_.windowSize.y > 0) {
            const animal::platform::WindowRect current = context.window->placement();
            context.window->setPlacement({settings_.windowPosition ? settings_.windowPosition->x : current.x,
                                          settings_.windowPosition ? settings_.windowPosition->y : current.y,
                                          settings_.windowSize.x, settings_.windowSize.y});
        }
        if (settings_.fullscreen) {
            context.window->setFullscreen(true);
        }
    }
    applyDisplaySettings(context);
}

void SandboxGame::applyDisplaySettings(animal::EngineContext& context) {
    context.device->setVsync(settings_.vsync);
    context.frameCap = settings_.vsync ? 0 : settings_.frameCap;
    if (context.window && context.window->fullscreen() != settings_.fullscreen) {
        context.window->setFullscreen(settings_.fullscreen);
    }
}

// The player's volumes scale the mix of the tuning table (audio.*).
void SandboxGame::applyVolumes() {
    if (!audio_) {
        return;
    }
    audio_->setMasterVolume(settings_.masterVolume);
    audio_->setBusVolume(animal::audio::Bus::Effects, busVolumes_[0] * settings_.effectsVolume);
    audio_->setBusVolume(animal::audio::Bus::Ambience, busVolumes_[1] * settings_.ambienceVolume);
    audio_->setBusVolume(animal::audio::Bus::Music, busVolumes_[2] * settings_.musicVolume);
}

void SandboxGame::saveSettings(animal::EngineContext& context) {
    if (context.window) {
        const animal::platform::WindowRect windowed = context.window->placement();
        if (windowed.width > 0 && windowed.height > 0) {
            settings_.windowSize = {windowed.width, windowed.height};
            settings_.windowPosition = animal::Vec2i{windowed.x, windowed.y};
        }
    }
    if (settingsPath_.empty() || settings_ == savedSettings_) {
        return; // unchanged: the file stays as it is
    }
    if (animal::saveSettings(settingsPath_, settings_)) {
        savedSettings_ = settings_;
    } else {
        animal::log::warn("Could not write the settings file {}", settingsPath_);
    }
}

} // namespace game
