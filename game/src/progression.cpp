// Progress through the area (ADR-0034): hazards and crushing, switches and doors, the dive ability,
// currents, the goal and the ending screen.

#include "sandbox_game.h"

#include <animal/framework/water.h>
#include <animal/renderer/renderer2d.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace game {

namespace {

constexpr animal::Color kEndShade = animal::Color::rgba(4, 4, 10, 225);
constexpr animal::Color kEndTitle = animal::Color::rgba(150, 240, 215);
constexpr animal::Color kEndText = animal::Color::rgba(235, 228, 210);
constexpr animal::Color kEndHint = animal::Color::rgba(120, 130, 140);
constexpr const char* kDiveMessage = "YOU CAN DIVE: HOLD DOWN IN WATER";

} // namespace

size_t SandboxGame::collectedGems() const {
    return static_cast<size_t>(
        std::ranges::count_if(save_.flags, [this](const std::string& flag) { return gemFlags_.contains(flag); }));
}

bool SandboxGame::canDive() const {
    return save_.flags.contains(Mechanisms::abilityFlag("dive"));
}

void SandboxGame::stepPlatforms(animal::Entity& player, float dt) {
    const animal::PlatformSystem::StepResult result = cave_.stepPlatforms(dt, &player.body);
    if (cave_.mechanisms().startedShaking() > 0) {
        playSound(sounds_.crumble, player.body.exactPosition().x, 0.6f);
    }
    if (result.crushed) {
        hurt(player);
    }
}

// Diving with the ability: holding down in the water goes under; rising to the surface (not holding
// down) floats again. Returns false when the player is not diving this step.
bool SandboxGame::stepDive(animal::Entity& player, float moveX, float moveY, float dt) {
    const float submersion = cave_.submersion(player.body.worldCollider());
    if (!canDive() || submersion <= 0.0f) {
        diving_ = false;
    } else if (!diving_ && submersion >= diveParams_.enterDepth && moveY > 0.5f) {
        diving_ = true;
    } else if (diving_ && submersion < diveParams_.leaveDepth && moveY <= 0.0f) {
        diving_ = false;
    }
    if (!diving_) {
        return false;
    }
    animal::stepDive(player.body, playerState_, {moveX, moveY}, diveParams_, cave_.tiles(), dt);
    return true;
}

void SandboxGame::applyCurrents(animal::Entity& player, float dt) {
    const animal::IntRect collider = player.body.worldCollider();
    const animal::Vec2 current = cave_.currentAt(collider);
    if (current.x != 0.0f || current.y != 0.0f) {
        animal::applyCurrent(player.body, current, cave_.submersion(collider), cave_.tiles(), dt);
    }
}

void SandboxGame::hurt(animal::Entity& player) {
    if (respawnPending_) {
        return;
    }
    playSound(sounds_.hit, player.body.exactPosition().x);
    respawnPending_ = true;
}

// After the player moved: spikes, switches, abilities, gates, doors and the goal.
void SandboxGame::updateMechanisms(animal::Entity& player, float dt) {
    const animal::IntRect collider = player.body.worldCollider();
    const float x = static_cast<float>(collider.x) + collider.width * 0.5f;
    if (cave_.tiles().overlapsHazard(collider)) {
        hurt(player);
    }
    Mechanisms::Touch touch = cave_.touchMechanisms(collider);
    save_.flags.insert(touch.flags.begin(), touch.flags.end());
    for (std::string& message : touch.messages) {
        showMessage(std::move(message));
    }
    if (touch.pressed) {
        playSound(sounds_.click, x);
    }
    if (touch.ability) {
        playSound(sounds_.ability, x);
        if (touch.messages.empty()) {
            showMessage(kDiveMessage);
        }
    }
    if (touch.gateNeeds) {
        showMessage(std::format("THE GATE NEEDS {} GEMS ({} FOUND)", *touch.gateNeeds, collectedGems()));
    }
    if (!touch.flags.empty()) {
        saveProgress();
    }
    if (cave_.updateMechanisms(dt, save_.flags, collectedGems()) > 0) {
        playSound(sounds_.door, x);
    }
    if (touch.goal && !save_.flags.contains(kFinishedFlag)) {
        save_.flags.insert(kFinishedFlag);
        endingOpen_ = true;
        playSound(sounds_.ability, x);
        saveProgress();
    }
}

void SandboxGame::drawEnding(animal::Renderer2D& renderer) {
    const animal::PresentLayout& screen = renderer.presentLayout();
    const animal::RectF canvas{static_cast<float>(screen.x), static_cast<float>(screen.y),
                               static_cast<float>(screen.width), static_cast<float>(screen.height)};
    renderer.fillRect(canvas, kEndShade);
    const float line = renderer.textLineHeight();
    const auto centred = [&](const std::string& text, float y, animal::Color color) {
        renderer.drawText({canvas.x + (canvas.width - renderer.textWidth(text)) * 0.5f, y}, text, color);
    };
    const int seconds = static_cast<int>(save_.playSeconds);
    float y = canvas.y + canvas.height * 0.3f;
    centred("THE END", y, kEndTitle);
    y += line * 2.5f;
    centred(std::format("GEMS {}/{}", collectedGems(), gemFlags_.size()), y, kEndText);
    y += line * 1.5f;
    centred(std::format("ROOMS {}/{}", save_.visited.size(), world_.rooms.size()), y, kEndText);
    y += line * 1.5f;
    centred(std::format("TIME {}:{:02}", seconds / 60, seconds % 60), y, kEndText);
    centred(usingGamepad_ ? "A CONTINUE" : "SPACE CONTINUE", canvas.y + canvas.height - line * 3.0f, kEndHint);
}

} // namespace game
