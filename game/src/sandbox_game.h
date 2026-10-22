#pragma once

#include "test_room.h"

#include <animal/app/application.h>
#include <animal/renderer/camera2d.h>
#include <animal/resources/loaders.h>

namespace game {

// M0 sandbox: one room, a freely movable placeholder creature, a following camera.
class SandboxGame final : public animal::Application {
protected:
    animal::AppConfig configure() override;
    bool onInit(animal::EngineContext& context) override;
    void onFixedUpdate(animal::EngineContext& context, double dt) override;
    void onRender(animal::EngineContext& context, animal::Renderer2D& renderer) override;
    void onDebugText(animal::EngineContext& context, std::vector<std::string>& lines) override;
    void onShutdown(animal::EngineContext& context) override;

private:
    void updateCamera();

    TestRoom room_;
    animal::LoadedTexture tileset_;
    animal::LoadedTexture creature_;
    animal::Vec2 playerPosition_;
    animal::Camera2D camera_;
};

} // namespace game
