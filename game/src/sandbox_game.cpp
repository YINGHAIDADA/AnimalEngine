#include "sandbox_game.h"

#include "room_layout.h"

#include <animal/platform/input.h>
#include <animal/renderer/renderer2d.h>
#include <animal/rhi/rhi.h>

#include <cmath>
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
constexpr animal::Vec2i kPlayerSpawn{3 * kTileSize, 20 * kTileSize - 7};

// F4 cycles through these light quantization settings (0 = smooth).
constexpr int kBandPresets[] = {0, 3, 5, 8};

constexpr animal::Color kBackdrop = animal::Color::rgba(12, 10, 20);
constexpr animal::Vec3 kAmbient{0.07f, 0.07f, 0.11f};
constexpr animal::Vec3 kCandleColor{1.0f, 0.62f, 0.28f};
constexpr animal::Vec3 kCrystalColor{0.35f, 0.8f, 1.0f};
constexpr animal::Vec3 kPlayerGlowColor{0.85f, 0.85f, 1.0f};

// Candles stand on platform tops: x in tiles, y = top row of the platform.
constexpr struct {
    int tileX;
    int surfaceRow;
} kCandleSpots[] = {{8, 20}, {21, 20}, {13, 11}, {28, 5}, {36, 17}, {20, 8}};

float flicker(double time, float phase) {
    const auto t = static_cast<float>(time);
    return 1.0f + 0.08f * std::sin(t * 13.0f + phase) + 0.05f * std::sin(t * 29.0f + phase * 2.3f);
}

} // namespace

animal::AppConfig SandboxGame::configure() {
    animal::AppConfig config;
    config.title = "Animal Engine · V1 Sandbox";
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
    const std::string textures = context.dataDirectory + "textures/";
    auto tileset = animal::loadMaterial(*context.device, textures + "tileset");
    auto creature = animal::loadMaterial(*context.device, textures + "creature");
    auto props = animal::loadMaterial(*context.device, textures + "props");
    if (!room || !tileset || !creature || !props) {
        return false;
    }
    room_ = std::move(*room);
    tileset_ = *tileset;
    creature_ = *creature;
    props_ = *props;

    float phase = 0.0f;
    for (const auto& spot : kCandleSpots) {
        candles_.push_back({{static_cast<float>(spot.tileX * kTileSize), static_cast<float>((spot.surfaceRow - 1) * kTileSize)},
                            phase});
        phase += 1.7f;
    }

    player_ = entities_.create(animal::EntityKind::Player);
    animal::Entity* player = entities_.get(player_);
    player->body.position = kPlayerSpawn;
    player->body.collider = kPlayerCollider;

    camera_.center = kCameraCenter;
    bindActions();

    animal::LightingSettings& lighting = context.renderer->lighting();
    lighting.ambient = kAmbient;
    lighting.bands = kBandPresets[bandsPreset_];
    return true;
}

void SandboxGame::onFrame(animal::EngineContext& context) {
    const animal::platform::Input& input = *context.input;
    actions_.update(input);
    animal::LightingSettings& lighting = context.renderer->lighting();
    if (input.pressed(Key::F2)) {
        showCollision_ = !showCollision_;
    }
    if (input.pressed(Key::F3)) {
        lighting.enabled = !lighting.enabled;
    }
    if (input.pressed(Key::F4)) {
        bandsPreset_ = (bandsPreset_ + 1) % std::size(kBandPresets);
        lighting.bands = kBandPresets[bandsPreset_];
    }
    if (input.gamepadConnected() && (input.down(GamepadButton::South) || input.axis(GamepadAxis::LeftX) != 0.0f)) {
        usingGamepad_ = true;
    } else if (input.down(Key::Left) || input.down(Key::Right) || input.down(Key::Space) || input.down(Key::Z)) {
        usingGamepad_ = false;
    }
}

