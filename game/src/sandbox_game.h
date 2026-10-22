#pragma once

#include <animal/app/application.h>
#include <animal/framework/action_map.h>
#include <animal/framework/entity.h>
#include <animal/framework/platformer.h>
#include <animal/framework/tilemap.h>
#include <animal/renderer/camera2d.h>
#include <animal/resources/loaders.h>

namespace game {

enum Action : animal::ActionId {
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    Jump,
};

// M1a sandbox: one single-screen room and a platformer-controlled creature.
class SandboxGame final : public animal::Application {
protected:
    animal::AppConfig configure() override;
    bool onInit(animal::EngineContext& context) override;
    void onFrame(animal::EngineContext& context) override;
    void onFixedUpdate(animal::EngineContext& context, double dt) override;
    void onRender(animal::EngineContext& context, animal::Renderer2D& renderer) override;
    void onDebugText(animal::EngineContext& context, std::vector<std::string>& lines) override;
    void onShutdown(animal::EngineContext& context) override;

private:
    void bindActions();
    void drawCollisionDebug(animal::Renderer2D& renderer, const animal::Entity& player);

    animal::TileMap room_;
    animal::EntityWorld entities_;
    animal::EntityId player_;
    animal::PlatformerParams playerParams_;
    animal::PlatformerState playerState_;
    bool facingLeft_ = false;

    animal::ActionMap actions_;
    bool showCollision_ = false;
    bool usingGamepad_ = false;

    animal::Camera2D camera_;
    animal::LoadedTexture tileset_;
    animal::LoadedTexture creature_;
};

} // namespace game
