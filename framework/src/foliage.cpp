#include <animal/framework/foliage.h>

#include <algorithm>
#include <cmath>

namespace animal {

namespace {

// Small integer hash (deterministic across platforms) for placement variation.
uint32_t hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float centerX(const IntRect& r) {
    return static_cast<float>(r.x) + static_cast<float>(r.width) * 0.5f;
}

} // namespace

float windAt(const WindParams& wind, float x, double time) {
    const auto t = static_cast<float>(time) * wind.speed;
    const float gust = 0.6f + 0.4f * std::sin(t * 0.37f);
    return gust * (0.65f * std::sin(t + x * 0.031f) + 0.35f * std::sin(t * 2.3f + x * 0.11f));
}

// --- Grass ---------------------------------------------------------------------------------

void GrassField::generate(const TileMap& map, const std::function<bool(TileId)>& isGrassy, uint32_t seed) {
    blades_.clear();
    const int size = map.tileSize();
    for (int ty = 0; ty < map.height(); ++ty) {
        for (int tx = 0; tx < map.width(); ++tx) {
            if (!isGrassy(map.tile(tx, ty)) || map.isSolid(tx, ty - 1)) {
                continue;
            }
            for (int px = 0; px < size; px += 2) {
                const uint32_t h = hash(seed ^ hash(static_cast<uint32_t>(ty * 4096 + tx * size + px)));
                if (h % 5 == 0) {
                    continue; // gaps keep the tufts irregular
                }
                Blade blade;
                blade.x = static_cast<float>(tx * size + px + static_cast<int>((h >> 8) % 2));
                blade.baseY = static_cast<float>(ty * size - 1);
                blade.height = 2 + static_cast<int>((h >> 12) % 4);
                blade.shade = static_cast<uint8_t>((h >> 20) % 3);
                blades_.push_back(blade);
            }
        }
    }
}

void GrassField::step(float dt, double time, std::span<const FoliageInfluence> influences, const WindParams& wind) {
    for (Blade& blade : blades_) {
        float target = params_.windLean * wind.strength * windAt(wind, blade.x, time);
        const float top = blade.baseY - static_cast<float>(blade.height);

        for (const FoliageInfluence& influence : influences) {
            const IntRect& r = influence.bounds;
            const float left = static_cast<float>(r.x) - params_.pushReach;
            const float right = static_cast<float>(r.right()) + params_.pushReach;
            const bool verticalOverlap = static_cast<float>(r.bottom()) > top && static_cast<float>(r.y) <= blade.baseY;
            if (verticalOverlap && blade.x >= left && blade.x <= right) {
                const float offset = blade.x - centerX(r);
                const float halfSpan = (right - left) * 0.5f;
                const float proximity = 1.0f - std::min(1.0f, std::fabs(offset) / halfSpan);
                const float direction = offset >= 0.0f ? 1.0f : -1.0f;
                target += direction * params_.maxBend * (0.35f + 0.65f * proximity);
                target += influence.velocity.x * params_.dragFactor;
            }
            if (influence.landed) {
                const float offset = blade.x - centerX(r);
                const float feetY = static_cast<float>(r.bottom());
                if (std::fabs(offset) < params_.landingRadius && std::fabs(feetY - blade.baseY - 1.0f) < 2.0f) {
                    const float falloff = 1.0f - std::fabs(offset) / params_.landingRadius;
                    const float direction = offset >= 0.0f ? 1.0f : -1.0f;
                    blade.angularVelocity +=
                        direction * falloff * std::fabs(influence.velocity.y) * params_.landingImpulse;
                }
            }
        }

        target = std::clamp(target, -params_.maxBend, params_.maxBend);
        const float acceleration =
            -params_.stiffness * (blade.angle - target) - params_.damping * blade.angularVelocity;
        blade.angularVelocity += acceleration * dt;
        blade.angle = std::clamp(blade.angle + blade.angularVelocity * dt, -params_.maxBend, params_.maxBend);
    }
}

// --- Vines ---------------------------------------------------------------------------------

void VineSystem::addVine(Vec2 anchor, int segments) {
    Vine vine;
    for (int i = 0; i <= segments; ++i) {
        const Vec2 p{anchor.x, anchor.y + static_cast<float>(i) * params_.segmentLength};
        vine.points.push_back(p);
        vine.previous.push_back(p);
    }
    vines_.push_back(std::move(vine));
}

void VineSystem::step(float dt, double time, std::span<const FoliageInfluence> influences, const WindParams& wind) {
    for (Vine& vine : vines_) {
        const size_t count = vine.points.size();

        // Integrate (point 0 stays pinned).
        for (size_t i = 1; i < count; ++i) {
            Vec2& p = vine.points[i];
            Vec2& prev = vine.previous[i];
            const Vec2 velocity = (p - prev) * params_.damping;
            const float sway = params_.windForce * wind.strength * windAt(wind, p.x, time) *
                               (static_cast<float>(i) / static_cast<float>(count));
            const Vec2 acceleration{sway, params_.gravity};
            prev = p;
            p = p + velocity + acceleration * (dt * dt);
        }

        // Influences push points out sideways and drag them along.
        for (const FoliageInfluence& influence : influences) {
            const IntRect& r = influence.bounds;
            const float left = static_cast<float>(r.x) - 1.0f;
            const float right = static_cast<float>(r.right()) + 1.0f;
            const float top = static_cast<float>(r.y) - 1.0f;
            const float bottom = static_cast<float>(r.bottom()) + 1.0f;
            for (size_t i = 1; i < count; ++i) {
                Vec2& p = vine.points[i];
                if (p.x > left && p.x < right && p.y > top && p.y < bottom) {
                    const bool exitLeft = (p.x - left) < (right - p.x);
                    p.x = exitLeft ? left : right;
                    vine.previous[i].x -= influence.velocity.x * dt * params_.momentumTransfer;
                }
            }
        }

        // Distance constraints, anchor first so the chain hangs from it.
        for (int iteration = 0; iteration < params_.iterations; ++iteration) {
            for (size_t i = 1; i < count; ++i) {
                Vec2& a = vine.points[i - 1];
                Vec2& b = vine.points[i];
                const Vec2 delta = b - a;
                const float distance = std::max(length(delta), 1e-4f);
                const float error = (distance - params_.segmentLength) / distance;
                if (i == 1) {
                    b = b - delta * error; // a is the pinned anchor
                } else {
                    a = a + delta * (error * 0.5f);
                    b = b - delta * (error * 0.5f);
                }
            }
        }
        // Relaxation alone lets gravity stretch long chains; restore exact lengths from the
        // anchor outwards (follow-the-leader), which also adds a little natural damping.
        for (size_t i = 1; i < count; ++i) {
            const Vec2 delta = vine.points[i] - vine.points[i - 1];
            const float distance = std::max(length(delta), 1e-4f);
            vine.points[i] = vine.points[i - 1] + delta * (params_.segmentLength / distance);
        }
    }
}

} // namespace animal