void SandboxGame::onFixedUpdate(animal::EngineContext&, double dt) {
    time_ += dt;
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

void SandboxGame::addLights(animal::Renderer2D& renderer) {
    for (const Candle& candle : candles_) {
        animal::PointLight light;
        light.position = candle.position + animal::Vec2{3.5f, 1.5f}; // the flame
        light.radius = 60.0f;
        light.color = kCandleColor;
        light.intensity = 1.5f * flicker(time_, candle.phase);
        light.height = 10.0f;
        renderer.addLight(light);
    }

    const float size = static_cast<float>(kTileSize);
    for (int ty = 0; ty < room_.height(); ++ty) {
        for (int tx = 0; tx < room_.width(); ++tx) {
            if (room_.tile(tx, ty) == TileCrystal) {
                animal::PointLight light;
                light.position = {tx * size + 4.0f, ty * size + 4.0f};
                light.radius = 44.0f;
                light.color = kCrystalColor;
                light.intensity = 1.1f + 0.15f * static_cast<float>(std::sin(time_ * 1.3 + tx));
                light.height = 12.0f;
                renderer.addLight(light);
            }
        }
    }

    if (const animal::Entity* player = entities_.get(player_)) {
        const animal::Vec2 center = player->body.exactPosition() + animal::Vec2{3.0f, 3.0f};
        animal::PointLight glow;
        glow.position = center;
        glow.radius = 30.0f;
        glow.color = kPlayerGlowColor;
        glow.intensity = 0.55f;
        glow.height = 14.0f;
        renderer.addLight(glow);
    }
}

void SandboxGame::onRender(animal::EngineContext&, animal::Renderer2D& renderer) {
    renderer.beginWorld(camera_, kBackdrop);

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
    for (const Candle& candle : candles_) {
        renderer.drawSprite(props_, candle.position, {0.0f, 0.0f, size, size});
    }

    const animal::Entity* player = entities_.get(player_);
    if (player) {
        const animal::Vec2i p = player->body.position + kSpriteOffset;
        renderer.drawSprite(creature_, {static_cast<float>(p.x), static_cast<float>(p.y)}, {0.0f, 0.0f, size, size},
                            animal::Color::white(), facingLeft_);
    }
    addLights(renderer);
    renderer.endWorld();

    if (showCollision_ && player) {
        renderer.beginWorldOverlay(camera_);
        drawCollisionDebug(renderer, *player);
        renderer.endWorldOverlay();
    }
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

void SandboxGame::onDebugText(animal::EngineContext& context, std::vector<std::string>& lines) {
    if (const animal::Entity* player = entities_.get(player_)) {
        const animal::PlatformerState& s = playerState_;
        lines.push_back(std::format("PLAYER {},{}  VEL {:.0f},{:.0f}", player->body.position.x,
                                    player->body.position.y, s.velocity.x, s.velocity.y));
        lines.push_back(std::format("GROUNDED {}  COYOTE {}  BUFFER {}", s.grounded ? "YES" : "NO", s.coyoteTimer,
                                    s.jumpBufferTimer));
    }
    const animal::LightingSettings& lighting = context.renderer->lighting();
    lines.push_back(std::format("F3 LIGHTING {}  F4 BANDS {}", lighting.enabled ? "ON" : "OFF",
                                lighting.bands == 0 ? std::string("SMOOTH") : std::to_string(lighting.bands)));
    lines.push_back(usingGamepad_ ? "GAMEPAD: STICK/DPAD MOVE  A JUMP" : "ARROWS/WASD MOVE  SPACE/Z JUMP");
    lines.push_back(std::format("F2 COLLISION VIEW ({})", showCollision_ ? "ON" : "OFF"));
}

void SandboxGame::onShutdown(animal::EngineContext& context) {
    animal::destroyMaterial(*context.device, tileset_);
    animal::destroyMaterial(*context.device, creature_);
    animal::destroyMaterial(*context.device, props_);
}

} // namespace game
