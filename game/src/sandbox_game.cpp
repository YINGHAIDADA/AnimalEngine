#include "sandbox_game.h"

#include <animal/core/file.h>
#include <animal/core/log.h>
#include <animal/core/profiler.h>
#include <animal/core/random.h>
#include <animal/platform/input.h>
#include <animal/platform/platform.h>
#include <animal/renderer/renderer2d.h>
#include <animal/rhi/rhi.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iterator>
#include <optional>

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

std::string upperCase(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

} // namespace

std::string roomName(const std::string& path) {
    const std::u8string stem = std::filesystem::path(std::u8string(path.begin(), path.end())).stem().u8string();
    return {stem.begin(), stem.end()};
}

animal::AppConfig SandboxGame::configure() {
    animal::AppConfig config;
    config.title = "Animal Engine · Sandbox";
    config.applicationId = "Sandbox";
    return config;
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
    if (!room || !cave_.build(*room, roomName(worldRoom.path), save_.flags, collectedGems(), effects_, plants_,
                              kRoomSeed + roomVisits_++)) {
        return false;
    }
    const bool firstRoom = currentRoom_ == nullptr;
    currentRoom_ = &worldRoom;
    save_.visited.insert(roomName(worldRoom.path));
    checkpointPending_ = true;
    submersion_ = 0.0f;
    diving_ = false;
    climb_.climbing = false; // vine indices belong to the previous room
    renderer.resetFluid();
    const animal::Vec2 canvasOrigin = camera_.topLeft(animal::Renderer2D::kCanvasExtent);
    renderer.setFluidObstacles(cave_.fluidObstacles(canvasOrigin));
    renderer.setLightOccluders(cave_.lightOccluders(canvasOrigin));
    renderer.setGpuEmitters(effectsEnabled_ ? std::span(cave_.gpuEmitters()) : std::span<const animal::GpuEmitter>());
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
    worldPath_ = context.dataDirectory + "world/test.world";
    auto world = animal::loadWorld(worldPath_);
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
        collectGemFlags(worldRoom, *room);
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
        checkpointFeet_ = entryFeet(*it);
    }
    save_.flags.insert(testFlags_.begin(), testFlags_.end());
    for (auto gem = gemFlags_.begin(); gem != gemFlags_.end() && collectedGems() < testGems_; ++gem) {
        save_.flags.insert(*gem);
    }

    animal::FluidSettings& fluid = context.renderer->fluid();
    fluid.smokeKeepPerSecond = 0.8f;
    fluid.velocityKeepPerSecond = 0.7f;
    fluid.buoyancy = 45.0f;
    registerTuning(*context.renderer);
    tuningPath_ = context.dataDirectory + "world/tuning.json";
    loadTuning();

    camera_.center = kCameraCenter;
    player_ = entities_.create(animal::EntityKind::Player);
    entities_.get(player_)->body.collider = kPlayerCollider;
    if (!enterRoom(*checkpointRoom_, *context.renderer)) {
        return false;
    }
    placePlayerFeet(checkpointFeet_);
    bindActions();
    audio_ = context.audio;
    const std::string sounds = context.dataDirectory + "sounds/";
    sounds_ = {audio_->load(sounds + "jump.wav"),   audio_->load(sounds + "land.wav"),
               audio_->load(sounds + "gem.wav"),    audio_->load(sounds + "hit.wav"),
               audio_->load(sounds + "splash.wav"), audio_->load(sounds + "grab.wav"),
               audio_->load(sounds + "cave_ambience.wav"), audio_->load(sounds + "click.wav"),
               audio_->load(sounds + "door.wav"),   audio_->load(sounds + "ability.wav"),
               audio_->load(sounds + "crumble.wav")};
    ambienceVoice_ = audio_->play(sounds_.ambience, {1.0f, 0.0f, 1.0f, true, animal::audio::Bus::Ambience, 2.0f});
    creatureSheetPath_ = textures + "creature.json";
    std::optional<animal::SpriteSheet> sheet = animal::loadSpriteSheet(creatureSheetPath_);
    if (!sheet) {
        return false;
    }
    creatureSheet_ = std::move(*sheet);
    if (!setupAnimations()) {
        return false;
    }
    return dev_.init(context, *this);
}

