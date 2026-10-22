#pragma once

#include <animal/app/application.h>
#include <animal/core/random.h>
#include <animal/framework/action_map.h>
#include <animal/framework/entity.h>
#include <animal/framework/foliage.h>
#include <animal/framework/particles.h>
#include <animal/framework/platformer.h>
#include <animal/framework/tilemap.h>
#include <animal/renderer/camera2d.h>
#include <animal/resources/loaders.h>

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

// V1 sandbox: one single-screen room, a platformer-controlled creature, pixel lighting and
// interactive foliage.
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
    struct Candle {
        animal::Vec2 position; // top-left of the 8×8 prop
        float phase = 0.0f;    // desynchronizes the flicker
        float emberBudget = 0.0f;
    };

    enum ParticleKind : uint8_t {
        Dust,
        Firefly,
        Ember,
    };

    void bindActions();
    animal::Vec2 randomOpenPosition();
    void spawnDust(bool midLife);
    void spawnFirefly();
    void spawnEmber(const Candle& candle);
    void updateParticles(float dt, std::span<const animal::Influence> influences);
    void drawParticles(animal::Renderer2D& renderer) const;
    void applyDemoInput();
    void addLights(animal::Renderer2D& renderer);
    void drawCollisionDebug(animal::Renderer2D& renderer, const animal::Entity& player);
    void drawGrass(animal::Renderer2D& renderer) const;
    void drawVines(animal::Renderer2D& renderer) const;

    animal::TileMap room_;
    std::vector<Candle> candles_;
    animal::GrassField grass_;
    animal::VineSystem vines_;
    animal::WindParams wind_;
    animal::ParticleSystem particles_;
    animal::Random rng_{0x5EED};
    animal::EntityWorld entities_;
    animal::EntityId player_;
    animal::PlatformerParams playerParams_;
    animal::PlatformerState playerState_;
    bool facingLeft_ = false;
    double time_ = 0.0; // simulation time, drives light animation

    animal::ActionMap actions_;
    bool showCollision_ = false;
    bool usingGamepad_ = false;
    size_t bandsPreset_ = 2;

    animal::Camera2D camera_;
    animal::SpriteMaterial tileset_;
    animal::SpriteMaterial creature_;
    animal::SpriteMaterial props_;
};

} // namespace game
