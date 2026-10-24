// Game sounds: positional effects, and the rustle and swish of vines (ADR-0033, ADR-0038).

#include "sandbox_game.h"

#include <algorithm>
#include <cmath>

namespace game {

namespace {

constexpr animal::Vec2 kScreenCentre{160.0f, 88.0f};
constexpr float kClimbRustleDistance = 7.0f; // px climbed between rustles
constexpr float kSwingSpeed = 70.0f;         // px/s: a swing faster than this swishes
constexpr float kSwingCooldown = 0.45f;      // s between swishes

} // namespace

void SandboxGame::playSound(animal::audio::SoundHandle sound, float x, float volume) {
    if (!audio_) {
        return;
    }
    const float pan = std::clamp((x - kScreenCentre.x) / kScreenCentre.x * 0.7f, -0.7f, 0.7f);
    audio_->play(sound, {volume, pan, soundRandom_.range(0.95f, 1.05f), false, animal::audio::Bus::Effects, 0.0f});
}

void SandboxGame::vineSounds(const animal::Entity& player, bool climbing, float dt) {
    const animal::Vec2 position = player.body.exactPosition();
    const animal::Vec2 delta = position - vineLastPosition_;
    vineLastPosition_ = position;
    vineSwingCooldown_ = std::max(0.0f, vineSwingCooldown_ - dt);
    const bool grabbedNow = climbing && !vineWasClimbing_;
    vineWasClimbing_ = climbing;
    if (!climbing || grabbedNow) { // the first step on a vine snaps to it: not a swing
        vineClimbDistance_ = 0.0f;
        vineLastSpeed_ = climbing ? kSwingSpeed : 0.0f;
        return;
    }
    const float x = position.x + 3.0f;
    const float speed = std::hypot(delta.x, delta.y) / dt;
    if (climb_.movedThisStep) {
        vineClimbDistance_ += std::hypot(delta.x, delta.y);
        if (vineClimbDistance_ >= kClimbRustleDistance) {
            vineClimbDistance_ = 0.0f;
            playSound(sounds_.vineClimb, x, 0.45f);
        }
    }
    if (speed > kSwingSpeed && vineLastSpeed_ <= kSwingSpeed && vineSwingCooldown_ <= 0.0f) {
        playSound(sounds_.vineSwing, x, std::clamp(speed / 160.0f, 0.3f, 0.9f));
        vineSwingCooldown_ = kSwingCooldown;
    }
    vineLastSpeed_ = speed;
}

} // namespace game
