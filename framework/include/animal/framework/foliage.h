#pragma once

#include <animal/core/math.h>
#include <animal/framework/geometry.h>
#include <animal/framework/influence.h>
#include <animal/framework/tilemap.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace animal {

// A local draft (ADR-0019): blows along `direction` from `position`, `radius` px either side of
// its axis, fading to nothing over `length` px (a vent's updraft, air out of a tunnel).
struct WindSource {
    Vec2 position;
    Vec2 direction{0.0f, -1.0f}; // unit
    float strength = 2.0f;       // wind units at the source (1 = gentle breeze)
    float length = 64.0f;
    float radius = 10.0f;
};

struct WindParams {
    float strength = 1.0f; // 0 = calm, 1 = gentle breeze
    float speed = 0.5f;    // how fast gusts evolve
    // Gust fronts sweeping across the room every `gustInterval` s at `gustSpeed` px/s (the sign
    // is the direction), `gustWidth` px wide; 0 strength = none.
    float gustStrength = 0.0f;
    float gustInterval = 6.0f;
    float gustSpeed = 120.0f;
    float gustWidth = 60.0f;
    std::vector<WindSource> sources;
};

// The base breeze: horizontal, in [-1, 1] at world x and simulation time; smooth in both.
float windAt(const WindParams& wind, float x, double time);
// The full wind at a point: base breeze × strength, gust fronts and local drafts, in wind units.
Vec2 windVectorAt(const WindParams& wind, Vec2 position, double time);

// A kind of plant growing as springy pixel strands (ADR-0019): grass, reeds, hanging moss,
// flowers. Upright species grow on exposed tops of solid tiles, hanging ones from exposed bottoms.
struct PlantSpecies {
    std::string name;
    int heightMin = 2;
    int heightMax = 5;
    int spacing = 2;          // px between strands
    float gaps = 0.2f;        // share of strand positions left empty
    bool hanging = false;
    float stiffness = 120.0f; // 1/s²
    float damping = 7.0f;     // 1/s
    float windLean = 0.12f;   // radians per wind unit
    float flutter = 0.15f;    // radians of quick shiver per wind unit above a breeze
    float maxBend = 1.15f;    // radians
    std::array<Color, 3> colors{Color::rgba(46, 104, 52), Color::rgba(76, 150, 70), Color::rgba(136, 198, 92)};
    // A flower on the tip (alpha 0: none); glowing flowers are drawn unlit and may carry a light.
    Color flowerColor = Color::rgba(0, 0, 0, 0);
    bool flowerGlow = false;
    float flowerLightRadius = 0.0f;
    Vec3 flowerLightColor{1.0f, 1.0f, 1.0f};
    float flowerLightIntensity = 0.5f;
};

// File format: {"format": "animal-plant", "version": 1, "heightMin": 8, "colors": [c, c, c], ...};
// colours "#rrggbb" or [r, g, b, a]. Unknown fields and wrong types are errors.
std::optional<PlantSpecies> parsePlant(std::string_view json, const std::string& path, std::string* error);

// Loads plant species once per path; failures are logged and remembered.
class PlantLibrary {
public:
    const PlantSpecies* load(const std::string& path);

private:
    std::map<std::string, std::optional<PlantSpecies>> cache_; // node-based: returned pointers stay valid
};

// Plant strands (grass blades, reeds, moss) are damped angular springs rooted on tile faces.
// Rendered as pixel columns whose tip bends by the strand angle (quadratic curvature).
class GrassField {
public:
    struct Params {
        float pushReach = 3.0f;     // px beyond the influence bounds that still bend blades
        float dragFactor = 0.004f;  // extra lean per px/s of the influence's horizontal speed
        float landingImpulse = 0.06f; // angular velocity per px/s of landing speed
        float landingRadius = 14.0f;
    };

    struct Blade {
        float x = 0.0f;     // pixel column (left edge)
        float baseY = 0.0f; // first pixel row off the tile face (above it, or below for hanging)
        int height = 4;
        float angle = 0.0f;
        float angularVelocity = 0.0f;
        uint8_t shade = 0;  // art variation
        uint8_t species = 0;
    };

    void clear();
    // At most 255 species; returns the index.
    size_t addSpecies(const PlantSpecies& species);
    // Default species (index 0, added if there is none) on each exposed top of a tile for which
    // `isGrassy(id)` is true. Positions and heights come from a hash of `seed` (deterministic).
    void generate(const TileMap& map, const std::function<bool(TileId)>& isGrassy, uint32_t seed);
    // `species` on every exposed solid-tile face inside `area` (world px): tops for upright
    // species, bottoms for hanging ones.
    void plant(const TileMap& map, const IntRect& area, size_t species, uint32_t seed);

    void step(float dt, double time, std::span<const Influence> influences, const WindParams& wind);

    // Calls fn(x, y, color) for every pixel.
    template <typename F>
    void forEachPixel(F&& fn) const {
        for (const Blade& blade : blades_) {
            const PlantSpecies& species = species_[blade.species];
            const float tip = std::sin(blade.angle) * static_cast<float>(blade.height);
            const int direction = species.hanging ? 1 : -1;
            for (int i = 0; i < blade.height; ++i) {
                const float t = static_cast<float>(i + 1) / static_cast<float>(blade.height);
                const int x = static_cast<int>(std::lround(blade.x + tip * t * t));
                const int y = static_cast<int>(blade.baseY) + direction * i;
                fn(x, y, shaded(species.colors[static_cast<size_t>(i * 3 / blade.height)], blade.shade));
            }
        }
    }

