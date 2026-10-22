#pragma once

#include <animal/core/math.h>
#include <animal/framework/geometry.h>
#include <animal/framework/influence.h>
#include <animal/framework/tilemap.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <span>
#include <vector>

namespace animal {

struct WindParams {
    float strength = 1.0f; // 0 = calm, 1 = gentle breeze
    float speed = 0.5f;    // how fast gusts evolve
};

// Horizontal wind in [-1, 1] at world x and simulation time; smooth in both.
float windAt(const WindParams& wind, float x, double time);

// Grass blades are damped angular springs rooted on top of grass tiles. Rendered as pixel
// columns whose tip bends by the blade angle (quadratic curvature).
class GrassField {
public:
    struct Params {
        float stiffness = 120.0f;   // spring constant (1/s²)
        float windLean = 0.12f;     // radians at full wind
        float damping = 7.0f;       // (1/s)
        float maxBend = 1.15f;      // radians
        float pushReach = 3.0f;     // px beyond the influence bounds that still bend blades
        float dragFactor = 0.004f;  // extra lean per px/s of the influence's horizontal speed
        float landingImpulse = 0.06f; // angular velocity per px/s of landing speed
        float landingRadius = 14.0f;
    };

    struct Blade {
        float x = 0.0f;     // pixel column (left edge)
        float baseY = 0.0f; // first pixel row above the ground
        int height = 4;
        float angle = 0.0f;
        float angularVelocity = 0.0f;
        uint8_t shade = 0;  // art variation
    };

    // One blade every ~2 px on each exposed top of a tile for which `isGrassy(id)` is true.
    // Positions and heights come from a hash of `seed`, so generation is deterministic.
    void generate(const TileMap& map, const std::function<bool(TileId)>& isGrassy, uint32_t seed);

    void step(float dt, double time, std::span<const Influence> influences, const WindParams& wind);

    // Calls fn(x, y, level, shade) for every pixel; level 0 = base .. 2 = tip.
    template <typename F>
    void forEachPixel(F&& fn) const {
        for (const Blade& blade : blades_) {
            const float tip = std::sin(blade.angle) * static_cast<float>(blade.height);
            for (int i = 0; i < blade.height; ++i) {
                const float t = static_cast<float>(i + 1) / static_cast<float>(blade.height);
                const int x = static_cast<int>(std::lround(blade.x + tip * t * t));
                const int y = static_cast<int>(blade.baseY) - i;
                const int level = i * 3 / blade.height;
                fn(x, y, level, blade.shade);
            }
        }
    }

    const std::vector<Blade>& blades() const { return blades_; }
    std::vector<Blade>& blades() { return blades_; }
    Params& params() { return params_; }

private:
    std::vector<Blade> blades_;
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
    };

    void addVine(Vec2 anchor, int segments);
    void step(float dt, double time, std::span<const Influence> influences, const WindParams& wind);

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
    void satisfyConstraint(Vine& vine, size_t i) const;

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
