// The grappling hook: a reusable item. Using it fires a hook straight up on a chain. The hook catches
// the first hard surface above it (a solid tile, or a dynamic solid such as a moving platform) and
// sticks. The chain is a pendulum, pumped with left and right the way a vine is. Holding up reels
// it in; holding down pays it out. Jumping or using it again lets go — jumping also leaps off.

#include "sandbox_game.h"

#include <animal/renderer/renderer2d.h>

#include <algorithm>
#include <cmath>
#include <span>

namespace game {

namespace {

constexpr float kFlySpeed = 260.0f;     // px/s the hook climbs
constexpr float kMaxReach = 168.0f;     // px; past this it comes back without catching
constexpr float kMinLength = 12.0f;     // px: the grip stays clear of the surface
constexpr float kMaxLength = 220.0f;
constexpr float kRetractSpeed = 340.0f; // px/s the hook returns to the hand
constexpr float kGrappleCell = 13.0f;   // props sheet
constexpr animal::Color kChain{90, 78, 64, 255};

animal::Vec2 gripOf(const animal::Entity& player) {
    const animal::IntRect box = player.body.worldCollider();
    return {static_cast<float>(box.x) + box.width * 0.5f, static_cast<float>(box.y)};
}

void pullToward(animal::Body& body, animal::Vec2 delta, const animal::TileMap& map) {
    animal::moveX(body, delta.x, map);
    animal::moveY(body, delta.y, map);
}

} // namespace

void SandboxGame::clearGrapple() {
    grapple_.active = false;
    grappleChain_.clear();
}

void SandboxGame::useGrapple(animal::Entity& player) {
    if (grapple_.active && grapple_.phase != Grapple::Phase::Retracting) {
        grapple_.phase = Grapple::Phase::Retracting;
        grappleChain_.clear();
        return;
    }
    if (grapple_.active) {
        return;
    }
    grapple_ = {};
    grapple_.active = true;
    grapple_.phase = Grapple::Phase::Flying;
    grapple_.hook = gripOf(player);
    grapple_.firedFrom = grapple_.hook;
    climb_.climbing = false;
    playSound(sounds_.toss, grapple_.hook.x, 0.6f);
}

bool SandboxGame::stepGrapple(animal::Entity& player, float moveX, float moveY, bool jumpPressed, float dt) {
    if (!grapple_.active) {
        return false;
    }
    const animal::TileMap& map = cave_.tiles();
    const auto grip = [&] { return gripOf(player); };

    if (grapple_.phase == Grapple::Phase::Flying) {
        const float step = kFlySpeed * dt;
        const int substeps = std::max(1, static_cast<int>(std::ceil(step)));
        const float dy = step / static_cast<float>(substeps);
        for (int i = 0; i < substeps; ++i) {
            grapple_.hook.y -= dy;
            if (grapple_.firedFrom.y - grapple_.hook.y > kMaxReach) {
                grapple_.phase = Grapple::Phase::Retracting;
                break;
            }
            if (const std::optional<animal::TileMap::OverheadHit> hit = map.firstSolidAbove(grapple_.hook + animal::Vec2{0.0f, 1.0f}, 1.0f)) {
                grapple_.hook = hit->point;
                grapple_.dynamic = hit->dynamic;
                grapple_.local = hit->local;
                const animal::Vec2 centre = player.body.exactPosition() +
                                            animal::Vec2{static_cast<float>(player.body.collider.x) + player.body.collider.width * 0.5f,
                                                         static_cast<float>(player.body.collider.y) + player.body.collider.height * 0.5f};
                grapple_.length = std::clamp(std::hypot(centre.x - grapple_.hook.x, centre.y - grapple_.hook.y), kMinLength, kMaxLength);
                const float segment = grappleChain_.params().segmentLength;
                const int segments = std::clamp(static_cast<int>(std::lround(grapple_.length / segment)), 2,
                                               static_cast<int>(std::lround(kMaxLength / segment)));
                grappleChain_.clear();
                grappleChain_.addVine(grapple_.hook, segments);
                // Hang the rope toward where the body already is, and keep its speed, so a catch
                // made while jumping or running does not snap the player back under the hook.
                grappleChain_.pose(0, grapple_.hook, centre, playerState_.velocity, dt);
                grappleGrip_ = {0, std::min(grapple_.length, grappleChain_.length(0))};
                grapple_.phase = Grapple::Phase::Latched;
                playSound(sounds_.grab, grapple_.hook.x, 0.8f);
                break;
            }
        }
    }

    if (grapple_.phase == Grapple::Phase::Retracting) {
        const animal::Vec2 toHand = grip() - grapple_.hook;
        const float distance = std::hypot(toHand.x, toHand.y);
        if (distance < 4.0f) {
            grapple_.active = false;
            return false;
        }
        grapple_.hook = grapple_.hook + toHand * (std::min(kRetractSpeed * dt, distance) / distance);
        return false;
    }

    if (grapple_.phase != Grapple::Phase::Latched) {
        return false;
    }

    if (grapple_.dynamic >= 0) {
        const std::span<const animal::IntRect> solids = map.dynamicSolids();
        if (grapple_.dynamic >= static_cast<int>(solids.size()) || solids[static_cast<size_t>(grapple_.dynamic)].width <= 0) {
            grapple_.phase = Grapple::Phase::Retracting;
            return false;
        }
        const animal::IntRect& solid = solids[static_cast<size_t>(grapple_.dynamic)];
        grapple_.hook = {static_cast<float>(solid.x) + grapple_.local.x, static_cast<float>(solid.y) + grapple_.local.y};
        grappleChain_.moveAnchor(0, grapple_.hook); // the pinned end rides the platform
    }

    if (grappleChain_.vines().empty()) {
        grapple_.phase = Grapple::Phase::Retracting;
        return false;
    }

    if (jumpPressed) {
        const animal::Vec2 swing = grappleChain_.velocityAt(grappleGrip_, dt);
        const float side = moveX > 0.25f ? 1.0f : (moveX < -0.25f ? -1.0f : 0.0f);
        playerState_ = {};
        playerState_.velocity = {swing.x * climbParams_.inheritVelocity + side * climbParams_.jumpSideSpeed,
                                 -climbParams_.jumpSpeed};
        playerState_.rising = true;
        playerState_.jumpedThisStep = true;
        grapple_.phase = Grapple::Phase::Retracting;
        grappleChain_.clear();
        playSound(sounds_.jump, grip().x, 0.8f);
        return false;
    }

    // Up reels the rope in, down pays it out. The wanted length is continuous: the vine only
    // gains or loses a segment once that length crosses the next multiple of the segment size.
    if (moveY < -0.5f) {
        grapple_.length = std::max(kMinLength, grapple_.length - climbParams_.climbSpeed * dt);
    } else if (moveY > 0.5f) {
        grapple_.length = std::min(kMaxLength, grapple_.length + climbParams_.climbSpeed * dt);
    }
    grappleChain_.setEndLength(0, grapple_.length);
    grappleGrip_ = {0, std::min(grapple_.length, grappleChain_.length(0))};
    grappleChain_.applyForce(grappleGrip_, {moveX * climbParams_.swingForce, climbParams_.weight});
    grappleChain_.hold(grappleGrip_);

    const animal::Vec2 centre = player.body.exactPosition() +
                                animal::Vec2{static_cast<float>(player.body.collider.x) + player.body.collider.width * 0.5f,
                                             static_cast<float>(player.body.collider.y) + player.body.collider.height * 0.5f};
    pullToward(player.body, grappleChain_.pointAt(grappleGrip_) - centre, map);
    playerState_.velocity = grappleChain_.velocityAt(grappleGrip_, dt);
    playerState_.grounded = false;
    playerState_.rising = false;
    climb_.climbing = false;
    return true;
}

void SandboxGame::drawGrapple(animal::Renderer2D& renderer) const {
    if (!grapple_.active) {
        return;
    }
    const animal::Entity* player = entities_.get(player_);
    if (!player) {
        return;
    }
    const float size = static_cast<float>(kTileSize);
    animal::Vec2 hook = grapple_.hook;
    if (grapple_.phase == Grapple::Phase::Latched && !grappleChain_.vines().empty()) {
        const animal::VineSystem::Vine& chain = grappleChain_.vines().front();
        hook = chain.points.front();
        for (size_t i = 1; i < chain.points.size(); ++i) {
            const animal::Vec2 along = chain.points[i] - chain.points[i - 1];
            const float span = std::hypot(along.x, along.y);
            if (span < 0.5f) {
                continue;
            }
            const animal::Vec2 step = along * (1.0f / span);
            for (float t = 0.0f; t < span; t += 2.0f) {
                const animal::Vec2 p = chain.points[i - 1] + step * t;
                renderer.fillRect({std::floor(p.x), std::floor(p.y), 1.0f, 1.0f}, kChain);
            }
        }
    } else {
        const animal::Vec2 hand = gripOf(*player);
        const animal::Vec2 toHook = hook - hand;
        const float length = std::hypot(toHook.x, toHook.y);
        if (length > 1.0f) {
            const animal::Vec2 step = toHook * (1.0f / length);
            for (float t = 2.0f; t < length - 2.0f; t += 3.0f) {
                const animal::Vec2 p = hand + step * t;
                renderer.fillRect({std::floor(p.x), std::floor(p.y), 1.0f, 1.0f}, kChain);
            }
        }
    }
    renderer.drawSprite(assets_.props, {std::round(hook.x - 3.0f), std::round(hook.y)}, {kGrappleCell * size, 0.0f, size, size});
}

} // namespace game