void SandboxGame::registerTuning(animal::Renderer2D& renderer) {
    animal::PlatformerParams& p = playerParams_;
    tuning_.add("player.runSpeed", &p.runSpeed, 0.0f, 200.0f);
    tuning_.add("player.groundAcceleration", &p.groundAcceleration, 0.0f, 3000.0f);
    tuning_.add("player.groundDeceleration", &p.groundDeceleration, 0.0f, 3000.0f);
    tuning_.add("player.airAcceleration", &p.airAcceleration, 0.0f, 3000.0f);
    tuning_.add("player.airDeceleration", &p.airDeceleration, 0.0f, 3000.0f);
    tuning_.add("player.gravity", &p.gravity, 0.0f, 2000.0f);
    tuning_.add("player.fallGravityMultiplier", &p.fallGravityMultiplier, 0.5f, 3.0f);
    tuning_.add("player.apexGravityMultiplier", &p.apexGravityMultiplier, 0.0f, 1.5f);
    tuning_.add("player.apexSpeedThreshold", &p.apexSpeedThreshold, 0.0f, 100.0f);
    tuning_.add("player.maxFallSpeed", &p.maxFallSpeed, 50.0f, 500.0f);
    tuning_.add("player.jumpSpeed", &p.jumpSpeed, 50.0f, 400.0f);
    tuning_.add("player.jumpCutMultiplier", &p.jumpCutMultiplier, 0.0f, 1.0f);
    tuning_.add("player.coyoteFrames", &p.coyoteFrames, 0, 20);
    tuning_.add("player.jumpBufferFrames", &p.jumpBufferFrames, 0, 20);
    tuning_.add("player.cornerCorrection", &p.cornerCorrection, 0, 4);
    animal::ClimbParams& c = climbParams_;
    tuning_.add("climb.climbSpeed", &c.climbSpeed, 0.0f, 120.0f);
    tuning_.add("climb.swingForce", &c.swingForce, 0.0f, 2000.0f);
    tuning_.add("climb.weight", &c.weight, 0.0f, 1500.0f);
    tuning_.add("climb.jumpSpeed", &c.jumpSpeed, 0.0f, 400.0f);
    tuning_.add("climb.jumpSideSpeed", &c.jumpSideSpeed, 0.0f, 200.0f);
    tuning_.add("climb.inheritVelocity", &c.inheritVelocity, 0.0f, 1.5f);
    tuning_.add("climb.regrabDelay", &c.regrabDelay, 0.0f, 1.0f);
    tuning_.add("climb.grabMargin", &c.grabMargin, 0, 6);
    animal::SwimParams& s = swimParams_;
    tuning_.add("swim.floatDepth", &s.floatDepth, 0.1f, 1.0f);
    tuning_.add("swim.runScale", &s.runScale, 0.1f, 1.0f);
    tuning_.add("swim.fallSpeedScale", &s.fallSpeedScale, 0.05f, 1.0f);
    tuning_.add("swim.drag", &s.drag, 0.0f, 10.0f);
    tuning_.add("swim.jumpDepth", &s.jumpDepth, 0.0f, 1.0f);
    animal::DiveParams& d = diveParams_;
    tuning_.add("dive.speed", &d.speed, 10.0f, 200.0f);
    tuning_.add("dive.acceleration", &d.acceleration, 50.0f, 2000.0f);
    tuning_.add("dive.drift", &d.drift, 0.0f, 60.0f);
    tuning_.add("dive.enterDepth", &d.enterDepth, 0.1f, 1.0f);
    tuning_.add("dive.leaveDepth", &d.leaveDepth, 0.1f, 1.0f);
    tuning_.add("audio.effects", &busVolumes_[0], 0.0f, 1.0f);
    tuning_.add("audio.ambience", &busVolumes_[1], 0.0f, 1.0f);
    tuning_.add("audio.music", &busVolumes_[2], 0.0f, 1.0f);
    animal::FluidSettings& f = renderer.fluid();
    tuning_.add("smoke.velocityKeepPerSecond", &f.velocityKeepPerSecond, 0.0f, 1.0f);
    tuning_.add("smoke.smokeKeepPerSecond", &f.smokeKeepPerSecond, 0.0f, 1.0f);
    tuning_.add("smoke.buoyancy", &f.buoyancy, 0.0f, 150.0f);
    tuning_.add("smoke.vorticity", &f.vorticity, 0.0f, 40.0f);
    tuning_.add("smoke.pressureIterations", &f.pressureIterations, 2, 40);
}

