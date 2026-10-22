#include "sandbox_game.h"

#include <animal/platform/input.h>
#include <animal/renderer/renderer2d.h>
#include <animal/rhi/rhi.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace game {

namespace {

constexpr float kPlayerSpeed = 96.0f; // pixels per second
constexpr float kPlayerSize = 16.0f;
constexpr auto kCanvas = animal::Renderer2D::kCanvasExtent;

} // namespace

animal::AppConfig SandboxGame::configure() {
    animal::AppConfig config;
    config.title = "Animal Engine · M0 Sandbox";
    return config;
}

bool SandboxGame::onInit(animal::EngineContext& context) {
    if (!room_.load()) {
        return false;
    }
    const auto tileset = animal::loadTexture(*context.device, context.dataDirectory + "textures/tileset.png");
    const auto creature = animal::loadTexture(*context.device, context.dataDirectory + "textures/creature.png");
    if (!tileset || !creature) {
        return false;
    }
    tileset_ = *tileset;
    creature_ = *creature;

    playerPosition_ = {3.0f * TestRoom::kTileSize, 8.0f * TestRoom::kTileSize};
    updateCamera();
    return true;
}

void SandboxGame::onFixedUpdate(animal::EngineContext& context, double dt) {
    using animal::platform::Key;
    const animal::platform::Input& input = *context.input;

    animal::Vec2 direction;
    if (input.down(Key::Left) || input.down(Key::A)) {
        direction.x -= 1.0f;
    }
    if (input.down(Key::Right) || input.down(Key::D)) {
        direction.x += 1.0f;
    }
    if (input.down(Key::Up) || input.down(Key::W)) {
        direction.y -= 1.0f;
    }
    if (input.down(Key::Down) || input.down(Key::S)) {
        direction.y += 1.0f;
    }
    const float len = animal::length(direction);
    if (len > 0.0f) {
        playerPosition_ += direction * (kPlayerSpeed * static_cast<float>(dt) / len);
    }

    const float border = TestRoom::kTileSize;
    playerPosition_.x = std::clamp(playerPosition_.x, border, room_.pixelWidth() - border - kPlayerSize);
    playerPosition_.y = std::clamp(playerPosition_.y, border, room_.pixelHeight() - border - kPlayerSize);
    updateCamera();
}

void SandboxGame::updateCamera() {
    const float halfW = kCanvas.width / 2.0f;
    const float halfH = kCanvas.height / 2.0f;
    const animal::Vec2 target = playerPosition_ + animal::Vec2{kPlayerSize / 2.0f, kPlayerSize / 2.0f};
    camera_.center.x = std::clamp(target.x, halfW, std::max(halfW, room_.pixelWidth() - halfW));
    camera_.center.y = std::clamp(target.y, halfH, std::max(halfH, room_.pixelHeight() - halfH));
}

void SandboxGame::onRender(animal::EngineContext&, animal::Renderer2D& renderer) {
    renderer.beginWorld(camera_, animal::Color::rgba(18, 16, 28));

    const animal::Vec2 origin = camera_.topLeft(kCanvas);
    const int size = TestRoom::kTileSize;
    const int x0 = std::max(0, static_cast<int>(std::floor(origin.x / size)));
    const int y0 = std::max(0, static_cast<int>(std::floor(origin.y / size)));
    const int x1 = std::min(room_.width(), static_cast<int>(std::ceil((origin.x + kCanvas.width) / size)) + 1);
    const int y1 = std::min(room_.height(), static_cast<int>(std::ceil((origin.y + kCanvas.height) / size)) + 1);

    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const TestRoom::Tile tile = room_.at(x, y);
            if (tile == TestRoom::Empty) {
                continue;
            }
            const animal::RectF source{static_cast<float>(tile * size), 0.0f, static_cast<float>(size),
                                       static_cast<float>(size)};
            renderer.drawSprite(tileset_, {static_cast<float>(x * size), static_cast<float>(y * size)}, source);
        }
    }

    renderer.drawSprite(creature_, playerPosition_, {0.0f, 0.0f, kPlayerSize, kPlayerSize});
    renderer.endWorld();
}

void SandboxGame::onDebugText(animal::EngineContext&, std::vector<std::string>& lines) {
    lines.push_back(std::format("PLAYER {:.0f},{:.0f}  ROOM {}X{} TILES", playerPosition_.x, playerPosition_.y,
                                room_.width(), room_.height()));
    lines.push_back("ARROWS / WASD MOVE");
}

void SandboxGame::onShutdown(animal::EngineContext& context) {
    context.device->destroy(tileset_.handle);
    context.device->destroy(creature_.handle);
}

} // namespace game
