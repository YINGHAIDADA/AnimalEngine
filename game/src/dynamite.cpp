// Dynamite (ADR-0038): picked up in rooms, thrown forward in an arc with the throw action; the fuse
// blinks faster for two seconds, then it explodes: a puff of smoke in the fluid, sparks, a flash and
// a shock wave that kills crawlers and pushes the player, lanterns, vines and grass away.

#include "sandbox_game.h"

#include <animal/renderer/fluid.h>
#include <animal/renderer/renderer2d.h>

#include <algorithm>
#include <cmath>

namespace game {

namespace {

constexpr float kDynamiteCell = 12.0f; // in the props material
constexpr animal::IntRect kStickCollider{0, 0, 4, 4};
constexpr float kFuseSeconds = 2.0f;
constexpr float kBlastRadius = 30.0f;
constexpr float kBlastStrength = 260.0f; // px/s at the centre (lanterns: per unit of mass)
constexpr float kPlayerPushRadius = 40.0f;
constexpr float kPlayerPush = 240.0f;
constexpr float kBlastSeconds = 0.5f;     // smoke and flash
constexpr float kShockSeconds = 0.15f;    // the shock wave pushing grass and vines
constexpr animal::Vec2 kThrowVelocity{65.0f, -100.0f};
constexpr animal::Vec3 kSmokeColor{0.62f, 0.58f, 0.54f};
constexpr animal::Vec3 kFlashColor{1.0f, 0.62f, 0.3f};
constexpr animal::Vec3 kFuseColor{1.0f, 0.75f, 0.35f};
// The fuse blinks ever faster: lit for the first half of each period.
bool fuseLit(float fuseLeft) {
    const float period = 0.08f + 0.22f * std::clamp(fuseLeft / kFuseSeconds, 0.0f, 1.0f);
    return std::fmod(kFuseSeconds - fuseLeft, period) < period * 0.5f;
}

} // namespace

void SandboxGame::throwDynamite(animal::Entity& player) {
    const animal::CarriedItem* held = loadout_.equipped();
    if (!held || held->id != "dynamite") {
        return;
    }
    if (!loadout_.consumeEquipped()) {
        return;
    }
    const float facing = facingLeft_ ? -1.0f : 1.0f;
    Thrown t;
    t.projectile.body.collider = kStickCollider;
    t.projectile.body.position = player.body.position + animal::Vec2i{facingLeft_ ? -3 : 5, -2};
    if (animal::collidesAt(t.projectile.body, {}, cave_.tiles())) {
        t.projectile.body.position = player.body.position + animal::Vec2i{1, 1}; // against a wall: from the hand
    }
    t.projectile.velocity = {kThrowVelocity.x * facing + playerState_.velocity.x * 0.5f,
                             kThrowVelocity.y + std::min(0.0f, playerState_.velocity.y) * 0.3f};
    t.fuse = kFuseSeconds;
    thrown_.push_back(t);
    playSound(sounds_.toss, player.body.exactPosition().x, 0.7f);
}

void SandboxGame::stepDynamite(float dt) {
    for (Blast& b : blasts_) {
        b.age += dt;
    }
    std::erase_if(blasts_, [](const Blast& b) { return b.age > kBlastSeconds; });
    for (Thrown& t : thrown_) {
        const float x = t.projectile.body.exactPosition().x;
        if (t.projectile.step(throwParams_, cave_.tiles(), dt)) {
            playSound(sounds_.land, x, 0.35f);
        }
        const bool wasLit = fuseLit(t.fuse);
        t.fuse -= dt;
        if (fuseLit(t.fuse) && !wasLit) {
            playSound(sounds_.fuse, x, 0.4f);
        }
    }
    for (size_t i = 0; i < thrown_.size();) {
        if (thrown_[i].fuse <= 0.0f) {
            const animal::IntRect c = thrown_[i].projectile.body.worldCollider();
            const animal::Vec2 centre{static_cast<float>(c.x) + c.width * 0.5f, static_cast<float>(c.y) + c.height * 0.5f};
            thrown_.erase(thrown_.begin() + static_cast<std::ptrdiff_t>(i));
            explode(centre);
        } else {
            ++i;
        }
    }
}

void SandboxGame::explode(animal::Vec2 centre) {
    blasts_.push_back({centre, 0.0f});
    crawlersKilled_ += cave_.blast(centre, kBlastRadius, kBlastStrength);
    playSound(sounds_.explosion, centre.x, 1.0f);
    if (animal::Entity* player = entities_.get(player_)) {
        const animal::Vec2 middle = player->body.exactPosition() + animal::Vec2{3.0f, 3.5f};
        const animal::Vec2 away = middle - centre;
        const float distance = std::hypot(away.x, away.y);
        if (distance < kPlayerPushRadius) {
            const animal::Vec2 direction = distance > 0.5f ? away * (1.0f / distance) : animal::Vec2{0.0f, -1.0f};
            clearGrapple();
            playerState_.velocity = playerState_.velocity + direction * (kPlayerPush * (1.0f - distance / kPlayerPushRadius));
            playerState_.velocity.y = std::min(playerState_.velocity.y, -60.0f); // lifted off the ground
            playerState_.grounded = false;
            playerState_.rising = false;
            climb_.climbing = false;
            diving_ = false;
        }
    }
}

// The shock wave as influences: grass bends, vines and particles fly away from the blast.
void SandboxGame::addBlastInfluences(std::vector<animal::Influence>& influences) const {
    for (const Blast& b : blasts_) {
        if (b.age > kShockSeconds) {
            continue;
        }
        const int x = static_cast<int>(b.centre.x);
        const int y = static_cast<int>(b.centre.y);
        influences.push_back({{x - 40, y - 24, 40, 48}, {-280.0f, -80.0f}, false, true});
        influences.push_back({{x, y - 24, 40, 48}, {280.0f, -80.0f}, false, true});
        influences.push_back({{x - 24, y - 40, 48, 40}, {0.0f, -280.0f}, false, true});
    }
}

void SandboxGame::drawDynamite(animal::Renderer2D& renderer) const {
    const float size = static_cast<float>(kTileSize);
    for (const Thrown& t : thrown_) {
        const animal::Vec2 p = t.projectile.body.exactPosition();
        renderer.drawSprite(assets_.props, {std::round(p.x) - 2.0f, std::round(p.y) - 4.0f},
                            {kDynamiteCell * size, 0.0f, size, size});
        if (fuseLit(t.fuse)) {
            renderer.drawGlow({std::round(p.x) + 3.0f, std::round(p.y) - 4.0f}, animal::Color::rgba(255, 200, 90), 1.0f);
        }
    }
}

void SandboxGame::addDynamiteLights(animal::Renderer2D& renderer) const {
    for (const Thrown& t : thrown_) {
        if (fuseLit(t.fuse)) {
            const animal::Vec2 p = t.projectile.body.exactPosition();
            renderer.addLight({{p.x + 3.0f, p.y - 4.0f}, 18.0f, kFuseColor, 0.9f, 6.0f});
        }
    }
    for (const Blast& b : blasts_) {
        const float fade = 1.0f - b.age / kBlastSeconds;
        renderer.addLight({b.centre, 40.0f + 60.0f * fade, kFlashColor, 3.0f * fade * fade, 14.0f});
    }
}

void SandboxGame::addBlastSmoke(animal::Renderer2D& renderer) const {
    for (const Blast& b : blasts_) {
        const float fade = 1.0f - b.age / kBlastSeconds;
        const float radius = 8.0f + 20.0f * (1.0f - fade);
        renderer.addFluidSplat({b.centre, radius, {0.0f, -260.0f * fade}, kSmokeColor, 140.0f * fade});
        for (const animal::Vec2 direction : {animal::Vec2{1.0f, 0.0f}, animal::Vec2{-1.0f, 0.0f}, animal::Vec2{0.0f, -1.0f},
                                             animal::Vec2{0.0f, 1.0f}}) {
            renderer.addFluidSplat({b.centre + direction * (radius * 0.6f), radius * 0.6f, direction * (900.0f * fade),
                                    kSmokeColor, 40.0f * fade});
        }
    }
}

} // namespace game
