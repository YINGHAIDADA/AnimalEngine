#pragma once

#include <animal/framework/level.h>
#include <animal/framework/platforms.h>

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace animal {
class Renderer2D;
class TileMap;
} // namespace animal

namespace game {

struct CaveAssets;

// What opens doors and moves the player through the area (ADR-0034), built from a room's objects:
//   platform  rectangle; travelX / travelY (px), speed, pause, phase; falls = true for one that
//             drops a moment after being stood on
//   door      rectangle; opens for good when the switch named by "flag" is pressed, or once
//             "gems" gems are collected (a gate)
//   switch    point (bottom-centre, on the floor); pressed by walking into it; "flag" names it
//   ability   point (centre); picking it up grants "name" (dive); "message" is shown
//   goal      rectangle; entering it ends the run
// Switches and abilities are save flags ("switch:NAME", "ability:NAME"), so they hold in every room.
class Mechanisms {
public:
    static std::string switchFlag(const std::string& name) { return "switch:" + name; }
    static std::string abilityFlag(const std::string& name) { return "ability:" + name; }

    void clear();
    // Takes the object if it is one of the types above; false otherwise.
    bool add(const animal::LevelObject& object, const std::string& roomPath, const std::set<std::string>& flags);
    // After all objects: doors whose condition already holds start open; the map gets the solids.
    void finish(const std::set<std::string>& flags, size_t gems, animal::TileMap& map);

    // Before the player moves: platforms move, carrying and pushing the player.
    animal::PlatformSystem::StepResult stepPlatforms(float dt, animal::TileMap& map, animal::Body* player);

    struct Touch {
        std::vector<std::string> flags;    // save flags set now (switches, abilities)
        std::vector<std::string> messages; // to show
        bool pressed = false;              // a switch went down
        bool ability = false;              // an ability was picked up
        bool goal = false;                 // the player is in a goal area
        std::optional<int> gateNeeds;      // walked into a closed gate: gems it needs
    };
    // After the player moved (room pixels).
    Touch touch(const animal::IntRect& player);
    // Opens the doors whose condition holds now and animates them; returns how many opened.
    int update(float dt, const std::set<std::string>& flags, size_t gems, animal::TileMap& map);
    // Falling platforms that started shaking this step (for the sound).
    int startedShaking() const { return startedShaking_; }

    void draw(animal::Renderer2D& renderer, const CaveAssets& assets, double time) const;
    void addLights(animal::Renderer2D& renderer, double time) const;
    void drawCollision(animal::Renderer2D& renderer) const;

private:
    struct Door {
        size_t platform = 0; // index in platforms_
        animal::IntRect rect;
        std::string flag;    // switch name, or empty
        int gems = 0;        // gems needed (gate), or 0
        bool open = false;
        float opened = 0.0f; // 0..1, the slide into the ceiling
        bool touched = false;
    };
    struct Switch {
        animal::Vec2 topLeft; // of the 8×8 prop
        std::string flag;
        std::string message;
        bool on = false;
    };
    struct Pickup {
        animal::Vec2 center;
        std::string flag;
        std::string message;
        bool taken = false;
    };

    animal::PlatformSystem platforms_;
    std::vector<size_t> moving_; // indices in platforms_ that are platforms (not doors)
    std::vector<Door> doors_;
    std::vector<Switch> switches_;
    std::vector<Pickup> pickups_;
    std::vector<animal::IntRect> goals_;
    int startedShaking_ = 0;
};

} // namespace game
