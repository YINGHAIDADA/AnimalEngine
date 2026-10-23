#include "mechanisms.h"

#include "cave_room.h"

#include <animal/core/log.h>
#include <animal/framework/tilemap.h>
#include <animal/renderer/renderer2d.h>

#include <algorithm>
#include <cmath>

namespace game {

namespace {

// Cells of the props material.
constexpr float kPlatformCell = 4.0f;
constexpr float kDoorCell = 5.0f;
constexpr float kSwitchOffCell = 6.0f;
constexpr float kSwitchOnCell = 7.0f;
constexpr float kOrbCell = 8.0f;
constexpr float kGateCell = 9.0f;

constexpr float kDoorOpenSeconds = 0.6f;
constexpr animal::Vec3 kOrbLight{0.35f, 0.7f, 1.0f};
constexpr animal::Vec3 kSwitchLight{0.4f, 1.0f, 0.5f};
constexpr animal::Vec3 kGateLight{0.3f, 0.95f, 0.8f};
constexpr animal::IntRect kPickupArea{-4, -4, 8, 8}; // around the centre

animal::IntRect rectOf(const animal::LevelObject& object) {
    return {static_cast<int>(std::lround(object.position.x)), static_cast<int>(std::lround(object.position.y)),
            static_cast<int>(std::lround(object.size.x)), static_cast<int>(std::lround(object.size.y))};
}

// Repeats an 8×8 cell over a rectangle, cutting the last row / column short.
void tile(animal::Renderer2D& renderer, const animal::SpriteMaterial& material, float cell, animal::RectF area) {
    const float size = static_cast<float>(kTileSize);
    for (float y = 0.0f; y < area.height; y += size) {
        for (float x = 0.0f; x < area.width; x += size) {
            const float w = std::min(size, area.width - x);
            const float h = std::min(size, area.height - y);
            renderer.drawSprite(material, {area.x + x, area.y + y}, {cell * size, 0.0f, w, h});
        }
    }
}

} // namespace

void Mechanisms::clear() {
    platforms_.clear();
    moving_.clear();
    doors_.clear();
    switches_.clear();
    pickups_.clear();
    goals_.clear();
    startedShaking_ = 0;
}

bool Mechanisms::add(const animal::LevelObject& object, const std::string& roomPath, const std::set<std::string>& flags) {
    const animal::Properties& p = object.properties;
    const bool rectangle = !object.point && object.size.x > 0.0f && object.size.y > 0.0f;
    if (object.type == "platform" || object.type == "door" || object.type == "goal") {
        if (!rectangle) {
            animal::log::warn("{}: {} {} must be a rectangle", roomPath, object.type, object.id);
            return true;
        }
    }
    if (object.type == "platform") {
        animal::PlatformDesc desc;
        desc.rect = rectOf(object);
        desc.travel = {p.getFloat("travelX"), p.getFloat("travelY")};
        desc.speed = p.getFloat("speed", desc.speed);
        desc.pause = p.getFloat("pause", desc.pause);
        desc.phase = p.getFloat("phase", desc.phase);
        desc.falls = p.getBool("falls");
        moving_.push_back(platforms_.add(desc));
    } else if (object.type == "door") {
        Door door;
        door.rect = rectOf(object);
        door.flag = p.getString("flag");
        door.gems = p.getInt("gems");
        if (door.flag.empty() == (door.gems <= 0)) {
            animal::log::warn("{}: door {} needs either a \"flag\" or a \"gems\" count", roomPath, object.id);
        }
        animal::PlatformDesc desc;
        desc.rect = door.rect;
        door.platform = platforms_.add(desc);
        doors_.push_back(door);
    } else if (object.type == "switch") {
        Switch s;
        s.topLeft = {std::round(object.position.x) - 4.0f, std::round(object.position.y) - 8.0f};
        s.flag = p.getString("flag");
        s.message = p.getString("message");
        if (s.flag.empty()) {
            animal::log::warn("{}: switch {} has no \"flag\"", roomPath, object.id);
        }
        s.on = flags.contains(switchFlag(s.flag));
        switches_.push_back(s);
    } else if (object.type == "ability") {
        Pickup pickup;
        pickup.center = object.position;
        const std::string name = p.getString("name");
        if (name != "dive") {
            animal::log::warn("{}: ability {} grants \"{}\" (known: dive)", roomPath, object.id, name);
        }
        pickup.flag = abilityFlag(name);
        pickup.message = p.getString("message");
        pickup.taken = flags.contains(pickup.flag);
        pickups_.push_back(pickup);
    } else if (object.type == "goal") {
        goals_.push_back(rectOf(object));
    } else {
        return false;
    }
    return true;
}

void Mechanisms::finish(const std::set<std::string>& flags, size_t gems, animal::TileMap& map) {
    for (Door& door : doors_) {
        const bool open = (!door.flag.empty() && flags.contains(switchFlag(door.flag))) ||
                          (door.gems > 0 && gems >= static_cast<size_t>(door.gems));
        door.open = open;
        door.opened = open ? 1.0f : 0.0f;
        platforms_.setSolid(door.platform, !open);
    }
    platforms_.publish(map);
}

animal::PlatformSystem::StepResult Mechanisms::stepPlatforms(float dt, animal::TileMap& map, animal::Body* player) {
    std::vector<animal::PlatformSystem::State> before;
    before.reserve(platforms_.platforms().size());
    for (const auto& p : platforms_.platforms()) {
        before.push_back(p.state);
    }
    const auto result = platforms_.step(dt, map, player, map.pixelBounds().bottom() + 16);
    startedShaking_ = 0;
    for (size_t i = 0; i < before.size(); ++i) {
        if (before[i] != animal::PlatformSystem::State::Shaking &&
            platforms_.platforms()[i].state == animal::PlatformSystem::State::Shaking) {
            ++startedShaking_;
        }
    }
    return result;
}

Mechanisms::Touch Mechanisms::touch(const animal::IntRect& player) {
    Touch touch;
    for (Switch& s : switches_) {
        const animal::IntRect area{static_cast<int>(s.topLeft.x) + 1, static_cast<int>(s.topLeft.y) + 3, 6, 5};
        if (!s.on && !s.flag.empty() && area.overlaps(player)) {
            s.on = true;
            touch.pressed = true;
            touch.flags.push_back(switchFlag(s.flag));
            if (!s.message.empty()) {
                touch.messages.push_back(s.message);
            }
        }
    }
    for (Pickup& pickup : pickups_) {
        const animal::IntRect area =
            kPickupArea.offset({static_cast<int>(pickup.center.x), static_cast<int>(pickup.center.y)});
        if (!pickup.taken && area.overlaps(player)) {
            pickup.taken = true;
            touch.ability = true;
            touch.flags.push_back(pickup.flag);
            if (!pickup.message.empty()) {
                touch.messages.push_back(pickup.message);
            }
        }
    }
    for (const animal::IntRect& goal : goals_) {
        touch.goal = touch.goal || goal.overlaps(player);
    }
    // Pressing against a closed gate tells what it needs, once per approach.
    for (Door& door : doors_) {
        const animal::IntRect around{door.rect.x - 1, door.rect.y, door.rect.width + 2, door.rect.height};
        const bool touching = !door.open && door.gems > 0 && around.overlaps(player);
        if (touching && !door.touched) {
            touch.gateNeeds = door.gems;
        }
        door.touched = touching;
    }
    return touch;
}

int Mechanisms::update(float dt, const std::set<std::string>& flags, size_t gems, animal::TileMap& map) {
    int opened = 0;
    for (Door& door : doors_) {
        if (!door.open && ((!door.flag.empty() && flags.contains(switchFlag(door.flag))) ||
                           (door.gems > 0 && gems >= static_cast<size_t>(door.gems)))) {
            door.open = true;
            platforms_.setSolid(door.platform, false);
            ++opened;
        }
        if (door.open) {
            door.opened = std::min(1.0f, door.opened + dt / kDoorOpenSeconds);
        }
    }
    if (opened > 0) {
        platforms_.publish(map);
    }
    return opened;
}

void Mechanisms::draw(animal::Renderer2D& renderer, const CaveAssets& assets, double time) const {
    const float size = static_cast<float>(kTileSize);
    for (const size_t index : moving_) {
        const animal::PlatformSystem::Platform& p = platforms_.platforms()[index];
        if (p.state == animal::PlatformSystem::State::Gone) {
            continue;
        }
        float shake = 0.0f;
        if (p.state == animal::PlatformSystem::State::Shaking) {
            shake = static_cast<float>(static_cast<int>(time * 30.0) % 2 * 2 - 1);
        }
        tile(renderer, assets.props, kPlatformCell,
             {static_cast<float>(p.rect.x) + shake, static_cast<float>(p.rect.y), static_cast<float>(p.rect.width),
              static_cast<float>(p.rect.height)});
    }
    for (const Door& door : doors_) {
        // An opening door slides up into the ceiling: its lower part stays visible.
        const float visible = std::round(static_cast<float>(door.rect.height) * (1.0f - door.opened));
        if (visible <= 0.0f) {
            continue;
        }
        tile(renderer, assets.props, door.gems > 0 ? kGateCell : kDoorCell,
             {static_cast<float>(door.rect.x), static_cast<float>(door.rect.y), static_cast<float>(door.rect.width),
              visible});
    }
    for (const Switch& s : switches_) {
        renderer.drawSprite(assets.props, s.topLeft, {(s.on ? kSwitchOnCell : kSwitchOffCell) * size, 0.0f, size, size});
    }
    for (const Pickup& pickup : pickups_) {
        if (!pickup.taken) {
            const float bob = std::round(std::sin(static_cast<float>(time) * 2.0f) * 1.5f);
            renderer.drawSprite(assets.props, {pickup.center.x - 4.0f, pickup.center.y - 4.0f + bob},
                                {kOrbCell * size, 0.0f, size, size});
        }
    }
}

void Mechanisms::addLights(animal::Renderer2D& renderer, double time) const {
    for (const Pickup& pickup : pickups_) {
        if (!pickup.taken) {
            const float pulse = 1.0f + 0.15f * std::sin(static_cast<float>(time) * 3.0f);
            renderer.addLight({pickup.center, 48.0f, kOrbLight, 1.2f * pulse, 10.0f});
        }
    }
    for (const Switch& s : switches_) {
        if (s.on) {
            renderer.addLight({s.topLeft + animal::Vec2{4.0f, 3.0f}, 14.0f, kSwitchLight, 0.7f, 6.0f});
        }
    }
    for (const Door& door : doors_) {
        if (door.gems > 0 && !door.open) {
            const animal::Vec2 center{static_cast<float>(door.rect.x) + door.rect.width * 0.5f,
                                      static_cast<float>(door.rect.y) + door.rect.height * 0.5f};
            renderer.addLight({center, 26.0f, kGateLight, 0.6f, 8.0f});
        }
    }
}

void Mechanisms::drawCollision(animal::Renderer2D& renderer) const {
    for (const animal::PlatformSystem::Platform& p : platforms_.platforms()) {
        if (p.solid && p.state != animal::PlatformSystem::State::Gone) {
            renderer.fillRect({static_cast<float>(p.rect.x), static_cast<float>(p.rect.y),
                               static_cast<float>(p.rect.width), static_cast<float>(p.rect.height)},
                              animal::Color::rgba(255, 160, 60, 70));
        }
    }
    for (const animal::IntRect& goal : goals_) {
        renderer.strokeRect({static_cast<float>(goal.x), static_cast<float>(goal.y), static_cast<float>(goal.width),
                             static_cast<float>(goal.height)},
                            animal::Color::rgba(255, 230, 90));
    }
}

} // namespace game
