#pragma once

#include <animal/core/math.h>
#include <animal/framework/foliage.h>
#include <animal/framework/influence.h>

#include <cstdint>
#include <span>
#include <vector>

namespace animal {

class TileMap;

// One generic particle; the game composes looks (fireflies, embers, dust) from these knobs.
struct Particle {
    Vec2 position;
    Vec2 velocity;
    Vec2 acceleration;          // constant, px/s² (gravity, buoyancy)
    float drag = 0.0f;          // 1/s
    float age = 0.0f;
    float lifetime = 1.0f;      // s
    float windResponse = 0.0f;  // px/s² at full wind
    float wander = 0.0f;        // px/s² of smooth pseudo-random steering
    float flicker = 0.0f;       // Hz of brightness pulsing (0 = steady)
    float pushResponse = 1.0f;  // scales how much influences push it
    float fade = 0.0f;          // s of fade-in at birth and fade-out before death
    Color startColor = Color::white();
    Color endColor = Color::white();
    uint32_t seed = 0;          // per-particle variation (wander path, flicker phase)
    uint8_t kind = 0;           // game-defined render style (EffectSystem: preset id)
    uint16_t source = 0;        // EffectSystem: emitter index + 1 (0 = burst)
    bool dieOnSolid = false;
};

class ParticleSystem {
public:
    struct Params {
        float pushRadius = 10.0f;   // px around an influence's bounds
        float pushStrength = 160.0f; // px/s² away from the influence at contact
        float dragAlong = 1.5f;     // share of the influence velocity imparted per second
        float boundsMargin = 16.0f; // particles this far outside the map are removed
    };

    explicit ParticleSystem(size_t capacity = 4096) : capacity_(capacity) { particles_.reserve(capacity); }

    // Returns false (and drops the particle) when the system is full.
    bool emit(const Particle& particle);
    void clear() { particles_.clear(); }

    // `map` may be null (no solid / bounds checks).
    void step(float dt, double time, const TileMap* map, std::span<const Influence> influences,
              const WindParams& wind);

    // Color at the particle's current age (start → end), alpha scaled by fade and flicker.
    Color colorOf(const Particle& particle) const;

    const std::vector<Particle>& particles() const { return particles_; }
    size_t size() const { return particles_.size(); }
    size_t countOf(uint8_t kind) const;
    Params& params() { return params_; }

private:
    std::vector<Particle> particles_;
    size_t capacity_;
    Params params_;
    double time_ = 0.0;
};

} // namespace animal
