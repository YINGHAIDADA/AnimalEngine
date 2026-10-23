// Progress through the area: the save (ADR-0011, ADR-0032); hazards and crushing, switches and doors,
// the dive ability, currents, the goal and the ending screen (ADR-0034).

#include "sandbox_game.h"

#include <animal/core/file.h>
#include <animal/core/log.h>
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

constexpr int kHeartWidth = 9;
constexpr int kHeartHeight = 8;
constexpr const char* kHeart[kHeartHeight] = {
    ".oo...oo.",
    "ohfo.offo",
    "offfofffo",
    "offfffffo",
    ".offfffo.",
    "..offfo..",
    "...ofo...",
    "....o....",
};
constexpr animal::Color kHeartOutline = animal::Color::rgba(24, 12, 22);
constexpr animal::Color kHeartFill = animal::Color::rgba(232, 52, 72);
constexpr animal::Color kHeartShine = animal::Color::rgba(255, 196, 204);
constexpr animal::Color kHeartEmpty = animal::Color::rgba(70, 34, 50);

} // namespace

// Resumes at the saved checkpoint with the saved gems collected; a save naming a room that is
// no longer in the world keeps its gems but starts at the spawn point.
void SandboxGame::loadProgress() {
    animal::SaveLoad load = animal::loadSave(savePath_);
    switch (load.status) {
    case animal::SaveLoad::Status::Missing:
        return;
    case animal::SaveLoad::Status::TooNew:
        animal::log::error("{}", load.message);
        savePath_.clear(); // never overwrite progress from a newer version
        showMessage("SAVE FROM A NEWER VERSION - NOT SAVING");
        return;
    case animal::SaveLoad::Status::Unusable:
        animal::log::error("{}", load.message);
        showMessage("SAVE DAMAGED - NEW GAME");
        return;
    case animal::SaveLoad::Status::Recovered:
        animal::log::warn("{}", load.message);
        showMessage("SAVE RESTORED FROM BACKUP");
        break;
    case animal::SaveLoad::Status::Loaded:
        if (!load.message.empty()) {
            animal::log::info("{}", load.message);
        }
        break;
    }
    const animal::SaveData* loaded = &*load.data;
    save_.flags = loaded->flags;
    save_.visited = loaded->visited;
    save_.playSeconds = loaded->playSeconds;
    for (const animal::WorldRoom& worldRoom : world_.rooms) {
        if (roomName(worldRoom.path) == loaded->room) {
            checkpointRoom_ = &worldRoom;
            checkpointFeet_ = loaded->position;
            save_.room = loaded->room;
            save_.position = loaded->position;
            animal::log::info("Loaded save: room '{}', {} gem(s)", save_.room, save_.flags.size());
            return;
        }
    }
    animal::log::warn("Save names unknown room '{}'; starting at the spawn point", loaded->room);
}

void SandboxGame::saveProgress() {
    if (!savePath_.empty() && !animal::writeSave(savePath_, save_)) {
        animal::log::warn("Could not write the save file {}", savePath_);
    }
}

// F8: forget all progress and start over from the spawn point.
void SandboxGame::resetProgress(animal::Renderer2D& renderer) {
    if (!savePath_.empty()) {
        for (const std::string& file : {savePath_, savePath_ + ".bak"}) { // a backup would bring it back
            if (animal::fileExists(file)) {
                animal::deleteFile(file);
            }
        }
    }
    health_ = kMaxHealth;
    save_.flags.clear();
    save_.visited.clear();
    save_.playSeconds = 0.0;
    save_.room = roomName(startRoom_->path);
    save_.position = startFeet_;
    checkpointRoom_ = startRoom_;
    checkpointFeet_ = startFeet_;
    respawn(renderer);
    showMessage("PROGRESS RESET");
}

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
    takeDamage();
    respawnPending_ = true;
}

void SandboxGame::takeDamage() {
    health_ = std::max(0, health_ - 1);
    if (health_ == 0) {
        respawnPending_ = true;
    }
}

// Hearts in the top-left corner, one HUD pixel per heart pixel; a heart loses its quarters
// top-left, top-right, bottom-right, bottom-left.
void SandboxGame::drawHearts(animal::Renderer2D& renderer) const {
    const animal::PresentLayout& screen = renderer.presentLayout();
    const float u = static_cast<float>(renderer.overlayScale());
    const float margin = 6.0f * u;
    for (int heart = 0; heart < kMaxHealth / kQuartersPerHeart; ++heart) {
        const int quarters = std::clamp(health_ - heart * kQuartersPerHeart, 0, kQuartersPerHeart);
        const float left = static_cast<float>(screen.x) + margin + static_cast<float>(heart) * 10.0f * u;
        const float top = static_cast<float>(screen.y) + margin;
        for (int y = 0; y < kHeartHeight; ++y) {
            for (int x = 0; x < kHeartWidth; ++x) {
                const char c = kHeart[y][x];
                if (c == '.') {
                    continue;
                }
                const bool rightHalf = x > kHeartWidth / 2;
                const bool bottomHalf = y >= kHeartHeight / 2;
                const int quarter = bottomHalf ? (rightHalf ? 2 : 3) : (rightHalf ? 1 : 0);
                animal::Color color = kHeartOutline;
                if (c != 'o') {
                    color = quarter < quarters ? (c == 'h' ? kHeartShine : kHeartFill) : kHeartEmpty;
                }
                renderer.fillRect({left + static_cast<float>(x) * u, top + static_cast<float>(y) * u, u, u}, color);
            }
        }
    }
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
