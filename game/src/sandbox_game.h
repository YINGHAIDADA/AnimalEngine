#pragma once

#include "cave_room.h"
#include "dev/dev_tools.h"

#include <animal/app/application.h>
#include <animal/audio/audio.h>
#include <animal/core/random.h>
#include <animal/framework/action_map.h>
#include <animal/framework/animation.h>
#include <animal/framework/climbing.h>
#include <animal/framework/entity.h>
#include <animal/framework/level.h>
#include <animal/framework/look.h>
#include <animal/framework/platformer.h>
#include <animal/framework/projectile.h>
#include <animal/framework/save.h>
#include <animal/framework/settings.h>
#include <animal/framework/sprite_sheet.h>
#include <animal/framework/tuning.h>
#include <animal/framework/water.h>
#include <animal/renderer/camera2d.h>
#include <animal/resources/loaders.h>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace animal {
class Renderer2D;
}

namespace game {

struct DevPanels;

std::string roomName(const std::string& path); // a room's name: its file name without extension

enum Action : animal::ActionId {
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    Jump,
    Map,   // read every frame (ActionMap::pressedThisFrame)
    Pause,
    Throw, // dynamite
};

// M1b sandbox: a Tiled world of single-screen rooms, a platformer-controlled creature, and the
// V1 visual systems in every room.
class SandboxGame final : public animal::Application {
public:
    // An action, the name it is saved under (settings) and shown with (menus), and its defaults.
    struct ActionInfo {
        Action id;
        const char* name;
        const char* label;
        animal::ActionBinding defaults;
    };
    static const std::vector<ActionInfo>& actionInfos();

    enum class Page { None, Title, ConfirmNewGame, Pause, Settings, Audio, Display, Controls, Rebind };
    struct MenuItem {
        std::string label;
        std::string value;                 // shown in a column; empty for plain entries
        std::function<void()> activate;    // confirm
        std::function<void(int)> adjust;   // left / right (-1 / +1)
        bool disabled = false;
    };

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

    // front_end.cpp (ADR-0036)
    std::vector<MenuItem> menuItems();
    void updateFrontEnd(animal::EngineContext& context, float dt);
    void drawFrontEnd(animal::Renderer2D& renderer);
    void openPage(Page page);
    void backPage();
    void closeMenus();
    void openTitle();
    bool worldPaused() const; // a menu over the game (not the title screen)
    std::string screenName() const;
    void rebind(size_t actionIndex, std::optional<animal::platform::Key> key,
                std::optional<animal::platform::GamepadButton> button);
    // controls.cpp (ADR-0036)
    void bindActions(); // defaults, with the player's rebinding from the settings
    animal::ActionBinding bindingFor(const ActionInfo& info) const;
    bool usingGamepad() const; // the gamepad was used last: prompts name its buttons
    std::string prompt(Action action) const; // "SPACE", "A", "CROSS"...
    static std::string inputLabel(animal::platform::Key key);
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
    void drawMap(animal::Renderer2D& renderer); // map_screen.cpp
    // progression.cpp (ADR-0034)
    size_t collectedGems() const;
    bool canDive() const;
    void stepPlatforms(animal::Entity& player, float dt);
    bool stepDive(animal::Entity& player, float moveX, float moveY, float dt);
    void applyCurrents(animal::Entity& player, float dt);
    void hurt(animal::Entity& player); // spikes or crushed: a quarter heart and back to the checkpoint
    void takeDamage();                 // a quarter heart; with none left, back to the checkpoint
    void drawHearts(animal::Renderer2D& renderer) const;
    void updateMechanisms(animal::Entity& player, float dt);
    void drawEnding(animal::Renderer2D& renderer);
    // dynamite.cpp (ADR-0038)
    void throwDynamite(animal::Entity& player);
    void stepDynamite(float dt);
    void explode(animal::Vec2 centre);
    void addBlastInfluences(std::vector<animal::Influence>& influences) const;
    void drawDynamite(animal::Renderer2D& renderer) const;
    void addDynamiteLights(animal::Renderer2D& renderer) const;
    void addBlastSmoke(animal::Renderer2D& renderer) const;
    void drawDynamiteHud(animal::Renderer2D& renderer) const;
    void vineSounds(const animal::Entity& player, bool climbing, float dt); // rustling and swishing
    // game_settings.cpp (ADR-0036)
    void loadSettings(animal::EngineContext& context);
    void applyDisplaySettings(animal::EngineContext& context);
    void applyVolumes();
    void saveSettings(animal::EngineContext& context);
    // A sound at a room position (panned by where it is on screen), slightly varied in pitch.
    void playSound(animal::audio::SoundHandle sound, float x, float volume = 1.0f);
    animal::Look roomLook(const animal::WorldRoom& worldRoom);
    animal::Look currentLook() const;
    void applyCurrentLook(animal::Renderer2D& renderer) const;
    void addStressLights(animal::Renderer2D& renderer) const;
    bool setupAnimations(); // from the creature's sprite sheet tags
    void updateAnimation(float dt);
    void reloadLooks();
    void setEffectsEnabled(bool enabled, animal::Renderer2D& renderer);
    void registerTuning(animal::Renderer2D& renderer);
    void loadTuning();
    // Rebuilds the current room from its (possibly changed) data, keeping the player where it is.
    bool rebuildRoom(animal::Renderer2D& renderer);
    // Moves the player into a room (spawn point, or the test position every room has floor at).
    bool teleport(const animal::WorldRoom& worldRoom, animal::Renderer2D& renderer);
    animal::Vec2 entryFeet(const animal::WorldRoom& worldRoom);
    // Hot reload (ADR-0028) of a changed file under data/world: validated first, so a broken file
    // leaves everything as it was (and returns false, with the reason logged).
    bool reloadData(const std::string& path, animal::Renderer2D& renderer);
    bool reloadWorld(const std::string& path, animal::Renderer2D& renderer);
    void collectGemFlags(const animal::WorldRoom& worldRoom, const animal::Room& room);

