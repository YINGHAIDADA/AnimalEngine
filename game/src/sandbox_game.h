#pragma once

#include "cave_room.h"

#include <animal/app/application.h>
#include <animal/framework/action_map.h>
#include <animal/framework/animation.h>
#include <animal/framework/climbing.h>
#include <animal/framework/entity.h>
#include <animal/framework/level.h>
#include <animal/framework/look.h>
#include <animal/framework/platformer.h>
#include <animal/framework/save.h>
#include <animal/renderer/camera2d.h>
#include <animal/resources/loaders.h>

#include <map>
#include <set>
#include <string>
#include <string_view>
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
    bool onArgument(std::string_view argument) override;
    bool onInit(animal::EngineContext& context) override;
    void onFrame(animal::EngineContext& context) override;
    void onFixedUpdate(animal::EngineContext& context, double dt) override;
    void onRender(animal::EngineContext& context, animal::Renderer2D& renderer) override;
    void onScreen(animal::EngineContext& context, animal::Renderer2D& renderer) override;
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
    void loadProgress();
    void resetProgress(animal::Renderer2D& renderer);
    void saveProgress();
    void updateGameplay(animal::Entity& player);
    void showMessage(std::string text);
    animal::Look roomLook(const animal::WorldRoom& worldRoom);
    animal::Look currentLook() const;
    void applyCurrentLook(animal::Renderer2D& renderer) const;
    void addStressLights(animal::Renderer2D& renderer) const;
    void setupAnimations();
    void updateAnimation(float dt);

    animal::World world_;
    std::map<std::string, animal::Room> rooms_; // loaded room data by path
    const animal::WorldRoom* currentRoom_ = nullptr;
    const animal::WorldRoom* startRoom_ = nullptr; // where a new game begins
    animal::Vec2 startFeet_;
    const animal::WorldRoom* checkpointRoom_ = nullptr; // respawn point: first footing in the last room entered
    animal::Vec2 checkpointFeet_;
    bool checkpointPending_ = false;
    CaveRoom cave_;
    uint32_t roomVisits_ = 0;

    animal::EntityWorld entities_;
    animal::EntityId player_;
    animal::PlatformerParams playerParams_;
    animal::PlatformerState playerState_;
    animal::ClimbParams climbParams_;
    animal::ClimbState climb_;
    bool facingLeft_ = false;
    float invulnerable_ = 0.0f; // seconds left after a hit
    float stunned_ = 0.0f;      // seconds of knockback without control

    animal::SaveData save_;  // progress: checkpoint and collected gems
    std::string savePath_;   // empty: progress is not persisted (--demo, --deterministic)
    std::set<std::string> gemFlags_; // save flags of every gem in the world
    std::string message_;    // trigger text at the bottom of the screen
    float messageTime_ = 0.0f;

    animal::Animator animator_;
    struct Clips {
        animal::Animator::ClipId idle, run, jump, fall, climb, land;
    } clips_{};
    double time_ = 0.0;      // simulation time, drives animation
    float fluidTime_ = 0.0f; // simulation time not yet handed to the smoke solver

    animal::ActionMap actions_;
    bool showCollision_ = false;
    bool usingGamepad_ = false;
    uint32_t stressLights_ = 0; // --stress-lights=N: extra moving shadow-casting lights
    std::string startRoomName_; // --start-room=NAME: begin there (testing; progress is not saved)
    size_t bandsOverride_ = 0; // F4: index into kBandOverrides (0 = the room's look)
    bool shadowsEnabled_ = true; // F7

    // Looks (ADR-0015): presets by path, and the blend from the previous room's look.
    std::map<std::string, animal::Look> looks_;
    animal::Look lookFrom_;
    animal::Look lookTo_;
    float lookBlend_ = 1.0f;
    std::string lookName_;

    animal::Camera2D camera_;
    CaveAssets assets_;
    animal::SpriteMaterial creature_;
};

} // namespace game
