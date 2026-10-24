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

constexpr float kHeartFullCell = 10.0f; // cells of the props material
constexpr float kHeartEmptyCell = 11.0f;
constexpr animal::Vec2 kHeartsOffset{3.0f, 3.0f}; // canvas pixels from the top-left corner
constexpr float kHeartSpacing = 9.0f;
// Quarters of the 8×8 heart cell, in the order they are lost from the end.
constexpr animal::RectF kHeartQuarters[4] = {{0, 0, 4, 3}, {4, 0, 4, 3}, {4, 3, 4, 5}, {0, 3, 4, 5}};
constexpr animal::Vec3 kHeartGlow{1.0f, 0.3f, 0.36f};

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
        showMessage(text("message.newer_save"));
        return;
    case animal::SaveLoad::Status::Unusable:
        animal::log::error("{}", load.message);
        showMessage(text("message.save_damaged"));
        return;
    case animal::SaveLoad::Status::Recovered:
        animal::log::warn("{}", load.message);
        showMessage(text("message.save_restored"));
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
    dynamite_ = 0;
    health_ = kMaxHealth;
    save_.flags.clear();
    save_.visited.clear();
    save_.playSeconds = 0.0;
    save_.room = roomName(startRoom_->path);
    save_.position = startFeet_;
    checkpointRoom_ = startRoom_;
    checkpointFeet_ = startFeet_;
    respawn(renderer);
    showMessage(text("message.progress_reset"));
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

// Hearts in the top-left corner of the canvas, drawn into the world like the props: lit, glowing
// (emissive) and graded with the scene, and lighting the corner a little themselves. A heart loses
// its quarters top-left, top-right, bottom-right, bottom-left.
void SandboxGame::drawHearts(animal::Renderer2D& renderer) const {
    const animal::Vec2 corner = camera_.topLeft(animal::Renderer2D::kCanvasExtent) + kHeartsOffset;
    const float size = static_cast<float>(kTileSize);
    const int hearts = kMaxHealth / kQuartersPerHeart;
    for (int heart = 0; heart < hearts; ++heart) {
        const int quarters = std::clamp(health_ - heart * kQuartersPerHeart, 0, kQuartersPerHeart);
        const animal::Vec2 at{corner.x + static_cast<float>(heart) * kHeartSpacing, corner.y};
        renderer.drawSprite(assets_.props, at, {kHeartEmptyCell * size, 0.0f, size, size});
        for (int quarter = 0; quarter < quarters; ++quarter) {
            const animal::RectF& q = kHeartQuarters[quarter];
            renderer.drawSprite(assets_.props, {at.x + q.x, at.y + q.y}, {kHeartFullCell * size + q.x, q.y, q.width, q.height});
        }
    }
    animal::PointLight glow;
    glow.position = corner + animal::Vec2{static_cast<float>(hearts) * kHeartSpacing * 0.5f, 3.0f};
    glow.radius = 30.0f;
    glow.color = kHeartGlow;
    glow.intensity = 0.15f + 0.35f * static_cast<float>(health_) / static_cast<float>(kMaxHealth);
    glow.height = 6.0f;
    glow.castsShadows = false;
    renderer.addLight(glow);
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
    for (const std::string& message : touch.messages) {
        showMessage(levelText(message));
    }
    if (touch.pressed) {
        playSound(sounds_.click, x);
    }
    if (touch.ability) {
        playSound(sounds_.ability, x);
        if (touch.messages.empty()) {
            showMessage(text("message.dive"));
        }
    }
    if (touch.gateNeeds) {
        showMessage(text("message.gate", *touch.gateNeeds, collectedGems()));
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
    const float line = renderer.textLineHeight(font_);
    const auto centred = [&](const std::string& s, float y, animal::Color color, uint32_t size = 1) {
        renderer.drawText(font_, {canvas.x + (canvas.width - renderer.textWidth(font_, s, size)) * 0.5f, y}, s, color,
                          size);
    };
    const int seconds = static_cast<int>(save_.playSeconds);
    float y = canvas.y + canvas.height * 0.25f;
    centred(text("ending.title"), y, kEndTitle, 2);
    y += line * 3.0f;
    centred(text("ending.gems", collectedGems(), gemFlags_.size()), y, kEndText);
    y += line * 1.25f;
    centred(text("ending.rooms", save_.visited.size(), world_.rooms.size()), y, kEndText);
    y += line * 1.25f;
    centred(text("ending.time", std::format("{}:{:02}", seconds / 60, seconds % 60)), y, kEndText);
    centred(text("ending.continue", prompt(Jump)), canvas.y + canvas.height - line * 2.0f, kEndHint);
}

} // namespace game
