#include "sandbox_game.h"

#include <animal/core/log.h>
#include <animal/platform/input.h>
#include <animal/renderer/renderer2d.h>
#include <animal/rhi/rhi.h>

#include <cmath>
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

// F4 cycles through these light quantization settings (0 = smooth).
constexpr int kBandPresets[] = {0, 3, 5, 8};
constexpr animal::Color kBackdrop = animal::Color::rgba(12, 10, 20);
constexpr animal::Vec3 kPlayerGlowColor{0.85f, 0.85f, 1.0f};
constexpr uint32_t kRoomSeed = 0xA11CE;

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
    if (!room || !cave_.build(*room, kRoomSeed + roomVisits_++)) {
        return false;
    }
    currentRoom_ = &worldRoom;
    renderer.resetFluid();
    renderer.setFluidObstacles(cave_.fluidObstacles(camera_.topLeft(animal::Renderer2D::kCanvasExtent)));
    renderer.lighting().ambient = cave_.ambient();
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
    auto world = animal::loadWorld(context.dataDirectory + "world/test.world");
    if (!tileset || !props || !creature || !world) {
        return false;
    }
    assets_ = {*tileset, *props};
    creature_ = *creature;
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
    }
    if (!startRoom_) {
        animal::log::error("No room in the world has a 'spawn' object");
        return false;
    }

    camera_.center = kCameraCenter;
    player_ = entities_.create(animal::EntityKind::Player);
    entities_.get(player_)->body.collider = kPlayerCollider;
    if (!enterRoom(*startRoom_, *context.renderer)) {
        return false;
    }
    placePlayerFeet(startFeet_);
    bindActions();

    animal::Renderer2D& renderer = *context.renderer;
    renderer.lighting().bands = kBandPresets[bandsPreset_];
    animal::FluidSettings& fluid = renderer.fluid();
    fluid.smokeKeepPerSecond = 0.8f;
    fluid.velocityKeepPerSecond = 0.7f;
    fluid.buoyancy = 45.0f;

    // Cave grade: cool shadows, warm highlights, soft vignette; bloom around the light sources.
    animal::PostSettings& post = renderer.post();
    post.bloomThreshold = 0.55f;
    post.bloomIntensity = 1.0f;
    post.exposure = 1.2f; // the scene is dark; contrast below would otherwise crush it
    post.contrast = 1.04f;
    post.saturation = 1.12f;
    post.shadowTint = {0.9f, 0.95f, 1.12f};
    post.highlightTint = {1.08f, 1.0f, 0.9f};
    post.vignette = 0.25f;
    return true;
}

// --demo: a short tour driven by simulation time (reproducible with --deterministic): run right
// through the grass and vines of the entrance (jumping once), along the tunnel into the crystal
// gallery, and drop through its pit into the smoke pit.
void SandboxGame::applyDemoInput() {
    actions_.applyStrength(MoveRight, time_ < 7.5 ? 1.0f : 0.0f);
    actions_.applyStrength(MoveLeft, 0.0f);
    actions_.applyStrength(Jump, time_ >= 0.9 && time_ < 1.2 ? 1.0f : 0.0f);
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
        bandsPreset_ = (bandsPreset_ + 1) % std::size(kBandPresets);
        renderer.lighting().bands = kBandPresets[bandsPreset_];
    }
    if (input.pressed(Key::F5)) {
        renderer.fluid().enabled = !renderer.fluid().enabled;
    }
    if (input.pressed(Key::F6)) {
        renderer.post().enabled = !renderer.post().enabled;
    }
    if (input.gamepadConnected() && (input.down(GamepadButton::South) || input.axis(GamepadAxis::LeftX) != 0.0f)) {
        usingGamepad_ = true;
    } else if (input.down(Key::Left) || input.down(Key::Right) || input.down(Key::Space) || input.down(Key::Z)) {
        usingGamepad_ = false;
    }
}

void SandboxGame::respawn(animal::Renderer2D& renderer) {
    if (currentRoom_ != startRoom_) {
        enterRoom(*startRoom_, renderer);
    }
    placePlayerFeet(startFeet_);
    playerState_ = {};
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
    animal::Entity* player = entities_.get(player_);
    if (player) {
        animal::PlatformerInput input;
        input.moveX = actions_.strength(MoveRight) - actions_.strength(MoveLeft);
        input.jumpPressed = actions_.pressed(Jump);
        input.jumpHeld = actions_.down(Jump);
        animal::stepPlatformer(player->body, playerState_, input, playerParams_, cave_.tiles(),
                               static_cast<float>(dt));
        if (input.moveX < 0.0f) {
            facingLeft_ = true;
        } else if (input.moveX > 0.0f) {
            facingLeft_ = false;
        }
        handleRoomExit(*context.renderer);
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
    renderer.beginWorld(camera_, kBackdrop);
    cave_.drawBehindPlayer(renderer, assets_);

    const animal::Entity* player = entities_.get(player_);
    if (player) {
        const float size = static_cast<float>(kTileSize);
        const animal::Vec2i p = player->body.position + kSpriteOffset;
        renderer.drawSprite(creature_, {static_cast<float>(p.x), static_cast<float>(p.y)}, {0.0f, 0.0f, size, size},
                            animal::Color::white(), facingLeft_);
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

void SandboxGame::onDebugText(animal::EngineContext& context, std::vector<std::string>& lines) {
    if (currentRoom_) {
        lines.push_back(std::format("ROOM {} ({} IN WORLD)", roomName(currentRoom_->path), world_.rooms.size()));
    }
    if (const animal::Entity* player = entities_.get(player_)) {
        const animal::PlatformerState& s = playerState_;
        lines.push_back(std::format("PLAYER {},{}  VEL {:.0f},{:.0f}  GROUNDED {}", player->body.position.x,
                                    player->body.position.y, s.velocity.x, s.velocity.y, s.grounded ? "YES" : "NO"));
    }
    lines.push_back(std::format("GRASS {}  VINES {}  PARTICLES {}", cave_.bladeCount(), cave_.vineCount(),
                                cave_.particleCount()));
    animal::Renderer2D& renderer = *context.renderer;
    lines.push_back(std::format("F3 LIGHT {}  F4 BANDS {}  F5 SMOKE {}  F6 POST {}",
                                renderer.lighting().enabled ? "ON" : "OFF",
                                renderer.lighting().bands == 0 ? std::string("SMOOTH")
                                                               : std::to_string(renderer.lighting().bands),
                                renderer.fluid().enabled ? "ON" : "OFF", renderer.post().enabled ? "ON" : "OFF"));
    lines.push_back(usingGamepad_ ? "GAMEPAD: STICK/DPAD MOVE  A JUMP" : "ARROWS/WASD MOVE  SPACE/Z JUMP");
    lines.push_back(std::format("F2 COLLISION VIEW ({})", showCollision_ ? "ON" : "OFF"));
}

void SandboxGame::onShutdown(animal::EngineContext& context) {
    animal::destroyMaterial(*context.device, assets_.tileset);
    animal::destroyMaterial(*context.device, assets_.props);
    animal::destroyMaterial(*context.device, creature_);
}

} // namespace game