    animal::World world_;
    std::string worldPath_;
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
    animal::DiveParams diveParams_;
    bool diving_ = false;         // under water with the dive ability
    bool respawnPending_ = false; // hurt this step: back to the checkpoint after it
    // Dynamite (ADR-0038)
    static constexpr int kMaxDynamite = 9;
    int dynamite_ = 0; // carried
    struct Thrown {
        animal::Projectile projectile;
        float fuse = 0.0f; // seconds left
    };
    std::vector<Thrown> thrown_;
    struct Blast {
        animal::Vec2 centre;
        float age = 0.0f;
    };
    std::vector<Blast> blasts_;
    animal::ProjectileParams throwParams_;
    int crawlersKilled_ = 0;
    // Vine sounds
    float vineClimbDistance_ = 0.0f;
    float vineSwingCooldown_ = 0.0f;
    float vineLastSpeed_ = 0.0f;
    animal::Vec2 vineLastPosition_;
    bool vineWasClimbing_ = false;
    static constexpr int kQuartersPerHeart = 4;
    static constexpr int kMaxHealth = 3 * kQuartersPerHeart; // three hearts of four quarters
    int health_ = kMaxHealth;     // quarters left; refilled when knocked out
    bool endingOpen_ = false;     // the goal was reached: the ending screen shows until jump
    static constexpr const char* kFinishedFlag = "finished";
    animal::Tuning tuning_;  // the parameters above, overridable by world/tuning.json (ADR-0027)
    std::string tuningPath_;
    bool facingLeft_ = false;
    float invulnerable_ = 0.0f; // seconds left after a hit
    float stunned_ = 0.0f;      // seconds of knockback without control
    float submersion_ = 0.0f;   // share of the player under water (last step)

    animal::Settings settings_;  // the player's volumes, display and controls (ADR-0036)
    std::string settingsPath_;   // empty: not persisted (test runs, unless --settings=path)
    animal::Settings savedSettings_; // as last read or written
    bool settingsChanged_ = false;   // changed in the menus: written when they close

    struct MenuState {
        struct Entry {
            Page page;
            int selected;
        };
        Page page = Page::None;
        std::vector<Entry> stack; // pages to go back to
        int selected = 0;
        bool fromTitle = false;   // the menus were opened from the title screen: the world is not paused
        size_t rebindAction = 0;
        float rebindWait = 0.0f;
        float stickX = 0.0f;      // last frame's stick, for menu steps
        float stickY = 0.0f;
    } menu_;
    bool frontEndEnabled_ = false; // title and pause menus (not in test runs unless --title)
    bool forceTitle_ = false;      // --title
    bool forceMenus_ = false;      // --menus
    animal::EngineContext* context_ = nullptr;
    animal::SaveData save_;  // progress: checkpoint and collected gems
    std::string savePath_;   // empty: progress is not persisted (--demo, --deterministic)
    std::set<std::string> gemFlags_; // save flags of every gem in the world
    std::string message_;    // trigger text at the bottom of the screen
    float messageTime_ = 0.0f;

    animal::Animator animator_;
    animal::SpriteSheet creatureSheet_; // Aseprite export (ADR-0030): frames and animation tags
    std::string creatureSheetPath_;
    struct Clips {
        animal::Animator::ClipId idle, run, jump, fall, climb, land;
    } clips_{};
    double time_ = 0.0;      // simulation time, drives animation
    float fluidTime_ = 0.0f; // simulation time not yet handed to the smoke solver
    float gpuParticleTime_ = 0.0f; // likewise for the GPU particles

    animal::ActionMap actions_;
    bool showCollision_ = false;
    bool mapOpen_ = false; // M / Back: the map of the rooms seen so far; the world pauses
    const animal::platform::Input* input_ = nullptr;
    uint32_t stressLights_ = 0; // --stress-lights=N: extra moving shadow-casting lights
    std::string startRoomName_; // --start-room=NAME: begin there (testing; progress is not saved)
    std::set<std::string> testFlags_; // --flags=a,b: save flags to begin with (testing)
    size_t testGems_ = 0;             // --gems=N: begin with N gems collected (testing)
    uint64_t traceEvery_ = 0;         // --trace[=N]: log the player's room, position and state every N steps
    uint64_t traceSteps_ = 0;
    // --expect=room:NAME,gems:N,...: checked when the run ends (automated playthroughs, ADR-0032).
    std::vector<std::pair<std::string, std::string>> expectations_;
    bool checkExpectations();
    uint32_t respawns_ = 0;
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

    // Audio (ADR-0033)
    animal::audio::AudioSystem* audio_ = nullptr;
    struct Sounds {
        animal::audio::SoundHandle jump, land, gem, hit, splash, grab, ambience, click, door, ability, crumble, toss, fuse,
            explosion, pickup, vineClimb, vineSwing;
    } sounds_{};
    animal::audio::VoiceHandle ambienceVoice_;
    animal::Random soundRandom_{0x50D};           // pitch variation only: never touches the simulation
    std::array<float, 3> busVolumes_{1.0f, 0.5f, 1.0f}; // effects, ambience, music (tuning)

    DevTools dev_;
};

} // namespace game
