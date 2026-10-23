#pragma once

#include "cave_room.h"
#include "dev/dev_tools.h"

#include <animal/app/application.h>
#include <animal/framework/action_map.h>
#include <animal/framework/animation.h>
#include <animal/framework/climbing.h>
#include <animal/framework/entity.h>
#include <animal/framework/level.h>
#include <animal/framework/look.h>
#include <animal/framework/platformer.h>
#include <animal/framework/save.h>
#include <animal/framework/tuning.h>
#include <animal/framework/water.h>
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

struct DevPanels;

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
    void onToolsPass(animal::EngineContext& context, animal::rhi::CommandList& commands) override;
    void onShutdown(animal::EngineContext& context) override;

private:
    friend struct DevPanels; // the developer tools inspect and edit the game state

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
    void reloadLooks();
    void setEffectsEnabled(bool enabled, animal::Renderer2D& renderer);
    void registerTuning(animal::Renderer2D& renderer);
    void loadTuning();
    // Rebuilds the current room from its (possibly changed) data, keeping the player where it is.
    bool rebuildRoom(animal::Renderer2D& renderer);
    // Moves the player into a room (spawn point, or the test position every room has floor at).
    bool teleport(const animal::WorldRoom& worldRoom, animal::Renderer2D& renderer);
    // Hot reload (ADR-0028) of a changed file under data/world: validated first, so a broken file
    // leaves everything as it was (and returns false, with the reason logged).
    bool reloadData(const std::string& path, animal::Renderer2D& renderer);
    bool reloadWorld(const std::string& path, animal::Renderer2D& renderer);
    void collectGemFlags(const animal::WorldRoom& worldRoom, const animal::Room& room);

    animal::World world_;
    std::map<std::string, animal::Room> rooms_; // loaded room data by path
    const animal::WorldRoom* currentRoom_ = nullptr;
    const animal::WorldRoom* startRoom_ = nullptr; // where a new game begins
    animal::Vec2 startFeet_;
    const animal::WorldRoom* checkpointRoom_ = nullptr; // respawn point: first footing in the last room entered
    animal::Vec2 checkpointFeet_;
    bool checkpointPending_ = false;
    CaveRoom cave_;
    animal::EffectLibrary effects_; // effect presets by path, shared by all rooms
    animal::PlantLibrary plants_;   // plant species by path
    uint32_t roomVisits_ = 0;

    animal::EntityWorld entities_;
    animal::EntityId player_;
    animal::PlatformerParams playerParams_;
    animal::PlatformerState playerState_;
    animal::ClimbParams climbParams_;
    animal::ClimbState climb_;
    animal::SwimParams swimParams_;
    animal::Tuning tuning_;  // the parameters above, overridable by world/tuning.json (ADR-0027)
    std::string tuningPath_;
    bool facingLeft_ = false;
    float invulnerable_ = 0.0f; // seconds left after a hit
    float stunned_ = 0.0f;      // seconds of knockback without control
    float submersion_ = 0.0f;   // share of the player under water (last step)

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
    float gpuParticleTime_ = 0.0f; // likewise for the GPU particles

    animal::ActionMap actions_;
    bool showCollision_ = false;
    bool usingGamepad_ = false;
    uint32_t stressLights_ = 0; // --stress-lights=N: extra moving shadow-casting lights
    std::string startRoomName_; // --start-room=NAME: begin there (testing; progress is not saved)
    size_t bandsOverride_ = 0; // F4: index into kBandOverrides (0 = the room's look)
    bool shadowsEnabled_ = true; // F7
    bool effectsEnabled_ = true; // F10

    // Looks (ADR-0015): presets by path, and the blend from the previous room's look.
    std::map<std::string, animal::Look> looks_;
    animal::Look lookFrom_;
    animal::Look lookTo_;
    float lookBlend_ = 1.0f;
    std::string lookName_;

    animal::Camera2D camera_;
    CaveAssets assets_;
    animal::SpriteMaterial creature_;

    DevTools dev_;
};

} // namespace game
