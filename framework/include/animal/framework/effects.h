#pragma once

// Particle effects (ADR-0017): reusable presets (*.effect.json) and the emitters placed in a room.

#include <animal/core/math.h>
#include <animal/core/random.h>
#include <animal/framework/foliage.h>
#include <animal/framework/geometry.h>
#include <animal/framework/influence.h>
#include <animal/framework/level.h>
#include <animal/framework/particles.h>

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace animal {

class Renderer2D;
class TileMap;

struct FloatRange {
    float min = 0.0f;
    float max = 0.0f;
};

struct EffectPreset {
    enum class Style {
        Lit,  // a lit pixel in the world pass: only visible where light falls (dust)
        Glow, // an unlit additive pixel over the lit scene (fireflies, embers)
    };

    std::string name; // file name without ".effect.json"
    // Simulated on the GPU (ADR-0020): a fixed population of `count`; rate, burst, prewarm, offset,
    // spread, speed / angle, wander, push, flicker, halo and lights are not supported there, and
    // acceleration ranges use their midpoint.
    bool gpu = false;

    // Emission. An emitter keeps `count` particles alive, adds `rate` per second, and a burst
    // spawns `burst` at once; any combination. Area emitters spawn at random open positions in
    // their rectangle; point emitters at their position + offset + spread.
    int count = 0;
    float rate = 0.0f;
    int burst = 0;
    bool prewarm = false; // the initial population starts at random ages instead of all newborn
    Vec2 offset;
    FloatRange spreadX, spreadY;

    // Motion (per particle, sampled from the ranges)
    FloatRange velocityX, velocityY;
    FloatRange speed, angle; // added polar velocity; angle in degrees, 0 = right, 90 = down
    FloatRange accelerationX, accelerationY;
    float drag = 0.0f;
    float windResponse = 0.0f;
    float wander = 0.0f;
    float pushResponse = 1.0f;
    bool dieOnSolid = false;

    // Look
    FloatRange lifetime{1.0f, 1.0f};
    float fade = 0.0f;
    FloatRange flicker;
    Color startColor = Color::white();
    Color endColor = Color::white();
    Style style = Style::Glow;
    float halo = 0.0f; // Glow: alpha of the four neighbouring pixels relative to the centre

    // A point light following each particle (radius 0: none); intensity scales with its alpha.
    float lightRadius = 0.0f;
    Vec3 lightColor{1.0f, 1.0f, 1.0f};
    float lightIntensity = 1.0f;
    float lightHeight = 8.0f;
};

// File format (every field optional except format / version):
//   {"format": "animal-effect", "version": 1, "count": 70, "velocityX": [-3, 3], "drag": 0.6, ...}
// Ranges are a number or [min, max]; colours "#rrggbb" or [r, g, b, a] (0-255); "lifetime" may be
// "forever"; "style" is "lit" or "glow". Unknown fields and wrong types are errors.
std::optional<EffectPreset> parseEffect(std::string_view json, const std::string& path, std::string* error);

// Loads presets once per path; failures are logged and remembered.
class EffectLibrary {
public:
    const EffectPreset* load(const std::string& path);
    // Invalidates every pointer returned by load(): rebuild what uses them.
    void clear() { cache_.clear(); }
    // Replaces (or adds) a preset in place: pointers already returned stay valid and see the new
    // values; systems that copied the preset need a rebuild.
    void set(const std::string& path, EffectPreset preset) { cache_[path] = std::move(preset); }
    std::vector<std::string> paths() const; // presets that loaded successfully

private:
    std::map<std::string, std::optional<EffectPreset>> cache_;
};

class EffectSystem {
public:
    using PresetId = uint8_t;

    // Removes presets, emitters and particles.
    void reset(uint32_t seed);
    // At most 255 presets; the same preset object added twice gets the same id.
    std::optional<PresetId> addPreset(const EffectPreset& preset);
    // `area` empty: a point emitter. `count` / `rate` override the preset's.
    void addEmitter(PresetId preset, Vec2 position, std::optional<IntRect> area = std::nullopt,
                    std::optional<int> count = std::nullopt, std::optional<float> rate = std::nullopt);
    void burst(PresetId preset, Vec2 position);
    // Spawns the initial populations; call once after adding the room's emitters.
    void start(const TileMap& map);

    void step(float dt, double time, const TileMap& map, std::span<const Influence> influences,
              const WindParams& wind);
    void draw(Renderer2D& renderer) const;
    void addLights(Renderer2D& renderer) const;

    // Off: nothing is simulated, drawn or lit; emitters resume from an empty state when turned on.
    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }

    const std::vector<EffectPreset>& presets() const { return presets_; }
    size_t countOf(PresetId preset) const;
    size_t particleCount() const { return particles_.size(); }
    const std::vector<Particle>& particles() const { return particles_.particles(); }
    double lastStepMilliseconds() const { return lastStepMilliseconds_; } // CPU time of the last step

private:
    struct Emitter {
        PresetId preset;
        Vec2 position;
        std::optional<IntRect> area;
        int count;
        float rate;
        float budget = 0.0f;
    };

    void spawn(PresetId preset, const Emitter* emitter, uint16_t source, const TileMap* map, bool randomAge);
    Vec2 openPosition(const IntRect& area, const TileMap* map);

    std::vector<EffectPreset> presets_;
    std::vector<const EffectPreset*> presetSources_;
    std::vector<Emitter> emitters_;
    ParticleSystem particles_;
    Random rng_;
    bool enabled_ = true;
    double lastStepMilliseconds_ = 0.0;
};

} // namespace animal
