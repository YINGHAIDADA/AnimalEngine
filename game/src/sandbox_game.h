#pragma once

#include "cave_room.h"

#include <animal/app/application.h>
#include <animal/framework/action_map.h>
#include <animal/framework/entity.h>
#include <animal/framework/level.h>
#include <animal/framework/platformer.h>
#include <animal/renderer/camera2d.h>
#include <animal/resources/loaders.h>

#include <map>
#include <string>
#include <vector>

namespace animal {
class Renderer2D;
}

namespace game {

enum Action : animal::ActionId {
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    Jump,
};

// M1b sandbox: a Tiled world of single-screen rooms, a platformer-controlled creature, and the
// V1 visual systems in every room.
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
    void applyDemoInput();
    const animal::Room* roomData(const animal::WorldRoom& worldRoom);
    bool enterRoom(const animal::WorldRoom& worldRoom, animal::Renderer2D& renderer);
    void placePlayerFeet(animal::Vec2 feet);
    void handleRoomExit(animal::Renderer2D& renderer);
    void respawn(animal::Renderer2D& renderer);

    animal::World world_;
    std::map<std::string, animal::Room> rooms_; // loaded room data by path
    const animal::WorldRoom* currentRoom_ = nullptr;
    const animal::WorldRoom* startRoom_ = nullptr;
    animal::Vec2 startFeet_;
    CaveRoom cave_;
    uint32_t roomVisits_ = 0;

    animal::EntityWorld entities_;
    animal::EntityId player_;
    animal::PlatformerParams playerParams_;
    animal::PlatformerState playerState_;
    bool facingLeft_ = false;
    double time_ = 0.0;      // simulation time, drives animation
    float fluidTime_ = 0.0f; // simulation time not yet handed to the smoke solver

    animal::ActionMap actions_;
    bool showCollision_ = false;
    bool usingGamepad_ = false;
    size_t bandsPreset_ = 2;

    animal::Camera2D camera_;
    CaveAssets assets_;
    animal::SpriteMaterial creature_;
};

} // namespace game