// world/tuning.json is optional; a broken file is reported and leaves the built-in values.
void SandboxGame::loadTuning() {
    if (!animal::fileExists(tuningPath_)) {
        return;
    }
    const std::optional<std::string> text = animal::readText(tuningPath_);
    if (!text) {
        animal::log::error("Cannot read {}", tuningPath_);
        return;
    }
    for (const std::string& problem : tuning_.apply(*text, tuningPath_).problems) {
        animal::log::warn("Tuning: {}", problem);
    }
}

bool SandboxGame::rebuildRoom(animal::Renderer2D& renderer) {
    if (!currentRoom_) {
        return false;
    }
    const animal::Room* room = roomData(*currentRoom_);
    if (!room || !cave_.build(*room, roomName(currentRoom_->path), save_.flags, collectedGems(), effects_, plants_,
                              kRoomSeed + roomVisits_++)) {
        return false;
    }
    climb_.climbing = false; // vine indices may have changed
    renderer.resetFluid();
    const animal::Vec2 canvasOrigin = camera_.topLeft(animal::Renderer2D::kCanvasExtent);
    renderer.setFluidObstacles(cave_.fluidObstacles(canvasOrigin));
    renderer.setLightOccluders(cave_.lightOccluders(canvasOrigin));
    renderer.setGpuEmitters(effectsEnabled_ ? std::span(cave_.gpuEmitters()) : std::span<const animal::GpuEmitter>());
    cave_.effects().setEnabled(effectsEnabled_);
    lookFrom_ = lookTo_ = roomLook(*currentRoom_);
    lookBlend_ = 1.0f;
    return true;
}

void SandboxGame::collectGemFlags(const animal::WorldRoom& worldRoom, const animal::Room& room) {
    const std::string prefix = roomName(worldRoom.path) + ":";
    std::erase_if(gemFlags_, [&prefix](const std::string& flag) { return flag.starts_with(prefix); });
    for (const animal::LevelObject* gem : room.objectsOfType("gem")) {
        gemFlags_.insert(CaveRoom::gemFlag(roomName(worldRoom.path), gem->id));
    }
}