    // Calls fn(position, species) for the tip of every strand of a flowering species.
    template <typename F>
    void forEachFlower(F&& fn) const {
        for (const Blade& blade : blades_) {
            const PlantSpecies& species = species_[blade.species];
            if (species.flowerColor.a == 0) {
                continue;
            }
            const float tip = std::sin(blade.angle) * static_cast<float>(blade.height);
            const float y = blade.baseY + (species.hanging ? 1.0f : -1.0f) * static_cast<float>(blade.height);
            fn(Vec2{std::round(blade.x + tip), y}, species);
        }
    }

    const std::vector<Blade>& blades() const { return blades_; }
    std::vector<Blade>& blades() { return blades_; }
    const std::vector<PlantSpecies>& species() const { return species_; }
    Params& params() { return params_; }

private:
    // Shade 0 = as authored, 1 = darker, 2 = lighter.
    static Color shaded(Color c, uint8_t shade) {
        const int scale = shade == 1 ? 88 : (shade == 2 ? 112 : 100);
        auto channel = [scale](uint8_t v) { return static_cast<uint8_t>(std::min(255, v * scale / 100)); };
        return Color::rgba(channel(c.r), channel(c.g), channel(c.b), c.a);
    }
    void addStrands(const TileMap& map, int tx, int ty, size_t species, uint32_t seed);

    std::vector<Blade> blades_;
    std::vector<PlantSpecies> species_;
    Params params_;
};

// Hanging vines as Verlet chains: point 0 is pinned to its anchor, the rest swing under
// gravity, wind and influences, with distance constraints between neighbours.
class VineSystem {
public:
    struct Params {
        float segmentLength = 3.0f;
        float gravity = 420.0f;   // px/s²
        float damping = 0.96f;    // velocity kept per fixed step (heavier vines settle in ~2 s)
        float heldDamping = 0.995f; // while someone hangs on it: swings carry on, so they can be pumped up
        // A climber is much heavier than the vine: the points it holds get this inverse mass in the
        // constraints, so the rope below trails it instead of soaking up its swing.
        float climberInverseMass = 0.05f;
        float windForce = 35.0f;  // px/s², applied uniformly along the vine (≈ 5° lean at full wind)
        int substeps = 4;
        int iterations = 4;       // forward + backward constraint sweeps per substep
        // Contact with influences is a force, not a position fix, so vines have weight: a pushed
        // vine leans until gravity and chain tension balance the push, and slips behind the
        // player once its center passes. Touched points are also dragged toward the influence
        // velocity (blended, never beyond it, capped by maxPushSpeed).
        float pushAcceleration = 1500.0f; // px/s² at full penetration, toward the nearer edge
        float dragCoupling = 6.0f;        // 1/s: how fast touched points pick up the influence velocity
        float maxPushSpeed = 30.0f;       // px/s cap on the dragged velocity
    };

    struct Vine {
        std::vector<Vec2> points;
        std::vector<Vec2> previous;
        std::vector<Vec2> forces; // external accelerations for the next step (cleared after it)
        size_t heldPoint = 0;     // hold() since the last step: the grip lies between points heldPoint and heldPoint + 1 (0: not held)
    };

    // A position along a vine: index into vines() and arc length from the anchor (px).
    struct Grip {
        size_t vine = 0;
        float distance = 0.0f;
    };

    void addVine(Vec2 anchor, int segments);
    void step(float dt, double time, std::span<const Influence> influences, const WindParams& wind);

    float length(size_t vine) const;
    Vec2 pointAt(const Grip& grip) const;
    // Velocity of the chain at the grip during the last step (px/s); `dt` is the fixed step.
    Vec2 velocityAt(const Grip& grip, float dt) const;
    // Adds an acceleration (px/s²) at the grip for the next step, split between the two
    // neighbouring points (a climber's weight, a swing).
    void applyForce(const Grip& grip, Vec2 acceleration);
    // Someone hangs at the grip during the next step: lighter damping and a heavy point there
    // (Params::heldDamping, climberInverseMass).
    void hold(const Grip& grip);
    // Closest point on any vine inside `rect`, if one passes through it.
    std::optional<Grip> findGrip(const IntRect& rect) const;

    // Calls fn(x, y, isLeaf) for every pixel along each chain (leaves sprout every few points).
    template <typename F>
    void forEachPixel(F&& fn) const {
        for (const Vine& vine : vines_) {
            for (size_t i = 1; i < vine.points.size(); ++i) {
                rasterizeLine(vine.points[i - 1], vine.points[i], [&](int x, int y) { fn(x, y, false); });
                if (i % 2 == 0) {
                    const Vec2 p = vine.points[i];
                    const int side = (i / 2) % 2 == 0 ? -1 : 1;
                    fn(static_cast<int>(std::lround(p.x)) + side, static_cast<int>(std::lround(p.y)), true);
                }
            }
        }
    }

    const std::vector<Vine>& vines() const { return vines_; }
    Params& params() { return params_; }

private:
    void satisfyConstraint(Vine& vine, size_t i, size_t heldPoint) const;

    template <typename F>
    static void rasterizeLine(Vec2 a, Vec2 b, F&& plot) {
        int x0 = static_cast<int>(std::lround(a.x));
        int y0 = static_cast<int>(std::lround(a.y));
        const int x1 = static_cast<int>(std::lround(b.x));
        const int y1 = static_cast<int>(std::lround(b.y));
        const int dx = std::abs(x1 - x0);
        const int dy = -std::abs(y1 - y0);
        const int sx = x0 < x1 ? 1 : -1;
        const int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        while (true) {
            plot(x0, y0);
            if (x0 == x1 && y0 == y1) {
                break;
            }
            const int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }

    std::vector<Vine> vines_;
    Params params_;
};

} // namespace animal
