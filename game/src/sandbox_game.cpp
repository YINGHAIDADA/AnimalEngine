#include "sandbox_game.h"

#include <animal/core/file.h>
#include <animal/core/log.h>
#include <animal/core/random.h>
#include <animal/platform/input.h>
#include <animal/platform/platform.h>
#include <animal/renderer/renderer2d.h>
#include <animal/rhi/rhi.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iterator>

namespace game {

namespace {

using animal::platform::GamepadAxis;
using animal::platform::GamepadButton;
using animal::platform::Key;

// The room (320×176) sits vertically centered in the 320×180 canvas (ADR-0006).
constexpr animal::Vec2 kCameraCenter{160.0f, 88.0f};

// Collider 6×7 inside the 8×8 sprite, leaving one transparent pixel column / row of slack.
constexpr animal::IntRect kPlayerCollider{0, 0, 6, 7};
constexpr animal::Vec2i kSpriteOffset{-1, -1};
constexpr int kFallMargin = 32; // px below a room with no neighbour before respawning

// F4 cycles through these light quantization settings: -1 = the room's look, 0 = smooth.
constexpr int kBandOverrides[] = {-1, 0, 3, 5, 8};
constexpr float kLookFadeSeconds = 0.6f; // grading crossfade when entering a room
constexpr animal::Color kBackdrop = animal::Color::rgba(12, 10, 20);
constexpr animal::Vec3 kPlayerGlowColor{0.85f, 0.85f, 1.0f};
constexpr uint32_t kRoomSeed = 0xA11CE;

constexpr animal::Vec2 kKnockback{120.0f, -140.0f};
constexpr float kInvulnerableSeconds = 1.0f;
constexpr float kStunSeconds = 0.25f;
constexpr float kMessageSeconds = 4.0f;
constexpr animal::Color kHudColor = animal::Color::rgba(150, 240, 215);
constexpr animal::Color kMessageColor = animal::Color::rgba(235, 228, 210);

std::string roomName(const std::string& path) {
    const std::u8string stem = std::filesystem::path(std::u8string(path.begin(), path.end())).stem().u8string();
    return {stem.begin(), stem.end()};
}

} // namespace

animal::AppConfig SandboxGame::configure() {
    animal::AppConfig config;
    config.title = "Animal Engine · M1b Sandbox";
    return config;
}

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
    return false;
}

// Performance testing: lights wandering over the whole room on fixed pseudo-random orbits.
void SandboxGame::addStressLights(animal::Renderer2D& renderer) const {
    for (uint32_t i = 0; i < stressLights_; ++i) {
        animal::Random rng(0x5EED0000u + i);
        const animal::Vec2 center{rng.range(16.0f, 304.0f), rng.range(16.0f, 160.0f)};
        const float orbit = rng.range(8.0f, 40.0f);
        const float speed = rng.range(0.3f, 1.2f) * (i % 2 == 0 ? 1.0f : -1.0f);
        const auto angle = static_cast<float>(time_) * speed + rng.range(0.0f, 2.0f * animal::kPi);
        animal::PointLight light;
        light.position = center + animal::Vec2{std::cos(angle), std::sin(angle)} * orbit;
        light.radius = rng.range(40.0f, 90.0f);
        light.color = {rng.range(0.3f, 1.0f), rng.range(0.3f, 1.0f), rng.range(0.3f, 1.0f)};
        light.intensity = 0.25f;
        renderer.addLight(light);
    }
}

void SandboxGame::bindActions() {
    actions_.bind(MoveLeft, {{Key::Left, Key::A}, {GamepadButton::DpadLeft}, {{GamepadAxis::LeftX, -1.0f}}});
    actions_.bind(MoveRight, {{Key::Right, Key::D}, {GamepadButton::DpadRight}, {{GamepadAxis::LeftX, 1.0f}}});
    actions_.bind(MoveUp, {{Key::Up, Key::W}, {GamepadButton::DpadUp}, {{GamepadAxis::LeftY, -1.0f}}});
    actions_.bind(MoveDown, {{Key::Down, Key::S}, {GamepadButton::DpadDown}, {{GamepadAxis::LeftY, 1.0f}}});
    actions_.bind(Jump, {{Key::Space, Key::Z}, {GamepadButton::South}, {}});
}

