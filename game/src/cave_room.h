#pragma once

#include <animal/core/random.h>
#include <animal/framework/foliage.h>
#include <animal/framework/influence.h>
#include <animal/framework/level.h>
#include <animal/framework/particles.h>
#include <animal/framework/tilemap.h>
#include <animal/resources/loaders.h>

#include <span>
#include <vector>

namespace animal {
class Renderer2D;
}

namespace game {

constexpr int kTileSize = 8;

struct CaveAssets {
    animal::SpriteMaterial tileset; // tile id = horizontal cell index
    animal::SpriteMaterial props;   // cell 0: candle, cell 1: vent grate
};

// Everything that lives in one room, built from its Tiled data: tiles, foliage, props, light
// sources, ambient particles and smoke vents. Rebuilt whenever the player enters the room.
class CaveRoom {
public:
    bool build(const animal::Room& room, uint32_t seed);

    const animal::TileMap& tiles() const { return tiles_; }
    animal::VineSystem& vines() { return vines_; }
    // Feet position (bottom-centre) of the room's "spawn" object, if it has one.
    std::optional<animal::Vec2> spawnPoint() const { return spawn_; }
    animal::Vec3 ambient() const { return ambient_; }

    void step(float dt, double time, std::span<const animal::Influence> influences);

    void drawBehindPlayer(animal::Renderer2D& renderer, const CaveAssets& assets) const;
    void drawInFrontOfPlayer(animal::Renderer2D& renderer) const;
    void drawCollision(animal::Renderer2D& renderer) const;
    void addLights(animal::Renderer2D& renderer, double time) const;
    void addFluidSources(animal::Renderer2D& renderer, double time) const;
    // kFluidGrid mask: cells over solid tiles, plus canvas rows outside the room.
    std::vector<uint8_t> fluidObstacles(animal::Vec2 canvasOrigin) const;

    size_t bladeCount() const { return grass_.blades().size(); }
    size_t vineCount() const { return vines_.vines().size(); }
    size_t particleCount() const { return particles_.size(); }

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

    struct TileLight {
        animal::Vec2 position;
        animal::Vec3 color;
        float radius = 44.0f;
        float intensity = 1.0f;
        float height = 12.0f;
        float phase = 0.0f;
    };

    enum ParticleKind : uint8_t {
        Dust,
        Firefly,
        Ember,
    };

    animal::Vec2 randomOpenPosition();
    void spawnDust(bool midLife);
    void spawnFirefly();
    void spawnEmber(const Candle& candle);

    animal::TileMap tiles_;
    std::optional<animal::Vec2> spawn_;
    animal::Vec3 ambient_{0.07f, 0.07f, 0.11f};
    std::vector<Candle> candles_;
    std::vector<Vent> vents_;
    std::vector<TileLight> tileLights_;
    std::vector<TileLight> pointLights_;
    animal::GrassField grass_;
    animal::VineSystem vines_;
    animal::WindParams wind_;
    animal::ParticleSystem particles_;
    animal::Random rng_;
    size_t dustCount_ = 70;
    size_t fireflyCount_ = 14;
};

} // namespace game
