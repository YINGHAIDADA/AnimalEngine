#include <animal/framework/pendulum.h>

#include <algorithm>
#include <cmath>

namespace animal {

Vec2 Pendulum::bob() const {
    return {pivot.x + std::sin(angle) * length, pivot.y + std::cos(angle) * length};
}

Vec2 Pendulum::bobVelocity() const {
    return Vec2{std::cos(angle), -std::sin(angle)} * (angularVelocity * length);
}

void Pendulum::touch(const IntRect& bounds, Vec2 velocity, const IntRect& weight, const PendulumParams& params,
                     float dt) {
    const Vec2 centre = bob();
    const IntRect box = weight.offset({static_cast<int>(std::lround(centre.x)), static_cast<int>(std::lround(centre.y))});
    if (!box.overlaps(bounds) || length <= 0.0f) {
        return;
    }
    // Only the motion along the swing (the tangent) moves the weight; heavier weights follow slower.
    const Vec2 tangent{std::cos(angle), -std::sin(angle)};
    const float relative = velocity.x * tangent.x + velocity.y * tangent.y - angularVelocity * length;
    const float rate = std::min(1.0f, params.contact * dt / std::max(params.mass, 0.01f));
    angularVelocity += relative * rate / length;
}

void Pendulum::push(Vec2 impulse, const PendulumParams& params) {
    if (length <= 0.0f) {
        return;
    }
    const Vec2 tangent{std::cos(angle), -std::sin(angle)};
    angularVelocity += (impulse.x * tangent.x + impulse.y * tangent.y) / (length * std::max(params.mass, 0.01f));
}

void Pendulum::step(const PendulumParams& params, float dt) {
    if (length <= 0.0f) {
        return;
    }
    angularVelocity += (-params.gravity / length * std::sin(angle) - params.damping * angularVelocity) * dt;
    angle += angularVelocity * dt;
    if (std::fabs(angle) > params.maxAngle) { // the rope goes slack beyond: stop at the limit
        angle = std::copysign(params.maxAngle, angle);
        angularVelocity = 0.0f;
    }
}

} // namespace animal