const animal::Room* SandboxGame::roomData(const animal::WorldRoom& worldRoom) {
    if (const auto it = rooms_.find(worldRoom.path); it != rooms_.end()) {
        return &it->second;
    }
    auto room = animal::loadRoom(worldRoom.path);
    if (!room) {
        return nullptr;
    }
    if (room->tiles.pixelBounds().width != worldRoom.rect.width ||
        room->tiles.pixelBounds().height != worldRoom.rect.height) {
        animal::log::error("{}: map size does not match its size in the world file", worldRoom.path);
        return nullptr;
    }
    return &rooms_.emplace(worldRoom.path, std::move(*room)).first->second;
}

bool SandboxGame::enterRoom(const animal::WorldRoom& worldRoom, animal::Renderer2D& renderer) {
    const animal::Room* room = roomData(worldRoom);
    if (!room || !cave_.build(*room, roomName(worldRoom.path), save_.flags, kRoomSeed + roomVisits_++)) {
        return false;
    }
    const bool firstRoom = currentRoom_ == nullptr;
    currentRoom_ = &worldRoom;
    checkpointPending_ = true;
    climb_.climbing = false; // vine indices belong to the previous room
    renderer.resetFluid();
    const animal::Vec2 canvasOrigin = camera_.topLeft(animal::Renderer2D::kCanvasExtent);
    renderer.setFluidObstacles(cave_.fluidObstacles(canvasOrigin));
    renderer.setLightOccluders(cave_.lightOccluders(canvasOrigin));
    lookTo_ = roomLook(worldRoom);
    lookFrom_ = firstRoom ? lookTo_ : currentLook(); // fade from whatever is on screen
    lookBlend_ = firstRoom ? 1.0f : 0.0f;
    animal::log::info("Entered room '{}'", roomName(worldRoom.path));
    return true;
}

void SandboxGame::placePlayerFeet(animal::Vec2 feet) {
    animal::Entity* player = entities_.get(player_);
    player->body.position = {static_cast<int>(std::lround(feet.x)) - kPlayerCollider.width / 2,
                             static_cast<int>(std::lround(feet.y)) - kPlayerCollider.height};
    player->body.remainder = {};
}

bool SandboxGame::onInit(animal::EngineContext& context) {
    const std::string textures = context.dataDirectory + "textures/";
    auto tileset = animal::loadMaterial(*context.device, textures + "tileset");
    auto props = animal::loadMaterial(*context.device, textures + "props");
    auto creature = animal::loadMaterial(*context.device, textures + "creature");
    auto crawler = animal::loadMaterial(*context.device, textures + "crawler");
    auto world = animal::loadWorld(context.dataDirectory + "world/test.world");
    if (!tileset || !props || !creature || !crawler || !world) {
        return false;
    }
    assets_ = {*tileset, *props, *crawler};
    creature_ = *creature;
    creature_.rim = 1.0f; // creatures catch rim light so they read against the dark
    assets_.crawler.rim = 1.0f;
    world_ = std::move(*world);

    // Start in the room that has a spawn point.
    for (const animal::WorldRoom& worldRoom : world_.rooms) {
        const animal::Room* room = roomData(worldRoom);
        if (!room) {
            return false;
        }
        for (const animal::LevelObject* spawn : room->objectsOfType("spawn")) {
            startRoom_ = &worldRoom;
            startFeet_ = spawn->position;
        }
        for (const animal::LevelObject* gem : room->objectsOfType("gem")) {
            gemFlags_.insert(CaveRoom::gemFlag(roomName(worldRoom.path), gem->id));
        }
    }
    if (!startRoom_) {
        animal::log::error("No room in the world has a 'spawn' object");
        return false;
    }
    checkpointRoom_ = startRoom_;
    checkpointFeet_ = startFeet_;
    save_.room = roomName(startRoom_->path);
    save_.position = startFeet_;
    if (!context.demo && !context.deterministic && startRoomName_.empty()) {
        if (const std::string dir = animal::platform::userDataDirectory("AnimalEngine", "Sandbox"); !dir.empty()) {
            savePath_ = dir + "save.json";
            loadProgress();
        }
    }
    if (!startRoomName_.empty()) {
        const auto it = std::ranges::find_if(world_.rooms, [this](const animal::WorldRoom& r) {
            return roomName(r.path) == startRoomName_;
        });
        if (it == world_.rooms.end()) {
            animal::log::error("--start-room: no room named '{}'", startRoomName_);
            return false;
        }
        checkpointRoom_ = &*it;
        checkpointFeet_ = {4.0f * kTileSize + 4.0f, 20.0f * kTileSize}; // every test room has floor there
    }

    camera_.center = kCameraCenter;
    player_ = entities_.create(animal::EntityKind::Player);
    entities_.get(player_)->body.collider = kPlayerCollider;
    if (!enterRoom(*checkpointRoom_, *context.renderer)) {
        return false;
    }
    placePlayerFeet(checkpointFeet_);
    bindActions();
    setupAnimations();

    animal::FluidSettings& fluid = context.renderer->fluid();
    fluid.smokeKeepPerSecond = 0.8f;
    fluid.velocityKeepPerSecond = 0.7f;
    fluid.buoyancy = 45.0f;
    return true;
}

