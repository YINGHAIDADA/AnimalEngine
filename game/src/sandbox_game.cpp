#include "sandbox_game.h"

#include "room_layout.h"

#include <animal/platform/input.h>
#include <animal/renderer/renderer2d.h>
#include <animal/rhi/rhi.h>

#include <format>

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
constexpr animal::Vec2i kPlayerSpawn{3 * kTileSize, 20 * kTileSize - 7};

} // namespace

animal::AppConfig SandboxGame::configure() {
    animal::AppConfig config;
    config.title = "Animal Engine · M1a Sandbox";
    return config;
}

void SandboxGame::bindActions() {
    actions_.bind(MoveLeft, {{Key::Left, Key::A}, {GamepadButton::DpadLeft}, {{GamepadAxis::LeftX, -1.0f}}});
    actions_.bind(MoveRight, {{Key::Right, Key::D}, {GamepadButton::DpadRight}, {{GamepadAxis::LeftX, 1.0f}}});
    actions_.bind(MoveUp, {{Key::Up, Key::W}, {GamepadButton::DpadUp}, {{GamepadAxis::LeftY, -1.0f}}});
    actions_.bind(MoveDown, {{Key::Down, Key::S}, {GamepadButton::DpadDown}, {{GamepadAxis::LeftY, 1.0f}}});
    actions_.bind(Jump, {{Key::Space, Key::Z}, {GamepadButton::South}, {}});
}

bool SandboxGame::onInit(animal::EngineContext& context) {
    auto room = buildTestRoom();
    const auto tileset = animal::loadTexture(*context.device, context.dataDirectory + "textures/tileset.png");
    const auto creature = animal::loadTexture(*context.device, context.dataDirectory + "textures/creature.png");
    if (!room || !tileset || !creature) {
        return false;
    }
    room_ = std::move(*room);
    tileset_ = *tileset;
    creature_ = *creature;

    player_ = entities_.create(animal::EntityKind::Player);
    animal::Entity* player = entities_.get(player_);
    player->body.position = kPlayerSpawn;
    player->body.collider = kPlayerCollider;

    camera_.center = kCameraCenter;
    bindActions();
    return true;
}

void SandboxGame::onFrame(animal::EngineContext& context) {
    const animal::platform::Input& input = *context.input;
    actions_.update(input);
    if (input.pressed(Key::F2)) {
        showCollision_ = !showCollision_;
    }
    if (input.gamepadConnected() && (input.down(GamepadButton::South) || input.axis(GamepadAxis::LeftX) != 0.0f)) {
        usingGamepad_ = true;
    } else if (input.down(Key::Left) || input.down(Key::Right) || input.down(Key::Space) || input.down(Key::Z)) {
        usingGamepad_ = false;
    }
}

void SandboxGame::onFixedUpdate(animal::EngineContext&, double dt) {
    animal::Entity* player = entities_.get(player_);
    if (player) {
        animal::PlatformerInput input;
        input.moveX = actions_.strength(MoveRight) - actions_.strength(MoveLeft);
        input.jumpPressed = actions_.pressed(Jump);
        input.jumpHeld = actions_.down(Jump);
        animal::stepPlatformer(player->body, playerState_, input, playerParams_, room_, static_cast<float>(dt));

        if (input.moveX < 0.0f) {
            facingLeft_ = true;
        } else if (input.moveX > 0.0f) {
            facingLeft_ = false;
        }
        // Fell out of the room (no exits yet): respawn.
        if (player->body.position.y > room_.pixelBounds().bottom() + 64) {
            player->body.position = kPlayerSpawn;
            playerState_ = {};
        }
    }
    actions_.endFixedStep();
    entities_.flushDestroyed();
}

void SandboxGame::onRender(animal::EngineContext&, animal::Renderer2D& renderer) {
    renderer.beginWorld(camera_, animal::Color::rgba(12, 10, 20));

    const float size = static_cast<float>(kTileSize);
    for (int ty = 0; ty < room_.height(); ++ty) {
        for (int tx = 0; tx < room_.width(); ++tx) {
            const animal::TileId tile = room_.tile(tx, ty);
            if (tile == TileEmpty) {
                continue;
            }
            renderer.drawSprite(tileset_, {tx * size, ty * size}, {tile * size, 0.0f, size, size});
        }
    }

    if (const animal::Entity* player = entities_.get(player_)) {
        const animal::Vec2i p = player->body.position + kSpriteOffset;
        renderer.drawSprite(creature_, {static_cast<float>(p.x), static_cast<float>(p.y)}, {0.0f, 0.0f, size, size},
                            animal::Color::white(), facingLeft_);
        if (showCollision_) {
            drawCollisionDebug(renderer, *player);
        }
    }
    renderer.endWorld();
}

void SandboxGame::drawCollisionDebug(animal::Renderer2D& renderer, const animal::Entity& player) {
    const float size = static_cast<float>(kTileSize);
    for (int ty = 0; ty < room_.height(); ++ty) {
        for (int tx = 0; tx < room_.width(); ++tx) {
            if (room_.isSolid(tx, ty)) {
                renderer.fillRect({tx * size, ty * size, size, size}, animal::Color::rgba(255, 60, 60, 50));
            }
        }
    }
    const animal::IntRect c = player.body.worldCollider();
    const animal::Color color =
        playerState_.grounded ? animal::Color::rgba(80, 255, 120) : animal::Color::rgba(255, 220, 60);
    renderer.strokeRect({static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.width),
                         static_cast<float>(c.height)},
                        color);
}

void SandboxGame::onDebugText(animal::EngineContext&, std::vector<std::string>& lines) {
    if (const animal::Entity* player = entities_.get(player_)) {
        const animal::PlatformerState& s = playerState_;
        lines.push_back(std::format("PLAYER {},{}  VEL {:.0f},{:.0f}", player->body.position.x,
                                    player->body.position.y, s.velocity.x, s.velocity.y));
        lines.push_back(std::format("GROUNDED {}  COYOTE {}  BUFFER {}", s.grounded ? "YES" : "NO", s.coyoteTimer,
                                    s.jumpBufferTimer));
    }
    lines.push_back(usingGamepad_ ? "GAMEPAD: STICK/DPAD MOVE  A JUMP" : "ARROWS/WASD MOVE  SPACE/Z JUMP");
    lines.push_back(std::format("F2 COLLISION VIEW ({})", showCollision_ ? "ON" : "OFF"));
}

void SandboxGame::onShutdown(animal::EngineContext& context) {
    context.device->destroy(tileset_.handle);
    context.device->destroy(creature_.handle);
}

} // namespace game
