#pragma once

#include "mechanisms.h"

#include <animal/core/random.h>
#include <animal/framework/effects.h>
#include <animal/framework/fluid_stirs.h>
#include <animal/framework/foliage.h>
#include <animal/framework/influence.h>
#include <animal/framework/level.h>
#include <animal/framework/light_animation.h>
#include <animal/framework/particles.h>
#include <animal/framework/patrol.h>
#include <animal/framework/pendulum.h>
#include <animal/framework/triggers.h>
#include <animal/framework/water.h>
#include <animal/framework/tilemap.h>
#include <animal/renderer/gpu_particles.h>
#include <animal/renderer/lighting.h>
#include <animal/renderer/water.h>
#include <animal/resources/loaders.h>

#include <set>
#include <span>
#include <string>
#include <vector>

namespace animal {
class Renderer2D;
}

namespace game {

constexpr int kTileSize = 8;

struct CaveAssets {
    animal::SpriteMaterial tileset; // tile id = horizontal cell index
    // cells: 0 candle, 1 vent grate, 2 gem, 3 lantern, 4 platform, 5 door, 6 / 7 switch off / on,
    // 8 ability orb, 9 gem gate, 10 / 11 heart full / empty, 12 dynamite
    animal::SpriteMaterial props;
    animal::SpriteMaterial crawler; // two walking frames
};

// Everything that lives in one room, built from its Tiled data: tiles, foliage, props, light
// sources, ambient particles and smoke vents. Rebuilt whenever the player enters the room.
class CaveRoom {
public:
    // `name` identifies the room in save flags; gems listed in `flags` ("name:id") are skipped, and
    // doors whose switch is in `flags` or whose gem count `gems` reaches start open.
    bool build(const animal::Room& room, const std::string& name, const std::set<std::string>& flags, size_t gems,
               animal::EffectLibrary& effects, animal::PlantLibrary& plants, uint32_t seed);

    const animal::TileMap& tiles() const { return tiles_; }
    animal::VineSystem& vines() { return vines_; }
    // Feet position (bottom-centre) of the room's "spawn" object, if it has one.
    std::optional<animal::Vec2> spawnPoint() const { return spawn_; }

    void step(float dt, double time, std::span<const animal::Influence> influences);

    // Mechanisms (ADR-0034): platforms move before the player does; touch / update after.
    animal::PlatformSystem::StepResult stepPlatforms(float dt, animal::Body* player) {
        return mechanisms_.stepPlatforms(dt, tiles_, player);
    }
    Mechanisms::Touch touchMechanisms(const animal::IntRect& player) { return mechanisms_.touch(player); }
    int updateMechanisms(float dt, const std::set<std::string>& flags, size_t gems) {
        return mechanisms_.update(dt, flags, gems, tiles_);
    }
    const Mechanisms& mechanisms() const { return mechanisms_; }

    // Blasts (ADR-0038): lanterns within `radius` are pushed away, crawlers within it die (returns
    // how many).
    int blast(animal::Vec2 centre, float radius, float strength);
    animal::PendulumParams& lanternParams() { return lanternParams_; }
    float largestLanternSwing() const { return largestSwing_; } // radians since the room was built
    size_t aliveCrawlers() const { return crawlers_.size(); }
    // Dynamite lying in the room ("dynamite" objects, bottom-centre on the floor): picks up at most
    // `capacity` the player touches; they lie there again when the room is entered again.
    int collectDynamite(const animal::IntRect& player, int capacity);
    // The current of the deepest pool the box is in (px/s).
    animal::Vec2 currentAt(const animal::IntRect& box) const;

    // Gameplay queries against the player's collider (room pixels).
    std::vector<std::string> collectGems(const animal::IntRect& player); // save flags of gems picked up now
    std::optional<animal::Vec2> touchingCrawler(const animal::IntRect& player) const; // its centre
    std::vector<std::string> enteredTriggers(const animal::IntRect& player);         // their messages
    // The player's submersion this step (deepest pool), after splashing the surface it crossed
    // (`previous` = last step's submersion) and rippling it while moving along it.
    float updatePlayerInWater(const animal::IntRect& player, animal::Vec2 velocity, float previous);
    float submersion(const animal::IntRect& box) const;
    void addWater(animal::Renderer2D& renderer, double time) const;
    const std::vector<animal::GpuEmitter>& gpuEmitters() const { return gpuEmitters_; }
    animal::Vec2 windAt(animal::Vec2 position, double time) const { return animal::windVectorAt(wind_, position, time); }
    static std::string gemFlag(const std::string& room, int objectId) { return room + ":" + std::to_string(objectId); }