// The room's look: the preset named by its "look" property (a file relative to the room) over
// the engine defaults, then its "look.*" property overrides.
animal::Look SandboxGame::roomLook(const animal::WorldRoom& worldRoom) {
    const animal::Room* room = roomData(worldRoom);
    animal::Look look = animal::Look::engineDefaults();
    if (!room) {
        return look;
    }
    if (const std::string file = room->properties.getString("look"); !file.empty()) {
        const std::string path = animal::resolvePath(room->path, file);
        auto it = looks_.find(path);
        if (it == looks_.end()) {
            it = looks_.emplace(path, animal::loadLook(path, look).value_or(look)).first;
        }
        look = it->second;
        lookName_ = roomName(path);
        if (lookName_.ends_with(".look")) {
            lookName_.resize(lookName_.size() - 5);
        }
    } else {
        lookName_ = "DEFAULT";
    }
    for (const std::string& problem : animal::applyLookOverrides(look, room->properties)) {
        animal::log::warn("{}: {}", room->path, problem);
    }
    return look;
}

animal::Look SandboxGame::currentLook() const {
    const float t = lookBlend_ * lookBlend_ * (3.0f - 2.0f * lookBlend_);
    return animal::mixLooks(lookFrom_, lookTo_, t);
}

void SandboxGame::applyCurrentLook(animal::Renderer2D& renderer) const {
    animal::applyLook(currentLook(), renderer.lighting(), renderer.post(), renderer.fluid());
    if (const int bands = kBandOverrides[bandsOverride_]; bands >= 0) {
        renderer.lighting().bands = bands;
    }
    renderer.lighting().shadows = renderer.lighting().shadows && shadowsEnabled_;
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

void SandboxGame::setupAnimations() {
    // Cells of creature.png: 0-1 idle (1 = blink), 2-5 run, 6 jump, 7 fall, 8-9 climb, 10 land.
    clips_.idle = animator_.add({{0, 0, 0, 0, 0, 1}, 3.0f, true});
    clips_.run = animator_.add({{2, 3, 4, 5}, 10.0f, true});
    clips_.jump = animator_.add({{6}, 1.0f, true});
    clips_.fall = animator_.add({{7}, 1.0f, true});
    clips_.climb = animator_.add({{8, 9}, 6.0f, true});
    clips_.land = animator_.add({{10}, 12.0f, false});
    animator_.play(clips_.idle);
}

// The animation state machine: the clip follows the movement state.
void SandboxGame::updateAnimation(float dt) {
    const animal::PlatformerState& s = playerState_;
    if (climb_.climbing) {
        animator_.play(clips_.climb);
    } else if (s.landedThisStep) {
        animator_.restart(clips_.land);
    } else if (animator_.current() == clips_.land && !animator_.finished()) {
        // let the squash finish
    } else if (s.grounded) {
        animator_.play(std::fabs(s.velocity.x) > 10.0f ? clips_.run : clips_.idle);
    } else {
        animator_.play(s.velocity.y < 0.0f ? clips_.jump : clips_.fall);
    }
    // On a vine the climb cycle only plays while moving along it.
    animator_.update(dt, climb_.climbing && !climb_.movedThisStep ? 0.0f : 1.0f);
}

void SandboxGame::onFrame(animal::EngineContext& context) {
    const animal::platform::Input& input = *context.input;
    if (context.demo) {
        applyDemoInput();
    } else {
        actions_.update(input);
    }
    animal::Renderer2D& renderer = *context.renderer;
    if (input.pressed(Key::F2)) {
        showCollision_ = !showCollision_;
    }
    if (input.pressed(Key::F3)) {
        renderer.lighting().enabled = !renderer.lighting().enabled;
    }
    if (input.pressed(Key::F4)) {
        bandsOverride_ = (bandsOverride_ + 1) % std::size(kBandOverrides);
    }
    if (input.pressed(Key::F5)) {
        renderer.fluid().enabled = !renderer.fluid().enabled;
    }
    if (input.pressed(Key::F6)) {
        renderer.post().enabled = !renderer.post().enabled;
    }
    if (input.pressed(Key::F7)) {
        shadowsEnabled_ = !shadowsEnabled_;
    }
    if (input.pressed(Key::F9) && currentRoom_) {
        looks_.clear(); // re-read the presets: edit data/world/looks/*.json and press F9
        lookFrom_ = lookTo_ = roomLook(*currentRoom_);
        lookBlend_ = 1.0f;
        animal::log::info("Reloaded look '{}'", lookName_);
    }
    if (input.pressed(Key::F8)) {
        resetProgress(renderer);
    }
    if (input.gamepadConnected() && (input.down(GamepadButton::South) || input.axis(GamepadAxis::LeftX) != 0.0f)) {
        usingGamepad_ = true;
    } else if (input.down(Key::Left) || input.down(Key::Right) || input.down(Key::Space) || input.down(Key::Z)) {
        usingGamepad_ = false;
    }
}

void SandboxGame::respawn(animal::Renderer2D& renderer) {
    enterRoom(*checkpointRoom_, renderer); // rebuilds the room even when already in it
    placePlayerFeet(checkpointFeet_);
    playerState_ = {};
    climb_ = {};
    checkpointPending_ = false;
}

// Resumes at the saved checkpoint with the saved gems collected; a save naming a room that is
// no longer in the world keeps its gems but starts at the spawn point.
void SandboxGame::loadProgress() {
    if (!animal::fileExists(savePath_)) {
        return;
    }
    std::optional<animal::SaveData> loaded = animal::readSave(savePath_);
    if (!loaded) {
        return; // readSave logged why; the next save overwrites the file
    }
    save_.flags = loaded->flags;
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
    if (!savePath_.empty() && animal::fileExists(savePath_)) {
        animal::deleteFile(savePath_);
    }
    save_.flags.clear();
    save_.room = roomName(startRoom_->path);
    save_.position = startFeet_;
    checkpointRoom_ = startRoom_;
    checkpointFeet_ = startFeet_;
    respawn(renderer);
    showMessage("PROGRESS RESET");
}

void SandboxGame::showMessage(std::string text) {
    message_ = std::move(text);
    messageTime_ = kMessageSeconds;
}

// Gems, crawler hits, triggers and checkpoints, after the player has moved this step.
void SandboxGame::updateGameplay(animal::Entity& player) {
    const animal::IntRect collider = player.body.worldCollider();
    const std::vector<std::string> gems = cave_.collectGems(collider);
    save_.flags.insert(gems.begin(), gems.end());
    if (!gems.empty()) {
        saveProgress();
    }

    if (invulnerable_ <= 0.0f) {
        if (const std::optional<animal::Vec2> crawler = cave_.touchingCrawler(collider)) {
            const float playerX = static_cast<float>(collider.x) + collider.width * 0.5f;
            const float away = playerX < crawler->x ? -1.0f : (playerX > crawler->x ? 1.0f : (facingLeft_ ? 1.0f : -1.0f));
            playerState_.velocity = {away * kKnockback.x, kKnockback.y};
            playerState_.rising = false; // not a jump: releasing the button must not cut it
            playerState_.grounded = false;
            climb_.climbing = false;
            climb_.cooldown = climbParams_.regrabDelay;
            invulnerable_ = kInvulnerableSeconds;            stunned_ = kStunSeconds;
        }
    }

    for (std::string& message : cave_.enteredTriggers(collider)) {
        showMessage(std::move(message));
    }

    if (checkpointPending_ && playerState_.grounded && !climb_.climbing) {
        checkpointPending_ = false;
        checkpointRoom_ = currentRoom_;
        checkpointFeet_ = {static_cast<float>(collider.x) + collider.width * 0.5f,
                           static_cast<float>(collider.y + collider.height)};
        save_.room = roomName(currentRoom_->path);
        save_.position = checkpointFeet_;
        saveProgress();
    }
}

// Leaving the room through an edge moves the player into the neighbouring room at the same
// world position (Tiled world layout); without a neighbour below, the player respawns.
void SandboxGame::handleRoomExit(animal::Renderer2D& renderer) {
    animal::Entity* player = entities_.get(player_);
    const animal::IntRect bounds = cave_.tiles().pixelBounds();
    const animal::IntRect c = player->body.worldCollider();
    const animal::Vec2i center{c.x + c.width / 2, c.y + c.height / 2};
    if (center.x >= 0 && center.y >= 0 && center.x < bounds.width && center.y < bounds.height) {
        return;
    }
    const animal::Vec2i global{currentRoom_->rect.x + center.x, currentRoom_->rect.y + center.y};
    const animal::WorldRoom* next = world_.roomContaining(global);
    if (next && next != currentRoom_) {
        const animal::Vec2i offset{next->rect.x - currentRoom_->rect.x, next->rect.y - currentRoom_->rect.y};
        const animal::Vec2 remainder = player->body.remainder;
        if (enterRoom(*next, renderer)) {
            player->body.position = player->body.position - offset;
            player->body.remainder = remainder;
        }
    } else if (center.y >= bounds.height + kFallMargin) {
        respawn(renderer);
    }
}

void SandboxGame::onFixedUpdate(animal::EngineContext& context, double dt) {
    time_ += dt;
    fluidTime_ += static_cast<float>(dt);
    const float step = static_cast<float>(dt);
    lookBlend_ = std::min(1.0f, lookBlend_ + step / kLookFadeSeconds);
    invulnerable_ = std::max(0.0f, invulnerable_ - step);
    stunned_ = std::max(0.0f, stunned_ - step);
    messageTime_ = std::max(0.0f, messageTime_ - step);
    animal::Entity* player = entities_.get(player_);
    if (player) {
        const bool control = stunned_ <= 0.0f;
        const float moveX = control ? actions_.strength(MoveRight) - actions_.strength(MoveLeft) : 0.0f;
        const float moveY = control ? actions_.strength(MoveDown) - actions_.strength(MoveUp) : 0.0f;
        const bool jumpPressed = control && actions_.pressed(Jump);
        const bool wasClimbing = climb_.climbing;
        const bool climbing = animal::stepClimb(player->body, playerState_, climb_, {moveX, moveY, jumpPressed},
                                                climbParams_, cave_.vines(), cave_.tiles(), static_cast<float>(dt));
        if (!climbing) {
            animal::PlatformerInput input;
            input.moveX = moveX;
            // A jump that let go of a vine is already used up.
            input.jumpPressed = jumpPressed && !wasClimbing;
            input.jumpHeld = control && actions_.down(Jump);
            animal::stepPlatformer(player->body, playerState_, input, playerParams_, cave_.tiles(),
                                   static_cast<float>(dt));
        }
        if (moveX < 0.0f) {
            facingLeft_ = true;
        } else if (moveX > 0.0f) {
            facingLeft_ = false;
        }
        updateAnimation(static_cast<float>(dt));
        handleRoomExit(*context.renderer);
        if ((player = entities_.get(player_))) {
            updateGameplay(*player);
        }
    }

    std::vector<animal::Influence> influences;
    if (player) {
        animal::Influence influence{player->body.worldCollider(), playerState_.velocity, false};
        if (playerState_.landedThisStep) {
            influence.landed = true;
            influence.velocity.y = playerState_.landingSpeed;
        }
        influences.push_back(influence);
    }
    cave_.step(static_cast<float>(dt), time_, influences);
    actions_.endFixedStep();
    entities_.flushDestroyed();
}

void SandboxGame::onRender(animal::EngineContext&, animal::Renderer2D& renderer) {
    applyCurrentLook(renderer);
    renderer.beginWorld(camera_, kBackdrop);
    cave_.drawBehindPlayer(renderer, assets_);
    cave_.drawGems(renderer, assets_, time_);

    const animal::Entity* player = entities_.get(player_);
    // While invulnerable after a hit the creature blinks.
    const bool blinkedOut = invulnerable_ > 0.0f && static_cast<int>(invulnerable_ * 16.0f) % 2 == 1;
    if (player && !blinkedOut) {
        const float size = static_cast<float>(kTileSize);
        const animal::Vec2i p = player->body.position + kSpriteOffset;
        const float frame = static_cast<float>(animator_.frame());
        renderer.drawSprite(creature_, {static_cast<float>(p.x), static_cast<float>(p.y)},
                            {frame * size, 0.0f, size, size}, animal::Color::white(), facingLeft_);
    }
    if (player) {
        animal::PointLight glow;
        glow.position = player->body.exactPosition() + animal::Vec2{3.0f, 3.0f};
        glow.radius = 30.0f;
        glow.color = kPlayerGlowColor;
        glow.intensity = 0.55f;
        glow.height = 14.0f;
        renderer.addLight(glow);
    }
    cave_.drawInFrontOfPlayer(renderer); // grass in front: the player wades through it
    cave_.addLights(renderer, time_);
    addStressLights(renderer);
    renderer.setFluidTimeStep(fluidTime_);
    fluidTime_ = 0.0f;
    cave_.addFluidSources(renderer, time_);
    renderer.endWorld();

    if (showCollision_ && player) {
        renderer.beginWorldOverlay(camera_);
        cave_.drawCollision(renderer);
        const animal::IntRect c = player->body.worldCollider();
        renderer.strokeRect({static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.width),
                             static_cast<float>(c.height)},
                            playerState_.grounded ? animal::Color::rgba(80, 255, 120)
                                                  : animal::Color::rgba(255, 220, 60));
        renderer.endWorldOverlay();
    }
}