bool SandboxGame::reloadData(const std::string& path, animal::Renderer2D& renderer) {
    const auto readValid = [&path]() -> std::optional<std::string> {
        std::optional<std::string> text = animal::readText(path);
        if (!text) {
            animal::log::error("Reload: cannot read {}", path);
        }
        return text;
    };
    const auto rebuildIfUsed = [&] {
        return !cave_.presetPaths().contains(path) || rebuildRoom(renderer);
    };

    if (path.ends_with(".look.json")) {
        const std::optional<std::string> text = readValid();
        std::string error;
        if (!text || !animal::parseLook(*text, path, animal::readText, animal::Look::engineDefaults(), &error)) {
            animal::log::error("Reload: {}", error.empty() ? path : error);
            return false;
        }
        reloadLooks(); // presets may be bases of others: re-read them all
        return true;
    }
    if (path.ends_with(".effect.json")) {
        const std::optional<std::string> text = readValid();
        std::string error;
        std::optional<animal::EffectPreset> preset = text ? animal::parseEffect(*text, path, &error) : std::nullopt;
        if (!preset) {
            animal::log::error("Reload: {}", error.empty() ? path : error);
            return false;
        }
        effects_.set(path, std::move(*preset));
        return rebuildIfUsed();
    }
    if (path.ends_with(".plant.json")) {
        const std::optional<std::string> text = readValid();
        std::string error;
        std::optional<animal::PlantSpecies> species = text ? animal::parsePlant(*text, path, &error) : std::nullopt;
        if (!species) {
            animal::log::error("Reload: {}", error.empty() ? path : error);
            return false;
        }
        plants_.set(path, std::move(*species));
        return rebuildIfUsed();
    }
    if (path == tuningPath_) {
        const std::optional<std::string> text = readValid();
        if (!text) {
            return false;
        }
        const animal::Tuning::ApplyResult check = tuning_.apply(*text, path, true);
        if (!check.valid) {
            animal::log::error("Reload: {}", check.problems.front());
            return false;
        }
        tuning_.resetToBuiltIn();
        loadTuning();
        return true;
    }
    if (path.ends_with(".tmj") || path.ends_with(".tsj")) {
        // A room, or a tileset that rooms use: reload every affected cached room, all or nothing.
        std::map<std::string, animal::Room> reloaded;
        for (const auto& [roomPath, room] : rooms_) {
            if (path.ends_with(".tmj") && roomPath != path) {
                continue;
            }
            const animal::WorldRoom* worldRoom = nullptr;
            for (const animal::WorldRoom& r : world_.rooms) {
                worldRoom = r.path == roomPath ? &r : worldRoom;
            }
            std::optional<animal::Room> fresh = animal::loadRoom(roomPath);
            if (!fresh) {
                return false; // loadRoom logged why
            }
            if (worldRoom && (fresh->tiles.pixelBounds().width != worldRoom->rect.width ||
                              fresh->tiles.pixelBounds().height != worldRoom->rect.height)) {
                animal::log::error("Reload: {}: map size does not match its size in the world file", roomPath);
                return false;
            }
            reloaded.emplace(roomPath, std::move(*fresh));
        }
        for (auto& [roomPath, room] : reloaded) {
            rooms_[roomPath] = std::move(room);
            for (const animal::WorldRoom& r : world_.rooms) {
                if (r.path == roomPath) {
                    collectGemFlags(r, rooms_[roomPath]);
                }
            }
        }
        return !currentRoom_ || !reloaded.contains(currentRoom_->path) || rebuildRoom(renderer);
    }
    if (path.ends_with(".world")) {
        return reloadWorld(path, renderer);
    }
    if (path == creatureSheetPath_) {
        std::optional<animal::SpriteSheet> sheet = animal::loadSpriteSheet(path);
        if (!sheet) {
            return false;
        }
        const animal::SpriteSheet previous = std::move(creatureSheet_);
        creatureSheet_ = std::move(*sheet);
        if (!setupAnimations()) {
            creatureSheet_ = previous;
            setupAnimations();
            return false;
        }
        return true;
    }
    return true; // not something the game reads
}

// The world layout: rooms are found again by path; a layout that drops a room in use is refused.
bool SandboxGame::reloadWorld(const std::string& path, animal::Renderer2D& renderer) {
    std::optional<animal::World> world = animal::loadWorld(path);
    if (!world) {
        return false;
    }
    const auto find = [&world](const animal::WorldRoom* room) -> const animal::WorldRoom* {
        if (!room) {
            return nullptr;
        }
        for (const animal::WorldRoom& r : world->rooms) {
            if (r.path == room->path) {
                return &r;
            }
        }
        return nullptr;
    };
    const animal::WorldRoom* current = find(currentRoom_);
    const animal::WorldRoom* start = find(startRoom_);
    const animal::WorldRoom* checkpoint = find(checkpointRoom_);
    if ((currentRoom_ && !current) || !start || !checkpoint) {
        animal::log::error("Reload: {} no longer contains a room in use (current, start or checkpoint)", path);
        return false;
    }
    for (const animal::WorldRoom& r : world->rooms) {
        if (!roomData(r)) {
            return false;
        }
    }
    world_ = std::move(*world); // moving the vector keeps its elements where they are
    for (const animal::WorldRoom& r : world_.rooms) {
        collectGemFlags(r, *roomData(r)); // rooms may have been added
    }
    currentRoom_ = current;
    startRoom_ = start;
    checkpointRoom_ = checkpoint;
    return rebuildRoom(renderer);
}