    void drawBehindPlayer(animal::Renderer2D& renderer, const CaveAssets& assets) const;
    void drawGems(animal::Renderer2D& renderer, const CaveAssets& assets, double time) const;
    void drawInFrontOfPlayer(animal::Renderer2D& renderer) const;
    void drawCollision(animal::Renderer2D& renderer) const;
    void addLights(animal::Renderer2D& renderer, double time) const;
    void addFluidSources(animal::Renderer2D& renderer, double time) const;
    // What stirs the smoke (animal::fluidStirs): lanterns, crawlers and moving or falling platforms;
    // the vines, with the length of their last substep.
    void addStirBodies(std::vector<animal::StirBody>& bodies) const;
    const animal::VineSystem& vineSystem() const { return vines_; }
    float vineSubstep() const;
    // kFluidGrid mask: cells over solid tiles, plus canvas rows outside the room.
    std::vector<uint8_t> fluidObstacles(animal::Vec2 canvasOrigin) const;
    std::vector<uint8_t> lightOccluders(animal::Vec2 canvasOrigin) const; // one byte per canvas pixel

    size_t bladeCount() const { return grass_.blades().size(); }
    size_t vineCount() const { return vines_.vines().size(); }
    size_t particleCount() const { return effects_.particleCount(); }
    animal::EffectSystem& effects() { return effects_; }
    const animal::EffectSystem& effects() const { return effects_; }
    size_t crawlerCount() const { return crawlers_.size(); }
    // Effect and plant preset files the last build() referred to.
    const std::set<std::string>& presetPaths() const { return presetPaths_; }

private:
    struct Candle {
        animal::Vec2 position; // top-left of the 8×8 prop
        float phase = 0.0f;
    };

    struct Vent {
        animal::Vec2 position; // top-left of the 8×8 prop
        float rate = 6.0f;     // smoke density per second
        float force = 420.0f;  // upward push, px/s²
    };

    // Light tiles, light objects and lanterns.
    struct LightSource {
        animal::PointLight light;
        animal::LightAnimation animation;
        float phase = 0.0f;
    };

    struct Pool {
        animal::WaterBody body;
        animal::WaterSurface look;
        std::optional<animal::EffectSystem::PresetId> splash;
        animal::Vec2 current; // px/s ("currentX" / "currentY")
    };

    struct Lantern {
        animal::Pendulum swing; // pivot where the chain meets the ceiling, bob at the lantern's centre
        size_t light = 0;       // its light in lights_ (follows the lantern)
    };

    struct Gem {
        std::string flag;
        animal::Vec2 center;
        float phase = 0.0f;
        bool collected = false;
    };

    struct Crawler {
        animal::Body body;
        animal::PatrolState patrol;
        float walkTime = 0.0f;
    };

    std::optional<animal::EffectSystem::PresetId> effectPreset(animal::EffectLibrary& library, const animal::Room& room,
                                                               const std::string& file);

    animal::TileMap tiles_;
    std::set<std::string> presetPaths_;
    std::optional<animal::Vec2> spawn_;
    std::vector<Candle> candles_;
    std::vector<Vent> vents_;
    std::vector<LightSource> lights_;
    std::vector<Lantern> lanterns_;
    std::vector<Pool> pools_;
    std::vector<animal::GpuEmitter> gpuEmitters_;
    struct Stream { // a GPU emitter pouring into a pool: keeps rippling its surface
        size_t pool;
        float left, right;
    };
    std::vector<Stream> streams_;
    std::vector<Gem> gems_;
    std::vector<Crawler> crawlers_;
    animal::PatrolParams patrolParams_;
    animal::TriggerSet triggers_;
    animal::GrassField grass_;
    animal::VineSystem vines_;
    animal::WindParams wind_;
    animal::EffectSystem effects_;
    std::optional<animal::EffectSystem::PresetId> sparkPreset_; // gem pickup burst
    Mechanisms mechanisms_;
    struct DynamitePickup {
        animal::Vec2 topLeft; // of the 8×8 prop, resting on the floor
        float phase = 0.0f;   // of its hovering
        bool taken = false;
    };
    // Hovering above the floor and bobbing like a gem: it reads as something to pick up.
    animal::Vec2 pickupTopLeft(const DynamitePickup& stick) const;
    std::vector<DynamitePickup> dynamite_;
    animal::PendulumParams lanternParams_;
    float largestSwing_ = 0.0f;
    double lastTime_ = 0.0; // of the last step: drives the shaking of falling platforms
    float stepDt_ = 1.0f / 60.0f; // last step, for vine velocities (previous is one substep behind)
};

} // namespace game
