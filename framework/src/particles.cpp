#include <animal/framework/particles.h>
#include <animal/framework/tilemap.h>

#include <algorithm>
#include <cmath>

namespace animal {

namespace {

uint8_t lerpChannel(uint8_t a, uint8_t b, float t) {
    return static_cast<uint8_t>(std::lround(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t));
}

float seedPhase(uint32_t seed, uint32_t salt) {
    return static_cast<float>((seed * 2654435761u + salt * 40503u) % 6283u) * 0.001f;
}

} // namespace

bool ParticleSystem::emit(const Particle& particle) {
    if (particles_.size() >= capacity_) {
        return false;
    }
    particles_.push_back(particle);
    return true;
}

void ParticleSystem::step(float dt, double time, const TileMap* map, std::span<const Influence> influences,
                          const WindParams& wind) {
    time_ = time;
    const auto t = static_cast<float>(time);
    for (size_t i = 0; i < particles_.size();) {
        Particle& p = particles_[i];
        p.age += dt;

        Vec2 acceleration = p.acceleration;
        if (p.windResponse != 0.0f) {
            acceleration.x += p.windResponse * wind.strength * windAt(wind, p.position.x, time);
        }
        if (p.wander != 0.0f) {
            // A slowly turning heading unique to each particle.
            const float heading = seedPhase(p.seed, 1) + t * 0.9f + 2.0f * std::sin(t * 0.43f + seedPhase(p.seed, 2));
            acceleration += Vec2{std::cos(heading), std::sin(heading)} * p.wander;
        }
        for (const Influence& influence : influences) {
            const IntRect& r = influence.bounds;
            const Vec2 center{static_cast<float>(r.x) + static_cast<float>(r.width) * 0.5f,
                              static_cast<float>(r.y) + static_cast<float>(r.height) * 0.5f};
            const Vec2 offset = p.position - center;
            const float reach = params_.pushRadius + 0.5f * static_cast<float>(std::max(r.width, r.height));
            const float distance = length(offset);
            if (distance < reach) {
                const float falloff = 1.0f - distance / reach;
                const Vec2 away = distance > 1e-3f ? offset * (1.0f / distance) : Vec2{0.0f, -1.0f};
                acceleration += away * (params_.pushStrength * falloff * p.pushResponse);
                acceleration += influence.velocity * (params_.dragAlong * falloff * p.pushResponse);
            }
        }

        p.velocity += acceleration * dt;
        p.velocity = p.velocity * std::max(0.0f, 1.0f - p.drag * dt);
        p.position += p.velocity * dt;

        bool dead = p.age >= p.lifetime;
        if (map && !dead) {
            const int size = map->tileSize();
            const auto tx = static_cast<int>(std::floor(p.position.x / static_cast<float>(size)));
            const auto ty = static_cast<int>(std::floor(p.position.y / static_cast<float>(size)));
            const IntRect bounds = map->pixelBounds();
            const float m = params_.boundsMargin;
            dead = (p.dieOnSolid && map->isSolid(tx, ty)) || p.position.x < static_cast<float>(bounds.x) - m ||
                   p.position.y < static_cast<float>(bounds.y) - m ||
                   p.position.x > static_cast<float>(bounds.right()) + m ||
                   p.position.y > static_cast<float>(bounds.bottom()) + m;
        }
        if (dead) {
            p = particles_.back();
            particles_.pop_back();
        } else {
            ++i;
        }
    }
}

Color ParticleSystem::colorOf(const Particle& p) const {
    const float life = std::clamp(p.age / std::max(p.lifetime, 1e-4f), 0.0f, 1.0f);
    Color c{lerpChannel(p.startColor.r, p.endColor.r, life), lerpChannel(p.startColor.g, p.endColor.g, life),
            lerpChannel(p.startColor.b, p.endColor.b, life), lerpChannel(p.startColor.a, p.endColor.a, life)};
    float alpha = 1.0f;
    if (p.fade > 0.0f) {
        alpha = std::min({1.0f, p.age / p.fade, (p.lifetime - p.age) / p.fade});
    }
    if (p.flicker > 0.0f) {
        const float wave = std::sin(static_cast<float>(time_) * p.flicker * 6.2831853f + seedPhase(p.seed, 3));
        alpha *= 0.55f + 0.45f * wave;
    }
    c.a = static_cast<uint8_t>(std::lround(static_cast<float>(c.a) * std::clamp(alpha, 0.0f, 1.0f)));
    return c;
}

size_t ParticleSystem::countOf(uint8_t kind) const {
    return static_cast<size_t>(
        std::count_if(particles_.begin(), particles_.end(), [kind](const Particle& p) { return p.kind == kind; }));
}

} // namespace animal