// Where tests and teleports put the player: the room's "entry" point, else its "spawn" point, else
// above the floor the original rooms have at the left.
animal::Vec2 SandboxGame::entryFeet(const animal::WorldRoom& worldRoom) {
    animal::Vec2 feet{4.0f * kTileSize + 4.0f, 20.0f * kTileSize};
    if (const animal::Room* room = roomData(worldRoom)) {
        for (const char* type : {"spawn", "entry"}) {
            for (const animal::LevelObject* point : room->objectsOfType(type)) {
                feet = point->position;
            }
        }
    }
    return feet;
}

bool SandboxGame::teleport(const animal::WorldRoom& worldRoom, animal::Renderer2D& renderer) {
    if (!enterRoom(worldRoom, renderer)) {
        return false;
    }
    placePlayerFeet(entryFeet(worldRoom));
    playerState_ = {};
    climb_ = {};
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

bool SandboxGame::setupAnimations() {
    animator_ = {};
    const auto add = [this](const char* tag, animal::Animator::ClipId& id) {
        const animal::AnimationClip* clip = creatureSheet_.clip(tag);
        if (!clip) {
            animal::log::error("{}: no \"{}\" tag", creatureSheetPath_, tag);
            return false;
        }
        id = animator_.add(*clip);
        return true;
    };
    if (!add("idle", clips_.idle) || !add("run", clips_.run) || !add("jump", clips_.jump) ||
        !add("fall", clips_.fall) || !add("climb", clips_.climb) || !add("land", clips_.land)) {
        return false;
    }
    animator_.play(clips_.idle);
    return true;
}

// The animation state machine: the clip follows the movement state.
void SandboxGame::updateAnimation(float dt) {
    const animal::PlatformerState& s = playerState_;
    if (climb_.climbing || diving_) {
        animator_.play(clips_.climb); // paddling under water looks like climbing
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
    const bool idle = (climb_.climbing && !climb_.movedThisStep) ||
                      (diving_ && std::fabs(playerState_.velocity.x) + std::fabs(playerState_.velocity.y) < 20.0f);
    animator_.update(dt, idle ? 0.0f : 1.0f);
}

void SandboxGame::onFrame(animal::EngineContext& context) {
    dev_.frame(context);
    if (audio_) {
        audio_->setBusVolume(animal::audio::Bus::Effects, busVolumes_[0]);
        audio_->setBusVolume(animal::audio::Bus::Ambience, busVolumes_[1]);
        audio_->setBusVolume(animal::audio::Bus::Music, busVolumes_[2]);
    }
    const animal::platform::Input& input = *context.input;
    if (context.demo) {
        applyDemoInput();
    } else if (!dev_.editing()) {
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
    if (input.pressed(Key::F9)) {
        reloadLooks();
    }
    if (input.pressed(Key::F10)) {
        setEffectsEnabled(!effectsEnabled_, renderer);
    }
    if (input.pressed(Key::F8)) {
        resetProgress(renderer);
    }
    if (input.pressed(Key::M) || input.pressed(GamepadButton::Back)) {
        mapOpen_ = !mapOpen_;
    }
    if (input.gamepadConnected() && (input.down(GamepadButton::South) || input.axis(GamepadAxis::LeftX) != 0.0f)) {
        usingGamepad_ = true;
    } else if (input.down(Key::Left) || input.down(Key::Right) || input.down(Key::Space) || input.down(Key::Z)) {
        usingGamepad_ = false;
    }
}

// F9: re-read the look presets (edit data/world/looks/*.json, or let the developer tools reload them).
void SandboxGame::reloadLooks() {
    if (!currentRoom_) {
        return;
    }
    looks_.clear();
    lookFrom_ = lookTo_ = roomLook(*currentRoom_);
    lookBlend_ = 1.0f;
    animal::log::info("Reloaded look '{}'", lookName_);
}

void SandboxGame::setEffectsEnabled(bool enabled, animal::Renderer2D& renderer) {
    effectsEnabled_ = enabled;
    cave_.effects().setEnabled(effectsEnabled_);
    renderer.setGpuEmitters(effectsEnabled_ ? std::span(cave_.gpuEmitters()) : std::span<const animal::GpuEmitter>());
}

void SandboxGame::respawn(animal::Renderer2D& renderer) {
    ++respawns_;
    if (health_ <= 0) {
        health_ = kMaxHealth;
        showMessage("KNOCKED OUT");
    }
    enterRoom(*checkpointRoom_, renderer); // rebuilds the room even when already in it
    placePlayerFeet(checkpointFeet_);
    playerState_ = {};
    climb_ = {};
    checkpointPending_ = false;
    respawnPending_ = false;
    diving_ = false;
}

void SandboxGame::playSound(animal::audio::SoundHandle sound, float x, float volume) {
    if (!audio_) {
        return;
    }
    const float pan = std::clamp((x - kCameraCenter.x) / kCameraCenter.x * 0.7f, -0.7f, 0.7f);
    audio_->play(sound, {volume, pan, soundRandom_.range(0.95f, 1.05f), false, animal::audio::Bus::Effects, 0.0f});
}

void SandboxGame::showMessage(std::string text) {
    message_ = std::move(text);
    messageTime_ = kMessageSeconds;
}

// Gems, crawler hits, triggers and checkpoints, after the player has moved this step.
void SandboxGame::updateGameplay(animal::Entity& player) {
    const animal::IntRect collider = player.body.worldCollider();
    const float playerX = static_cast<float>(collider.x) + collider.width * 0.5f;
    const float wasSubmerged = submersion_;
    submersion_ = cave_.updatePlayerInWater(collider, playerState_.velocity, submersion_);
    if (wasSubmerged <= 0.0f && submersion_ > 0.0f) {
        playSound(sounds_.splash, playerX, std::clamp(std::fabs(playerState_.velocity.y) / 200.0f, 0.3f, 1.0f));
    }
    const std::vector<std::string> gems = cave_.collectGems(collider);
    save_.flags.insert(gems.begin(), gems.end());
    if (!gems.empty()) {
        playSound(sounds_.gem, playerX);
        saveProgress();
    }

    if (invulnerable_ <= 0.0f) {
        if (const std::optional<animal::Vec2> crawler = cave_.touchingCrawler(collider)) {
            const float away = playerX < crawler->x ? -1.0f : (playerX > crawler->x ? 1.0f : (facingLeft_ ? 1.0f : -1.0f));
            playerState_.velocity = {away * kKnockback.x, kKnockback.y};
            playerState_.rising = false; // not a jump: releasing the button must not cut it
            playerState_.grounded = false;
            climb_.climbing = false;
            climb_.cooldown = climbParams_.regrabDelay;
            invulnerable_ = kInvulnerableSeconds;
            stunned_ = kStunSeconds;
            playSound(sounds_.hit, playerX);
            takeDamage();
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
    if (mapOpen_ || endingOpen_) {
        if (endingOpen_ && actions_.pressed(Jump)) {
            endingOpen_ = false;
        }
        actions_.endFixedStep(); // the world waits; presses made meanwhile do not carry over
        return;
    }
    time_ += dt;
    save_.playSeconds += dt;
    fluidTime_ += static_cast<float>(dt);
    gpuParticleTime_ += static_cast<float>(dt);
    const float step = static_cast<float>(dt);
    lookBlend_ = std::min(1.0f, lookBlend_ + step / kLookFadeSeconds);
    invulnerable_ = std::max(0.0f, invulnerable_ - step);
    stunned_ = std::max(0.0f, stunned_ - step);
    messageTime_ = std::max(0.0f, messageTime_ - step);
    animal::Entity* player = entities_.get(player_);
    const bool editing = dev_.editing(); // the level editor freezes the player; the room keeps moving
    std::optional<animal::profiler::Zone> playerZone(std::in_place, "player");
    if (player && !editing) {
        const bool control = stunned_ <= 0.0f;
        const float moveX = control ? actions_.strength(MoveRight) - actions_.strength(MoveLeft) : 0.0f;
        const float moveY = control ? actions_.strength(MoveDown) - actions_.strength(MoveUp) : 0.0f;
        const bool jumpPressed = control && actions_.pressed(Jump);
        stepPlatforms(*player, step);
        const bool wasClimbing = climb_.climbing;
        const bool climbing = animal::stepClimb(player->body, playerState_, climb_, {moveX, moveY, jumpPressed},
                                                climbParams_, cave_.vines(), cave_.tiles(), static_cast<float>(dt));
        const float playerX = player->body.exactPosition().x + 3.0f;
        if (climbing && !wasClimbing) {
            playSound(sounds_.grab, playerX, 0.7f);
        } else if (!climbing && wasClimbing && jumpPressed) {
            playSound(sounds_.jump, playerX, 0.8f);
        }
        const bool wasRising = playerState_.rising;
        if (!climbing && !stepDive(*player, moveX, moveY, step)) {
            animal::PlatformerInput input;
            input.moveX = moveX;
            // A jump that let go of a vine is already used up.
            input.jumpPressed = jumpPressed && !wasClimbing;
            input.jumpHeld = control && actions_.down(Jump);
            const float submersion = cave_.submersion(player->body.worldCollider());
            animal::applyWater(playerState_, playerParams_, submersion, static_cast<float>(dt), swimParams_);
            animal::stepPlatformer(player->body, playerState_, input,
                                   animal::swimmingParams(playerParams_, submersion, swimParams_), cave_.tiles(),
                                   static_cast<float>(dt));
            if (playerState_.rising && !wasRising) {
                playSound(sounds_.jump, playerX, 0.8f);
            }
            if (playerState_.landedThisStep) {
                playSound(sounds_.land, playerX, std::clamp(playerState_.landingSpeed / 260.0f, 0.15f, 1.0f));
            }
        }
        applyCurrents(*player, step);
        if (moveX < 0.0f) {
            facingLeft_ = true;
        } else if (moveX > 0.0f) {
            facingLeft_ = false;
        }
        updateAnimation(static_cast<float>(dt));
        handleRoomExit(*context.renderer);
        if ((player = entities_.get(player_))) {
            updateGameplay(*player);
            updateMechanisms(*player, step);
            if (respawnPending_) {
                respawn(*context.renderer);
                invulnerable_ = kInvulnerableSeconds; // blinks back in
                player = entities_.get(player_);
            }
        }
    }

    playerZone.reset();
    ANIMAL_PROFILE_ZONE("room");
    if (!player || editing) {
        cave_.stepPlatforms(step, nullptr); // the level editor freezes only the player
    }
    std::vector<animal::Influence> influences;
    if (player && !editing) {
        animal::Influence influence{player->body.worldCollider(), playerState_.velocity, false};
        influence.pushesVines = !climb_.climbing; // a climber hangs on its vine rather than pushing it
        if (playerState_.landedThisStep) {
            influence.landed = true;
            influence.velocity.y = playerState_.landingSpeed;
        }
        influences.push_back(influence);
    }
    cave_.step(static_cast<float>(dt), time_, influences);
    if (traceEvery_ > 0 && player && ++traceSteps_ % traceEvery_ == 0) {
        const animal::Vec2 p = player->body.exactPosition();
        animal::log::info("TRACE {} {} x {:.1f} y {:.1f} v {:.0f},{:.0f}{}{}{} sub {:.2f}", traceSteps_,
                          roomName(currentRoom_->path), p.x, p.y, playerState_.velocity.x, playerState_.velocity.y,
                          playerState_.grounded ? " grounded" : "", climb_.climbing ? " climbing" : "",
                          diving_ ? " diving" : "", submersion_);
    }
    actions_.endFixedStep();
    entities_.flushDestroyed();
}

void SandboxGame::onRender(animal::EngineContext&, animal::Renderer2D& renderer) {
    ANIMAL_PROFILE_ZONE("draw room");
    applyCurrentLook(renderer);
    renderer.setGpuParticleStep(gpuParticleTime_, cave_.windAt(kCameraCenter, time_));
    gpuParticleTime_ = 0.0f;
    renderer.beginWorld(camera_, kBackdrop);
    cave_.drawBehindPlayer(renderer, assets_);
    cave_.drawGems(renderer, assets_, time_);
    cave_.addWater(renderer, time_);

    const animal::Entity* player = entities_.get(player_);
    // While invulnerable after a hit the creature blinks.
    const bool blinkedOut = invulnerable_ > 0.0f && static_cast<int>(invulnerable_ * 16.0f) % 2 == 1;
    if (player && !blinkedOut) {
        const animal::SpriteFrame& f = creatureSheet_.frames[static_cast<size_t>(animator_.frame())];
        // Trimmed frames sit at their offset in the untrimmed sprite (mirrored when facing left).
        const int offsetX = facingLeft_ ? f.sourceSize.x - f.offset.x - f.source.width : f.offset.x;
        const animal::Vec2i p = player->body.position + kSpriteOffset + animal::Vec2i{offsetX, f.offset.y};
        renderer.drawSprite(creature_, {static_cast<float>(p.x), static_cast<float>(p.y)},
                            {static_cast<float>(f.source.x), static_cast<float>(f.source.y),
                             static_cast<float>(f.source.width), static_cast<float>(f.source.height)},
                            animal::Color::white(), facingLeft_);
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
    // The map and the ending screen cover the HUD.
    if (endingOpen_) {
        drawEnding(renderer);
        return;
    }
    if (mapOpen_) {
        drawMap(renderer);
        return;
    }
    const animal::PresentLayout& layout = renderer.presentLayout();
    const float margin = static_cast<float>(6 * renderer.overlayScale());
    const std::string gems = std::format("GEMS {}/{}", collectedGems(), gemFlags_.size());
    renderer.drawText({static_cast<float>(layout.x + layout.width) - margin - renderer.textWidth(gems),
                       static_cast<float>(layout.y) + margin},
                      gems, kHudColor);
    drawHearts(renderer);

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
    lines.push_back(std::format("GRASS {}  VINES {}", cave_.bladeCount(), cave_.vineCount()));
    const animal::EffectSystem& effects = cave_.effects();
    std::string effectLine = std::format("F10 EFFECTS {}", effects.enabled() ? "ON" : "OFF");
    if (effects.enabled()) {
        effectLine += std::format(" {:.2f} MS  GPU {}", effects.lastStepMilliseconds(), context.renderer->gpuParticleCount());
        for (size_t i = 0; i < effects.presets().size(); ++i) {
            effectLine += std::format("  {} {}", upperCase(effects.presets()[i].name),
                                      effects.countOf(static_cast<animal::EffectSystem::PresetId>(i)));
        }
    }
    lines.push_back(effectLine);
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
    lines.push_back(std::format("M MAP  F2 COLLISION VIEW ({})  F8 RESET PROGRESS{}", showCollision_ ? "ON" : "OFF",
                                savePath_.empty() ? " (NOT SAVED)" : ""));
}

void SandboxGame::onToolsPass(animal::EngineContext& context, animal::rhi::CommandList& commands) {
    dev_.render(context, commands);
}

void SandboxGame::onShutdown(animal::EngineContext& context) {
    if (!expectations_.empty() && !checkExpectations()) {
        context.exitCode = 3;
    }
    saveProgress(); // play time and visited rooms; the position stays the last checkpoint
    dev_.shutdown(context);
    animal::destroyMaterial(*context.device, assets_.tileset);
    animal::destroyMaterial(*context.device, assets_.props);
    animal::destroyMaterial(*context.device, creature_);
    animal::destroyMaterial(*context.device, assets_.crawler);
}

} // namespace game