void SandboxGame::onScreen(animal::EngineContext&, animal::Renderer2D& renderer) {
    const animal::PresentLayout& layout = renderer.presentLayout();
    const float margin = static_cast<float>(6 * renderer.overlayScale());
    const size_t collected = static_cast<size_t>(std::ranges::count_if(
        save_.flags, [this](const std::string& flag) { return gemFlags_.count(flag) != 0; }));
    const std::string gems = std::format("GEMS {}/{}", collected, gemFlags_.size());
    renderer.drawText({static_cast<float>(layout.x + layout.width) - margin - renderer.textWidth(gems),
                       static_cast<float>(layout.y) + margin},
                      gems, kHudColor);

    if (messageTime_ > 0.0f && !message_.empty()) {
        const float fade = std::min({1.0f, messageTime_ / 0.6f, (kMessageSeconds - messageTime_) / 0.3f});
        animal::Color color = kMessageColor;
        color.a = static_cast<uint8_t>(std::lround(255.0f * fade));
        renderer.drawText({static_cast<float>(layout.x) + (layout.width - renderer.textWidth(message_)) * 0.5f,
                           static_cast<float>(layout.y + layout.height) - margin - renderer.textLineHeight()},
                          message_, color);
    }
}

void SandboxGame::onDebugText(animal::EngineContext& context, std::vector<std::string>& lines) {
    if (currentRoom_) {
        lines.push_back(std::format("ROOM {} ({} IN WORLD)", roomName(currentRoom_->path), world_.rooms.size()));
    }
    if (const animal::Entity* player = entities_.get(player_)) {
        const animal::PlatformerState& s = playerState_;
        lines.push_back(std::format("PLAYER {},{}  VEL {:.0f},{:.0f}  {}", player->body.position.x,
                                    player->body.position.y, s.velocity.x, s.velocity.y,
                                    climb_.climbing ? "CLIMBING" : (s.grounded ? "GROUNDED" : "AIRBORNE")));
    }
    lines.push_back(std::format("GRASS {}  VINES {}  PARTICLES {}", cave_.bladeCount(), cave_.vineCount(),
                                cave_.particleCount()));
    animal::Renderer2D& renderer = *context.renderer;
    const int bands = renderer.lighting().bands;
    lines.push_back(std::format("F3 LIGHT {}  F4 BANDS {}{}  F5 SMOKE {}  F6 POST {}  F7 SHADOWS {}",
                                renderer.lighting().enabled ? "ON" : "OFF",
                                bands == 0 ? std::string("SMOOTH") : std::to_string(bands),
                                kBandOverrides[bandsOverride_] < 0 ? " (LOOK)" : "",
                                !renderer.fluid().enabled ? "OFF" : (renderer.fluidSleeping() ? "ASLEEP" : "ON"),
                                renderer.post().enabled ? "ON" : "OFF",
                                shadowsEnabled_ ? "ON" : "OFF"));
    lines.push_back(std::format("LOOK {}  F9 RELOAD LOOKS", lookName_));
    lines.push_back(usingGamepad_ ? "GAMEPAD: STICK MOVE  UP GRAB VINE  A JUMP"
                                  : "ARROWS/WASD MOVE  UP GRAB VINE  SPACE/Z JUMP");
    lines.push_back(std::format("F2 COLLISION VIEW ({})  F8 RESET PROGRESS{}", showCollision_ ? "ON" : "OFF",
                                savePath_.empty() ? " (NOT SAVED)" : ""));
}

void SandboxGame::onShutdown(animal::EngineContext& context) {
    animal::destroyMaterial(*context.device, assets_.tileset);
    animal::destroyMaterial(*context.device, assets_.props);
    animal::destroyMaterial(*context.device, creature_);
    animal::destroyMaterial(*context.device, assets_.crawler);
}

} // namespace game
