#pragma once

#include <animal/core/random.h>
#include <animal/framework/foliage.h>
#include <animal/framework/influence.h>
#include <animal/framework/level.h>
#include <animal/framework/light_animation.h>
#include <animal/framework/particles.h>
#include <animal/framework/patrol.h>
#include <animal/framework/triggers.h>
#include <animal/framework/tilemap.h>
#include <animal/renderer/lighting.h>
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
    animal::SpriteMaterial props;   // cell 0: candle, cell 1: vent grate, cell 2: gem, cell 3: lantern
    animal::SpriteMaterial crawler; // two walking frames
};

// Everything that lives in one room, built from its Tiled data: tiles, foliage, props, light
// sources, ambient particles and smoke vents. Rebuilt whenever the player enters the room.
class CaveRoom {
public:
    // `name` identifies the room in save flags; gems listed in `collected` ("name:id") are skipped.
    bool build(const animal::Room& room, const std::string& name, const std::set<std::string>& collected,
               uint32_t seed);

    const animal::TileMap& tiles() const { return tiles_; }
    animal::VineSystem& vines() { return vines_; }
    // Feet position (bottom-centre) of the room's "spawn" object, if it has one.
    std::optional<animal::Vec2> spawnPoint() const { return spawn_; }

    void step(float dt, double time, std::span<const animal::Influence> influences);

    // Gameplay queries against the player's collider (room pixels).
    std::vector<std::string> collectGems(const animal::IntRect& player); // save flags of gems picked up now
    std::optional<animal::Vec2> touchingCrawler(const animal::IntRect& player) const; // its centre
    std::vector<std::string> enteredTriggers(const animal::IntRect& player);         // their messages
    static std::string gemFlag(const std::string& room, int objectId) { return room + ":" + std::to_string(objectId); }

    void drawBehindPlayer(animal::Renderer2D& renderer, const CaveAssets& assets) const;
    void drawGems(animal::Renderer2D& renderer, const CaveAssets& assets, double time) const;
    void drawInFrontOfPlayer(animal::Renderer2D& renderer) const;
    void drawCollision(animal::Renderer2D& renderer) const;
    void addLights(animal::Renderer2D& renderer, double time) const;
    void addFluidSources(animal::Renderer2D& renderer, double time) const;
    // kFluidGrid mask: cells over solid tiles, plus canvas rows outside the room.
    std::vector<uint8_t> fluidObstacles(animal::Vec2 canvasOrigin) const;
    std::vector<uint8_t> lightOccluders(animal::Vec2 canvasOrigin) const; // one byte per canvas pixel

    size_t bladeCount() const { return grass_.blades().size(); }
    size_t vineCount() const { return vines_.vines().size(); }
    size_t particleCount() const { return particles_.size(); }
    size_t crawlerCount() const { return crawlers_.size(); }

private:
    struct Candle {
        animal::Vec2 position; // top-left of the 8×8 prop
        float phase = 0.0f;
        float emberBudget = 0.0f;
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

    struct Lantern {
        animal::Vec2 position;  // top-left of the 8×8 prop
        float chainTop = 0.0f;  // y where the chain meets the ceiling
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

    enum ParticleKind : uint8_t {
        Dust,
        Firefly,
        Ember,
        Spark,
    };

    animal::Vec2 randomOpenPosition();
    void spawnDust(bool midLife);
    void spawnFirefly();
    void spawnEmber(const Candle& candle);
    void burst(animal::Vec2 center);

    animal::TileMap tiles_;
    std::optional<animal::Vec2> spawn_;
    std::vector<Candle> candles_;
    std::vector<Vent> vents_;
    std::vector<LightSource> lights_;
    std::vector<Lantern> lanterns_;
    std::vector<Gem> gems_;
    std::vector<Crawler> crawlers_;
    animal::PatrolParams patrolParams_;
    animal::TriggerSet triggers_;
    animal::GrassField grass_;
    animal::VineSystem vines_;
    animal::WindParams wind_;
    animal::ParticleSystem particles_;
    animal::Random rng_;
    size_t dustCount_ = 70;
    size_t fireflyCount_ = 14;
};

} // namespace game
