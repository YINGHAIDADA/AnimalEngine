#include <animal/framework/platforms.h>
#include <animal/framework/tilemap.h>

#include <algorithm>
#include <cmath>

namespace animal {

namespace {

// Position along an out-and-back path: progress 0..1 maps to the start, the far end and back.
Vec2 pathPoint(const PlatformDesc& desc, float progress) {
    return {static_cast<float>(desc.rect.x) + desc.travel.x * progress,
            static_cast<float>(desc.rect.y) + desc.travel.y * progress};
}

bool standsOn(const IntRect& body, const IntRect& platform) {
    return !body.overlaps(platform) && body.offset({0, 1}).overlaps(platform);
}

} // namespace

void PlatformSystem::clear() {
    platforms_.clear();
}

size_t PlatformSystem::add(const PlatformDesc& desc) {
    Platform p;
    p.desc = desc;
    const bool moving = desc.travel.x != 0.0f || desc.travel.y != 0.0f;
    // phase 0..0.5 goes out, 0.5..1 comes back
    const float phase = desc.phase - std::floor(desc.phase);
    p.progress = phase <= 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
    p.direction = phase < 0.5f ? 1 : -1;
    p.exact = moving ? pathPoint(desc, p.progress)
                     : Vec2{static_cast<float>(desc.rect.x), static_cast<float>(desc.rect.y)};
    p.rect = {static_cast<int32_t>(std::lround(p.exact.x)), static_cast<int32_t>(std::lround(p.exact.y)),
              desc.rect.width, desc.rect.height};
    p.state = moving && !desc.falls ? State::Moving : State::Resting;
    platforms_.push_back(p);
    return platforms_.size() - 1;
}

void PlatformSystem::setSolid(size_t index, bool solid) {
    if (index < platforms_.size()) {
        platforms_[index].solid = solid;
    }
}

void PlatformSystem::publish(TileMap& map) const {
    std::vector<IntRect> rects;
    rects.reserve(platforms_.size());
    for (const Platform& p : platforms_) {
        rects.push_back(p.solid && p.state != State::Gone ? p.rect : IntRect{});
    }
    map.setDynamicSolids(std::move(rects));
}

void PlatformSystem::moveBy(size_t index, Vec2i delta, TileMap& map, Body* body, bool riding, StepResult& result) {
    Platform& p = platforms_[index];
    IntRect& published = map.dynamicSolids()[index];
    const auto stepAxis = [&](int32_t amount, int32_t Vec2i::*axis) {
        const int32_t sign = amount > 0 ? 1 : -1;
        Vec2i step;
        step.*axis = sign;
        for (int32_t i = 0; i != amount; i += sign) {
            p.rect = p.rect.offset(step);
            if (!body || !p.solid) {
                continue;
            }
            published = {}; // the platform itself does not block the body it moves
            const bool pushed = body->worldCollider().overlaps(p.rect);
            if (pushed || riding) {
                if (collidesAt(*body, step, map)) {
                    result.crushed = result.crushed || pushed;
                } else {
                    body->position = body->position + step;
                }
            }
            published = p.rect;
        }
    };
    if (delta.x != 0) {
        stepAxis(delta.x, &Vec2i::x);
    }
    if (delta.y != 0) {
        stepAxis(delta.y, &Vec2i::y);
    }
    if (!p.solid || p.state == State::Gone) {
        published = {};
    }
}

PlatformSystem::StepResult PlatformSystem::step(float dt, TileMap& map, Body* body, int bottom) {
    StepResult result;
    publish(map);
    for (size_t i = 0; i < platforms_.size(); ++i) {
        Platform& p = platforms_[i];
        const bool riding = body && p.solid && p.state != State::Gone && standsOn(body->worldCollider(), p.rect);
        result.riding = result.riding || riding;
        const Vec2 before = p.exact;
        Vec2 target = p.exact;
        switch (p.state) {
        case State::Resting:
            if (p.desc.falls && riding) {
                p.state = State::Shaking;
                p.timer = fall_.shakeSeconds;
            }
            break;
        case State::Moving: {
            if (p.timer > 0.0f) {
                p.timer -= dt;
                break;
            }
            const float length = std::hypot(p.desc.travel.x, p.desc.travel.y);
            p.progress += static_cast<float>(p.direction) * p.desc.speed * dt / std::max(length, 1.0f);
            if (p.progress >= 1.0f || p.progress <= 0.0f) {
                p.progress = std::clamp(p.progress, 0.0f, 1.0f);
                p.direction = -p.direction;
                p.timer = p.desc.pause;
            }
            target = pathPoint(p.desc, p.progress);
            break;
        }
        case State::Shaking:
            p.timer -= dt;
            if (p.timer <= 0.0f) {
                p.state = State::Falling;
                p.fallSpeed = 0.0f;
            }
            break;
        case State::Falling:
            p.fallSpeed = std::min(p.fallSpeed + fall_.gravity * dt, fall_.maxSpeed);
            target.y += p.fallSpeed * dt;
            break;
        case State::Gone:
            p.velocity = {};
            p.timer -= dt;
            if (p.timer <= 0.0f) {
                const IntRect home = p.desc.rect;
                if (!body || !body->worldCollider().overlaps(home)) { // never reappear inside the player
                    p.state = State::Resting;
                    p.exact = {static_cast<float>(home.x), static_cast<float>(home.y)};
                    p.rect = home;
                    map.dynamicSolids()[i] = p.solid ? home : IntRect{};
                }
            }
            continue;
        }
        const Vec2i delta{static_cast<int32_t>(std::lround(target.x)) - p.rect.x,
                          static_cast<int32_t>(std::lround(target.y)) - p.rect.y};
        p.exact = target;
        p.velocity = dt > 0.0f ? (target - before) * (1.0f / dt) : Vec2{};
        if (delta.x != 0 || delta.y != 0) {
            moveBy(i, delta, map, body, riding, result);
        }
        if (p.state == State::Falling && p.rect.y > bottom) {
            p.state = State::Gone;
            p.timer = fall_.returnSeconds;
            map.dynamicSolids()[i] = {};
        }
    }
    return result;
}

} // namespace animal
