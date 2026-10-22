#include "sandbox_game.h"

#include "room_layout.h"

#include <animal/platform/input.h>
#include <animal/renderer/renderer2d.h>
#include <animal/rhi/rhi.h>

#include <algorithm>
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

// Grass colors per [shade][level] (base → tip); vine stem and leaf colors.
constexpr animal::Color kGrassColors[3][3] = {
    {animal::Color::rgba(46, 104, 52), animal::Color::rgba(76, 150, 70), animal::Color::rgba(136, 198, 92)},
    {animal::Color::rgba(40, 92, 58), animal::Color::rgba(64, 136, 76), animal::Color::rgba(110, 182, 96)},
    {animal::Color::rgba(56, 112, 46), animal::Color::rgba(96, 160, 64), animal::Color::rgba(160, 208, 96)},
};
constexpr animal::Color kVineStem = animal::Color::rgba(70, 140, 72);
constexpr animal::Color kVineLeaf = animal::Color::rgba(120, 196, 96);
constexpr uint32_t kFoliageSeed = 0xA11CE;
constexpr int kVineSegment = 3;      // px, matches VineSystem::Params::segmentLength
constexpr int kVineClearance = 3;    // px left between a vine tip and the ground
constexpr int kMaxVineSegments = 50;

constexpr size_t kDustCount = 70;
constexpr size_t kFireflyCount = 14;
constexpr float kEmbersPerSecond = 2.2f; // per candle
constexpr animal::Color kDustColor = animal::Color::rgba(205, 195, 175, 170);
constexpr animal::Color kFireflyColor = animal::Color::rgba(210, 255, 130, 255);
constexpr animal::Vec3 kFireflyLight{0.75f, 1.0f, 0.45f};

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
    room_ = std::move(room->map);
    grass_.generate(room_, [](animal::TileId id) { return id == TileGround; }, kFoliageSeed);
    for (const animal::Vec2i cell : room->vineAnchors) {
        // Hang down to just above the first solid tile below the anchor.
        int floorRow = cell.y;
        while (floorRow < room_.height() && !room_.isSolid(cell.x, floorRow)) {
            ++floorRow;
        }
        const int drop = (floorRow - cell.y) * kTileSize - kVineClearance;
        const int segments = std::clamp(drop / kVineSegment, 2, kMaxVineSegments);
        vines_.addVine({cell.x * kTileSize + 4.0f, static_cast<float>(cell.y * kTileSize)}, segments);
    }
    tileset_ = *tileset;
    creature_ = *creature;
    props_ = *props;

    float phase = 0.0f;
    for (const auto& spot : kCandleSpots) {
        candles_.push_back({{static_cast<float>(spot.tileX * kTileSize), static_cast<float>((spot.surfaceRow - 1) * kTileSize)},
                            phase});
        phase += 1.7f;
    }

    for (size_t i = 0; i < kDustCount; ++i) {
        spawnDust(true);
    }
    for (size_t i = 0; i < kFireflyCount; ++i) {
        spawnFirefly();
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

// --demo: run right through the grass and two low vines, jump mid-run, then stop and let
// everything settle. Driven by simulation time, so --deterministic captures are reproducible.
void SandboxGame::applyDemoInput() {
    actions_.applyStrength(MoveRight, time_ < 1.6 ? 1.0f : 0.0f);
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

    std::vector<animal::Influence> influences;
    if (player) {
        animal::Influence influence{player->body.worldCollider(), playerState_.velocity, false};
        if (playerState_.landedThisStep) {
            influence.landed = true;
            influence.velocity.y = playerState_.landingSpeed;
        }
        influences.push_back(influence);
    }
    grass_.step(static_cast<float>(dt), time_, influences, wind_);
    vines_.step(static_cast<float>(dt), time_, influences, wind_);
    updateParticles(static_cast<float>(dt), influences);
    actions_.endFixedStep();
    entities_.flushDestroyed();
}

animal::Vec2 SandboxGame::randomOpenPosition() {
    const animal::IntRect bounds = room_.pixelBounds();
    animal::Vec2 p;
    for (int attempt = 0; attempt < 20; ++attempt) {
        p = {rng_.range(0.0f, static_cast<float>(bounds.width)), rng_.range(0.0f, static_cast<float>(bounds.height))};
        if (!room_.isSolid(static_cast<int>(p.x) / kTileSize, static_cast<int>(p.y) / kTileSize)) {
            break;
        }
    }
    return p;
}

// Dust only shows where light falls (drawn in the lit pass): motes floating in candle light.
void SandboxGame::spawnDust(bool midLife) {
    animal::Particle p;
    p.kind = Dust;
    p.position = randomOpenPosition();
    p.velocity = {rng_.range(-3.0f, 3.0f), rng_.range(-2.0f, 2.0f)};
    p.acceleration = {0.0f, rng_.range(-1.5f, 1.5f)};
    p.drag = 0.6f;
    p.windResponse = 6.0f;
    p.wander = 4.0f;
    p.lifetime = rng_.range(8.0f, 16.0f);
    p.age = midLife ? rng_.range(0.0f, p.lifetime * 0.8f) : 0.0f;
    p.fade = 2.0f;
    p.startColor = p.endColor = kDustColor;
    p.seed = rng_.next();
    particles_.emit(p);
}

void SandboxGame::spawnFirefly() {
    animal::Particle p;
    p.kind = Firefly;
    p.position = randomOpenPosition();
    p.drag = 1.6f;
    p.wander = 45.0f;
    p.pushResponse = 1.5f; // scatter when the player comes close
    p.flicker = rng_.range(0.4f, 0.9f);
    p.lifetime = 1.0e6f;   // until it drifts out of the room
    p.fade = 1.0f;
    p.startColor = p.endColor = kFireflyColor;
    p.seed = rng_.next();
    particles_.emit(p);
}

void SandboxGame::spawnEmber(const Candle& candle) {
    animal::Particle p;
    p.kind = Ember;
    p.position = candle.position + animal::Vec2{3.5f + rng_.range(-0.5f, 0.5f), 1.0f};
    p.velocity = {rng_.range(-8.0f, 8.0f), rng_.range(-28.0f, -16.0f)};
    p.acceleration = {0.0f, -14.0f}; // buoyancy
    p.drag = 0.9f;
    p.windResponse = 18.0f;
    p.wander = 10.0f;
    p.pushResponse = 0.6f;
    p.lifetime = rng_.range(0.7f, 1.5f);
    p.fade = 0.1f;
    p.startColor = animal::Color::rgba(255, 225, 140, 255);
    p.endColor = animal::Color::rgba(210, 60, 20, 0); // cools and fades
    p.dieOnSolid = true;
    p.seed = rng_.next();
    particles_.emit(p);
}

void SandboxGame::updateParticles(float dt, std::span<const animal::Influence> influences) {
    for (Candle& candle : candles_) {
        candle.emberBudget += kEmbersPerSecond * dt;
        while (candle.emberBudget >= 1.0f) {
            spawnEmber(candle);
            candle.emberBudget -= 1.0f;
        }
    }
    particles_.step(dt, time_, &room_, influences, wind_);
    for (size_t n = particles_.countOf(Dust); n < kDustCount; ++n) {
        spawnDust(false);
    }
    for (size_t n = particles_.countOf(Firefly); n < kFireflyCount; ++n) {
        spawnFirefly();
    }
}

void SandboxGame::drawParticles(animal::Renderer2D& renderer) const {
    for (const animal::Particle& p : particles_.particles()) {
        const animal::Color c = particles_.colorOf(p);
        switch (p.kind) {
        case Dust:
            renderer.fillRect({std::round(p.position.x), std::round(p.position.y), 1.0f, 1.0f}, c);
            break;
        case Firefly: {
            renderer.drawGlow(p.position, c);
            const animal::Color halo{c.r, c.g, c.b, static_cast<uint8_t>(c.a * 0.3f)};
            for (const animal::Vec2 d : {animal::Vec2{1, 0}, animal::Vec2{-1, 0}, animal::Vec2{0, 1}, animal::Vec2{0, -1}}) {
                renderer.drawGlow(p.position + d, halo);
            }
            break;
        }
        case Ember:
            renderer.drawGlow(p.position, c);
            break;
        default:
            break;
        }
    }
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

    for (const animal::Particle& p : particles_.particles()) {
        if (p.kind == Firefly) {
            animal::PointLight light;
            light.position = p.position;
            light.radius = 18.0f;
            light.color = kFireflyLight;
            light.intensity = 0.7f * static_cast<float>(particles_.colorOf(p).a) / 255.0f;
            light.height = 6.0f;
            renderer.addLight(light);
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
    drawVines(renderer);

    const animal::Entity* player = entities_.get(player_);
    if (player) {
        const animal::Vec2i p = player->body.position + kSpriteOffset;
        renderer.drawSprite(creature_, {static_cast<float>(p.x), static_cast<float>(p.y)}, {0.0f, 0.0f, size, size},
                            animal::Color::white(), facingLeft_);
    }
    drawGrass(renderer); // in front of the player: it wades through
    drawParticles(renderer);
    addLights(renderer);
    renderer.endWorld();

    if (showCollision_ && player) {
        renderer.beginWorldOverlay(camera_);
        drawCollisionDebug(renderer, *player);
        renderer.endWorldOverlay();
    }
}

void SandboxGame::drawGrass(animal::Renderer2D& renderer) const {
    grass_.forEachPixel([&](int x, int y, int level, uint8_t shade) {
        renderer.fillRect({static_cast<float>(x), static_cast<float>(y), 1.0f, 1.0f}, kGrassColors[shade][level]);
    });
}

void SandboxGame::drawVines(animal::Renderer2D& renderer) const {
    vines_.forEachPixel([&](int x, int y, bool leaf) {
        renderer.fillRect({static_cast<float>(x), static_cast<float>(y), 1.0f, 1.0f}, leaf ? kVineLeaf : kVineStem);
    });
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
    lines.push_back(std::format("GRASS {} BLADES  VINES {}  PARTICLES {}", grass_.blades().size(),
                                vines_.vines().size(), particles_.size()));
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
