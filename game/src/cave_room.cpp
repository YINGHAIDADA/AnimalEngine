#include "cave_room.h"

#include <animal/core/profiler.h>

#include <animal/core/log.h>
#include <animal/renderer/fluid.h>
#include <animal/renderer/renderer2d.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <map>

namespace game {

namespace {

constexpr int kVineSegment = 3;          // px, matches VineSystem::Params::segmentLength
constexpr int kVineClearance = 3;        // px left between a vine tip and the ground
constexpr int kMaxVineSegments = 50;

constexpr animal::Vec3 kCandleColor{1.0f, 0.62f, 0.28f};
constexpr animal::Vec3 kSmokeColor{0.85f, 0.85f, 0.9f};
// Effect presets used by props unless a "effect" property names another (relative to the room).
constexpr const char* kCandleEffect = "../effects/ember.effect.json";
constexpr const char* kGemPickupEffect = "../effects/spark.effect.json";
constexpr const char* kSplashEffect = "../effects/splash.effect.json";
constexpr animal::Color kWaterColor = animal::Color::rgba(40, 90, 120);
constexpr animal::Vec3 kGemLight{0.3f, 0.95f, 0.8f};
constexpr animal::IntRect kCrawlerCollider{0, 0, 7, 5}; // inside the 8×8 sprite, feet on row 7
constexpr animal::IntRect kGemArea{-3, -3, 6, 6};       // around the gem centre
constexpr animal::Color kVineStem = animal::Color::rgba(70, 140, 72);
constexpr animal::Color kVineLeaf = animal::Color::rgba(120, 196, 96);
// Grass colors per [shade][level] (base → tip).
constexpr const char* kGrassPlant = "../plants/grass.plant.json"; // species of tiles with the "grass" property

animal::Vec3 toVec3(animal::Color c) {
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f};
}

constexpr animal::LightAnimation kCandleFlicker{animal::LightAnimation::Mode::Flicker, 1.0f, 0.13f};
constexpr animal::Vec3 kHazardGlow{1.0f, 0.22f, 0.14f};
constexpr animal::LightAnimation kHazardPulse{animal::LightAnimation::Mode::Pulse, 0.5f, 0.2f};
constexpr int kHazardRun = 3; // tiles lit by one hazard light
constexpr animal::Color kLanternColor = animal::Color::rgba(255, 214, 150);
constexpr animal::Color kChainColor = animal::Color::rgba(70, 60, 52);
constexpr animal::IntRect kLanternWeight{-3, -4, 6, 8}; // the lantern's body around its centre
constexpr float kPickupHover = 3.0f;                     // px a dynamite pickup floats above the floor
constexpr animal::Vec3 kPickupGlow{1.0f, 0.42f, 0.3f};

// Light settings of a light tile (properties "lightColor", "lightRadius", ...) or of a light /
// lantern object ("color", "radius", ...), over the given defaults. Angles are in degrees;
// direction 0 = right, 90 = down.
animal::PointLight readLight(const animal::Properties& p, bool tile, animal::PointLight light) {
    auto key = [tile](std::string name) {
        if (tile) {
            name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
            name = "light" + name;
        }
        return name;
    };
    const animal::Color fallback = animal::Color::rgba(static_cast<uint8_t>(std::lround(light.color.x * 255.0f)),
                                                       static_cast<uint8_t>(std::lround(light.color.y * 255.0f)),
                                                       static_cast<uint8_t>(std::lround(light.color.z * 255.0f)));
    light.color = toVec3(p.getColor(key("color"), fallback));
    light.radius = p.getFloat(key("radius"), light.radius);
    light.intensity = p.getFloat(key("intensity"), light.intensity);
    light.height = p.getFloat(key("height"), light.height);
    light.castsShadows = p.getBool(key("shadows"), light.castsShadows);
    light.coneAngle = p.getFloat(key("cone"), 0.0f) * animal::kPi / 180.0f;
    const float direction = p.getFloat(key("direction"), 90.0f) * animal::kPi / 180.0f;
    light.direction = {std::cos(direction), std::sin(direction)};
    return light;
}

animal::LightAnimation readLightAnimation(const animal::Properties& p, bool tile, animal::LightAnimation animation,
                                          const std::string& where) {
    const char* modeKey = tile ? "lightAnimation" : "animation";
    if (const std::string name = p.getString(modeKey); !name.empty()) {
        if (const auto mode = animal::parseLightAnimationMode(name)) {
            animation.mode = *mode;
        } else {
            animal::log::warn("{}: unknown light animation '{}' (use none, flicker or pulse)", where, name);
        }
    }
    animation.speed = p.getFloat(tile ? "lightSpeed" : "speed", animation.speed);
    animation.amount = p.getFloat(tile ? "lightAmount" : "amount", animation.amount);
    return animation;
}

// Point objects stand on a surface: the point is the bottom-centre of the 8×8 prop.
animal::Vec2 propTopLeft(const animal::LevelObject& object) {
    return {std::round(object.position.x) - 4.0f, std::round(object.position.y) - 8.0f};
}

} // namespace

bool CaveRoom::build(const animal::Room& room, const std::string& name, const std::set<std::string>& flags, size_t gems,
                     animal::EffectLibrary& effects, animal::PlantLibrary& plants, uint32_t seed) {
    tiles_ = room.tiles;
    mechanisms_.clear();
    presetPaths_.clear();
    gems_.clear();
    crawlers_.clear();
    triggers_.clear();
    effects_.reset(seed);
    spawn_.reset();
    candles_.clear();
    vents_.clear();
    lights_.clear();
    lanterns_.clear();
    largestSwing_ = 0.0f;
    dynamite_.clear();
    pools_.clear();
    gpuEmitters_.clear();
    streams_.clear();
    vines_ = animal::VineSystem();
    sparkPreset_ = effectPreset(effects, room, kGemPickupEffect);

    grass_.clear();
    presetPaths_.insert(animal::resolvePath(room.path, kGrassPlant));
    if (const animal::PlantSpecies* grass = plants.load(animal::resolvePath(room.path, kGrassPlant))) {
        grass_.addSpecies(*grass);
    }
    grass_.generate(tiles_, [&room](animal::TileId id) { return room.propertiesOf(id).getBool("grass"); }, seed);
    std::map<const animal::PlantSpecies*, size_t> speciesIndex;

    // Wind (ADR-0019): the room's breeze and gusts; vents and wind objects add drafts below.
    wind_ = {};
    wind_.strength = room.properties.getFloat("wind.strength", wind_.strength);
    wind_.gustStrength = room.properties.getFloat("wind.gusts", wind_.gustStrength);
    wind_.gustInterval = room.properties.getFloat("wind.gustInterval", wind_.gustInterval);
    wind_.gustSpeed = room.properties.getFloat("wind.gustSpeed", wind_.gustSpeed);

    for (int ty = 0; ty < tiles_.height(); ++ty) {
        for (int tx = 0; tx < tiles_.width(); ++tx) {
            const animal::Properties& p = room.propertiesOf(tiles_.tile(tx, ty));
            if (p.getBool("light")) {
                animal::PointLight defaults;
                defaults.position = {tx * 8.0f + 4.0f, ty * 8.0f + 4.0f};
                defaults.radius = 44.0f;
                defaults.height = 12.0f;
                const std::string where = std::format("{}: tile {}", room.path, tiles_.tile(tx, ty));
                lights_.push_back({readLight(p, true, defaults), readLightAnimation(p, true, {}, where),
                                   static_cast<float>(tx)});
            }
        }
        // Hazards glow faintly red: one small light per run of up to kHazardRun tiles.
        for (int tx = 0; tx < tiles_.width();) {
            const animal::Hazard hazard = tiles_.hazardOf(tiles_.tile(tx, ty));
            if (hazard == animal::Hazard::None) {
                ++tx;
                continue;
            }
            int end = tx;
            while (end < tiles_.width() && end - tx < kHazardRun && tiles_.hazardOf(tiles_.tile(end, ty)) == hazard) {
                ++end;
            }
            const float yOffset = hazard == animal::Hazard::Up ? 6.0f : (hazard == animal::Hazard::Down ? 2.0f : 4.0f);
            animal::PointLight glow;
            glow.position = {(tx + end) * 0.5f * kTileSize, ty * 8.0f + yOffset};
            glow.radius = 10.0f + 4.0f * static_cast<float>(end - tx);
            glow.color = kHazardGlow;
            glow.intensity = 0.55f;
            glow.height = 4.0f;
            glow.castsShadows = false;
            lights_.push_back({glow, kHazardPulse, static_cast<float>(tx + ty)});
            tx = end;
        }
    }

    float phase = 0.0f;
    for (const animal::LevelObject& object : room.objects) {
        if (object.type == "spawn") {
            spawn_ = object.position;
        } else if (object.type == "candle") {
            candles_.push_back({propTopLeft(object), phase});
            phase += 1.7f;
            if (const auto preset = effectPreset(effects, room, object.properties.getString("effect", kCandleEffect))) {
                effects_.addEmitter(*preset, candles_.back().position);
            }
        } else if (object.type == "water") {
            // A rectangle of water; its top edge is the surface at rest.
            const animal::IntRect area{static_cast<int>(object.position.x), static_cast<int>(object.position.y),
                                       static_cast<int>(object.size.x), static_cast<int>(object.size.y)};
            if (object.point || area.width <= 0 || area.height <= 0) {
                animal::log::warn("{}: water {} must be a rectangle", room.path, object.id);
                continue;
            }
            animal::WaterSurface look;
            look.color = object.properties.getColor("color", kWaterColor);
            look.opacity = object.properties.getFloat("opacity", look.opacity);
            look.reflection = object.properties.getFloat("reflection", look.reflection);
            look.refraction = object.properties.getInt("refraction", look.refraction);
            look.opacityPerPixel = object.properties.getFloat("deepening", look.opacityPerPixel);
            const auto splash = effectPreset(effects, room, object.properties.getString("effect", kSplashEffect));
            const animal::Vec2 current{object.properties.getFloat("currentX"), object.properties.getFloat("currentY")};
            // The rectangle's top is the waterline. Solid tiles set the width and each column's floor.
            std::vector<animal::WaterBody> bodies = animal::WaterBody::fit(area, tiles_);
            if (bodies.empty()) {
                animal::log::warn("{}: water {} covers no open tiles", room.path, object.id);
                continue;
            }
            for (animal::WaterBody& body : bodies) {
                pools_.push_back({std::move(body), look, splash, current});
            }
        } else if (object.type == "emitter") {
            // A point emitter, or an area emitter for rectangles; "effect" names the preset file.
            const std::string file = object.properties.getString("effect");
            const auto preset = file.empty() ? std::nullopt : effectPreset(effects, room, file);
            if (!preset) {
                animal::log::warn("{}: emitter {} has no usable \"effect\" file", room.path, object.id);
                continue;
            }
            std::optional<animal::IntRect> area;
            if (!object.point && object.size.x > 0.0f && object.size.y > 0.0f) {
                area = animal::IntRect{static_cast<int>(object.position.x), static_cast<int>(object.position.y),
                                       static_cast<int>(object.size.x), static_cast<int>(object.size.y)};
            }
            const auto count = object.properties.has("count") ? std::optional<int>(object.properties.getInt("count"))
                                                              : std::nullopt;
            const auto rate = object.properties.has("rate") ? std::optional<float>(object.properties.getFloat("rate"))
                                                            : std::nullopt;
            const animal::EffectPreset& e = effects_.presets()[*preset];
            if (e.gpu) {
                // GPU particles (ADR-0020); "floor" is a world y below which they vanish (a water surface).
                animal::GpuEmitter g;
                g.area = area ? animal::RectF{static_cast<float>(area->x), static_cast<float>(area->y),
                                              static_cast<float>(area->width), static_cast<float>(area->height)}
                              : animal::RectF{object.position.x, object.position.y, 1.0f, 1.0f};
                g.count = static_cast<uint32_t>(std::max(0, count.value_or(e.count)));
                g.velocityX = {e.velocityX.min, e.velocityX.max};
                g.velocityY = {e.velocityY.min, e.velocityY.max};
                g.acceleration = {(e.accelerationX.min + e.accelerationX.max) * 0.5f,
                                  (e.accelerationY.min + e.accelerationY.max) * 0.5f};
                g.drag = e.drag;
                g.windResponse = e.windResponse;
                g.lifetime = {e.lifetime.min, e.lifetime.max};
                g.fade = e.fade;
                g.floorY = object.properties.getFloat("floor", g.floorY);
                g.dieOnSolid = e.dieOnSolid;
                g.startColor = e.startColor;
                g.endColor = e.endColor;
                g.style = e.style == animal::EffectPreset::Style::Glow ? animal::GpuEmitter::Style::Glow
                                                                         : animal::GpuEmitter::Style::Lit;
                gpuEmitters_.push_back(g);
                continue;
            }
            effects_.addEmitter(*preset, object.position, area, count, rate);
        } else if (object.type == "dynamite") {
            dynamite_.push_back({propTopLeft(object), static_cast<float>(object.id) * 1.1f, false});
        } else if (object.type == "vent") {
            vents_.push_back({propTopLeft(object), object.properties.getFloat("rate", 6.0f),
                              object.properties.getFloat("force", 420.0f)});
            animal::WindSource updraft;
            updraft.position = vents_.back().position + animal::Vec2{4.0f, 2.0f};
            updraft.strength = vents_.back().force / 200.0f;
            updraft.length = 70.0f;
            updraft.radius = 8.0f;
            wind_.sources.push_back(updraft);
        } else if (object.type == "wind") {
            // A draft: blows along "direction" (degrees, 0 = right, 90 = down) from the point.
            animal::WindSource draft;
            draft.position = object.position;
            const float angle = object.properties.getFloat("direction", 0.0f) * animal::kPi / 180.0f;
            draft.direction = {std::cos(angle), std::sin(angle)};
            draft.strength = object.properties.getFloat("strength", draft.strength);
            draft.length = object.properties.getFloat("length", draft.length);
            draft.radius = object.properties.getFloat("radius", draft.radius);
            wind_.sources.push_back(draft);
        } else if (object.type == "plants") {
            // Plants on the exposed tile faces inside the rectangle; "plant" names the species file.
            const std::string file = object.properties.getString("plant");
            if (!file.empty()) {
                presetPaths_.insert(animal::resolvePath(room.path, file));
            }
            const animal::PlantSpecies* species = file.empty() ? nullptr : plants.load(animal::resolvePath(room.path, file));
            if (!species || object.point) {
                animal::log::warn("{}: plants {} needs a rectangle and a usable \"plant\" file", room.path, object.id);
                continue;
            }
            auto [it, added] = speciesIndex.try_emplace(species, 0);
            if (added) {
                it->second = grass_.addSpecies(*species);
            }
            grass_.plant(tiles_,
                         {static_cast<int>(object.position.x), static_cast<int>(object.position.y),
                          static_cast<int>(object.size.x), static_cast<int>(object.size.y)},
                         it->second, seed ^ static_cast<uint32_t>(object.id));
        } else if (object.type == "vine") {
            // Hang down to just above the first solid tile below the anchor.
            const int tx = static_cast<int>(std::floor(object.position.x / kTileSize));
            int row = static_cast<int>(std::floor(object.position.y / kTileSize));
            const int anchorRow = row;
            while (row < tiles_.height() && !tiles_.isSolid(tx, row)) {
                ++row;
            }
            const int drop = (row - anchorRow) * kTileSize - kVineClearance;
            const int segments = object.properties.getInt("segments", std::clamp(drop / kVineSegment, 2, kMaxVineSegments));
            vines_.addVine(object.position, segments);
        } else if (object.type == "gem") {
            Gem gem;
            gem.flag = gemFlag(name, object.id);
            gem.center = object.position;
            gem.phase = static_cast<float>(object.id) * 0.9f;
            gem.collected = flags.count(gem.flag) != 0;
            gems_.push_back(gem);
        } else if (object.type == "crawler") {
            Crawler crawler;
            crawler.body.collider = kCrawlerCollider;
            crawler.body.position = {static_cast<int>(std::lround(object.position.x)) - kCrawlerCollider.width / 2,
                                     static_cast<int>(std::lround(object.position.y)) - kCrawlerCollider.height};
            crawler.patrol.direction = object.id % 2 == 0 ? 1.0f : -1.0f;
            crawlers_.push_back(crawler);
        } else if (object.type == "trigger") {
            animal::TriggerSet::Trigger trigger;
            trigger.id = object.id;
            trigger.area = {static_cast<int>(object.position.x), static_cast<int>(object.position.y),
                            static_cast<int>(object.size.x), static_cast<int>(object.size.y)};
            trigger.once = object.properties.getBool("once", true);
            trigger.message = object.properties.getString("message");
            triggers_.add(trigger);
        } else if (object.type == "light" || object.type == "lantern") {
            const bool lantern = object.type == "lantern";
            const std::string where = std::format("{}: object {}", room.path, object.id);
            animal::PointLight defaults;
            defaults.position = object.position;
            animal::LightAnimation animation;
            if (lantern) {
                // The point is the centre of the lantern; it hangs on a chain from the ceiling above.
                defaults.color = toVec3(kLanternColor);
                defaults.radius = 96.0f;
                defaults.intensity = 1.6f;
                defaults.height = 12.0f;
                animation = {animal::LightAnimation::Mode::Flicker, 0.6f, 0.06f};
                const animal::Vec2 topLeft{std::round(object.position.x) - 4.0f, std::round(object.position.y) - 4.0f};
                const int tx = static_cast<int>(std::floor((topLeft.x + 3.0f) / kTileSize));
                int ty = static_cast<int>(std::floor((topLeft.y - 1.0f) / kTileSize));
                while (ty >= 0 && !tiles_.isSolid(tx, ty)) {
                    --ty;
                }
                const float chainTop = static_cast<float>((ty + 1) * kTileSize);
                animal::Pendulum swing;
                swing.pivot = {topLeft.x + 4.0f, chainTop};
                swing.length = topLeft.y + 4.0f - chainTop;
                lanterns_.push_back({swing, lights_.size()});
            }
            lights_.push_back({readLight(object.properties, false, defaults),
                               readLightAnimation(object.properties, false, animation, where),
                               static_cast<float>(object.id) * 1.3f});
        } else if (!mechanisms_.add(object, room.path, flags)) {
            animal::log::warn("{}: unknown object type '{}' (id {}) ignored", room.path, object.type, object.id);
        }
    }
    mechanisms_.finish(flags, gems, tiles_);

    effects_.start(tiles_);
    // GPU emitters whose floor is a pool's surface pour into it.
    for (const animal::GpuEmitter& g : gpuEmitters_) {
        for (size_t i = 0; i < pools_.size(); ++i) {
            const animal::IntRect& water = pools_[i].body.area();
            if (std::fabs(g.floorY - static_cast<float>(water.y)) < 1.0f && g.area.x + g.area.width > water.x &&
                g.area.x < water.right()) {
                streams_.push_back({i, g.area.x, g.area.x + g.area.width});
            }
        }
    }
    return true;
}

std::optional<animal::EffectSystem::PresetId> CaveRoom::effectPreset(animal::EffectLibrary& library,
                                                                     const animal::Room& room, const std::string& file) {
    presetPaths_.insert(animal::resolvePath(room.path, file));
    const animal::EffectPreset* preset = library.load(animal::resolvePath(room.path, file));
    return preset ? effects_.addPreset(*preset) : std::nullopt;
}

animal::Vec2 CaveRoom::pickupTopLeft(const DynamitePickup& stick) const {
    const float bob = std::round(std::sin(static_cast<float>(lastTime_) * 2.4f + stick.phase) * 1.5f);
    return {stick.topLeft.x, stick.topLeft.y - kPickupHover + bob};
}

int CaveRoom::collectDynamite(const animal::IntRect& player, int capacity) {
    int picked = 0;
    for (DynamitePickup& stick : dynamite_) {
        const animal::Vec2 at = pickupTopLeft(stick);
        const animal::IntRect area{static_cast<int>(at.x) + 2, static_cast<int>(at.y) + 1, 4, 7};
        if (picked < capacity && !stick.taken && area.overlaps(player)) {
            stick.taken = true;
            ++picked;
        }
    }
    return picked;
}

int CaveRoom::blast(animal::Vec2 centre, float radius, float strength) {
    for (Lantern& lantern : lanterns_) {
        const animal::Vec2 away = lantern.swing.bob() - centre;
        const float distance = std::hypot(away.x, away.y);
        if (distance < radius && distance > 0.0f) {
            lantern.swing.push(away * (strength * (1.0f - distance / radius) / distance), lanternParams_);
        }
    }
    const size_t before = crawlers_.size();
    std::erase_if(crawlers_, [&](const Crawler& crawler) {
        const animal::IntRect c = crawler.body.worldCollider();
        const animal::Vec2 middle{static_cast<float>(c.x) + c.width * 0.5f, static_cast<float>(c.y) + c.height * 0.5f};
        const bool hit = std::hypot(middle.x - centre.x, middle.y - centre.y) < radius;
        if (hit && sparkPreset_) {
            effects_.burst(*sparkPreset_, middle);
        }
        return hit;
    });
    return static_cast<int>(before - crawlers_.size());
}

std::vector<std::string> CaveRoom::collectGems(const animal::IntRect& player) {
    std::vector<std::string> picked;
    for (Gem& gem : gems_) {
        if (!gem.collected && kGemArea.offset({static_cast<int>(gem.center.x), static_cast<int>(gem.center.y)}).overlaps(player)) {
            gem.collected = true;
            if (sparkPreset_) {
                effects_.burst(*sparkPreset_, gem.center);
            }
            picked.push_back(gem.flag);
        }
    }
    return picked;
}

std::optional<animal::Vec2> CaveRoom::touchingCrawler(const animal::IntRect& player) const {
    for (const Crawler& crawler : crawlers_) {
        const animal::IntRect c = crawler.body.worldCollider();
        if (c.overlaps(player)) {
            return animal::Vec2{static_cast<float>(c.x) + c.width * 0.5f, static_cast<float>(c.y) + c.height * 0.5f};
        }
    }
    return std::nullopt;
}

std::vector<std::string> CaveRoom::enteredTriggers(const animal::IntRect& player) {
    std::vector<std::string> messages;
    for (const animal::TriggerSet::Trigger* trigger : triggers_.update(player)) {
        if (!trigger->message.empty()) {
            messages.push_back(trigger->message);
        }
    }
    return messages;
}

float CaveRoom::submersion(const animal::IntRect& box) const {
    float deepest = 0.0f;
    for (const Pool& pool : pools_) {
        deepest = std::max(deepest, pool.body.submersion(box));
    }
    return deepest;
}

animal::Vec2 CaveRoom::currentAt(const animal::IntRect& box) const {
    float deepest = 0.0f;
    animal::Vec2 current;
    for (const Pool& pool : pools_) {
        const float s = pool.body.submersion(box);
        if (s > deepest) {
            deepest = s;
            current = pool.current;
        }
    }
    return current;
}

float CaveRoom::updatePlayerInWater(const animal::IntRect& player, animal::Vec2 velocity, float previous) {
    float deepest = 0.0f;
    const float x = static_cast<float>(player.x) + static_cast<float>(player.width) * 0.5f;
    for (Pool& pool : pools_) {
        const float s = pool.body.submersion(player);
        const bool entered = previous <= 0.0f && s > 0.0f;
        const bool left = previous > 0.0f && s <= 0.0f && velocity.y < 0.0f && pool.body.submersion(player.offset({0, 2})) > 0.0f;
        if (entered || left) {
            const float speed = std::min(std::fabs(velocity.y), 300.0f);
            pool.body.disturb(x, entered ? 4.0f : 3.0f, entered ? speed * 0.6f : -speed * 0.3f);
            if (pool.splash && speed > 40.0f) {
                effects_.burst(*pool.splash, {x, pool.body.surfaceY(x) - 1.0f});
            }
        } else if (s > 0.0f && s < 0.95f && std::fabs(velocity.x) > 20.0f) {
            // Wading along the surface pushes a small bow wave ahead.
            pool.body.disturb(x + (velocity.x > 0.0f ? 4.0f : -4.0f), 2.0f, std::fabs(velocity.x) * 0.15f);
        }
        deepest = std::max(deepest, s);
    }
    return deepest;
}

void CaveRoom::addWater(animal::Renderer2D& renderer, double time) const {
    for (const Pool& pool : pools_) {
        animal::WaterSurface surface = pool.look;
        const animal::IntRect& area = pool.body.area();
        surface.topLeft = {static_cast<float>(area.x), static_cast<float>(area.y)};
        surface.width = area.width;
        surface.depth = area.height;
        surface.offsets = pool.body.offsets();
        surface.tops = pool.body.tops();
        surface.floors = pool.body.floors();
        surface.time = static_cast<float>(time);
        renderer.addWater(surface);
        for (const animal::WaterBody::Fall& fall : pool.body.falls()) {
            renderer.addWaterFall(fall.x0, fall.x1, fall.top, fall.bottom);
        }
    }
}

void CaveRoom::step(float dt, double time, std::span<const animal::Influence> influences) {
    const auto t = static_cast<float>(time);
    lastTime_ = time;
    stepDt_ = dt;
    for (Lantern& lantern : lanterns_) {
        for (const animal::Influence& influence : influences) {
            lantern.swing.touch(influence.bounds, influence.velocity, kLanternWeight, lanternParams_, dt);
        }
        lantern.swing.step(lanternParams_, dt);
        lights_[lantern.light].light.position = lantern.swing.bob();
        largestSwing_ = std::max(largestSwing_, std::fabs(lantern.swing.angle));
    }
    for (size_t i = 0; i < streams_.size(); ++i) {
        // The falling water lands somewhere along its width each step.
        const Stream& s = streams_[i];
        const float wander = 0.5f + 0.5f * std::sin(t * 23.0f + static_cast<float>(i) * 1.7f);
        pools_[s.pool].body.disturb(s.left + (s.right - s.left) * wander, 1.5f, 40.0f);
    }
    {
        ANIMAL_PROFILE_ZONE("water");
        const double splashPeriod = 0.45;
        const bool splashNow = std::floor(time / splashPeriod) != std::floor((time - static_cast<double>(dt)) / splashPeriod);
        for (Pool& pool : pools_) {
            pool.body.step(dt);
            if (!splashNow || !pool.splash) {
                continue;
            }
            for (const animal::WaterBody::Fall& fall : pool.body.falls()) {
                const float x = static_cast<float>(fall.x0 + fall.x1) * 0.5f;
                effects_.burst(*pool.splash, {x, static_cast<float>(fall.bottom) - 1.0f});
            }
        }
    }
    {
        ANIMAL_PROFILE_ZONE("creatures");
        for (Crawler& crawler : crawlers_) {
            animal::stepPatrol(crawler.body, crawler.patrol, patrolParams_, tiles_, dt);
            crawler.walkTime += dt;
        }
    }
    {
        ANIMAL_PROFILE_ZONE("plants");
        grass_.step(dt, time, influences, wind_);
    }
    {
        ANIMAL_PROFILE_ZONE("vines");
        vines_.step(dt, time, influences, wind_);
    }
    ANIMAL_PROFILE_ZONE("particles");
    effects_.step(dt, time, tiles_, influences, wind_);
}

void CaveRoom::drawBehindPlayer(animal::Renderer2D& renderer, const CaveAssets& assets) const {
    const float size = static_cast<float>(kTileSize);
    for (int ty = 0; ty < tiles_.height(); ++ty) {
        for (int tx = 0; tx < tiles_.width(); ++tx) {
            const animal::TileId tile = tiles_.tile(tx, ty);
            if (tile != 0) {
                const bool back = !tiles_.isSolidId(tile) && tiles_.hazardOf(tile) == animal::Hazard::None;
                renderer.setLayerDepth(back ? 1.0f : 0.0f);
                renderer.drawSprite(assets.tileset, {tx * size, ty * size}, {tile * size, 0.0f, size, size});
            }
        }
    }
    renderer.setLayerDepth(0.0f);
    for (const Candle& candle : candles_) {
        renderer.drawSprite(assets.props, candle.position, {0.0f, 0.0f, size, size});
    }
    for (const Vent& vent : vents_) {
        renderer.drawSprite(assets.props, vent.position, {size, 0.0f, size, size});
    }
    for (const Lantern& lantern : lanterns_) {
        // Chain links alternate between the two ring columns every 2 px, along the rope.
        const animal::Pendulum& s = lantern.swing;
        const animal::Vec2 direction{std::sin(s.angle), std::cos(s.angle)};
        for (float t = 0.0f; t < s.length - 4.0f; t += 1.0f) {
            const animal::Vec2 p = s.pivot + direction * t;
            const float alternate = static_cast<float>(static_cast<int>(s.pivot.y + t) / 2 % 2);
            renderer.fillRect({std::floor(p.x - 1.0f + alternate), std::floor(p.y), 1.0f, 1.0f}, kChainColor);
        }
        const animal::Vec2 bob = s.bob();
        renderer.drawSprite(assets.props, {std::round(bob.x - 4.0f), std::round(bob.y - 4.0f)},
                            {3.0f * size, 0.0f, size, size});
    }
    mechanisms_.draw(renderer, assets, lastTime_);
    for (const DynamitePickup& stick : dynamite_) {
        if (!stick.taken) {
            renderer.drawSprite(assets.props, pickupTopLeft(stick), {12.0f * size, 0.0f, size, size});
        }
    }
    for (const Crawler& crawler : crawlers_) {
        const animal::Vec2i p = crawler.body.position;
        const float frame = static_cast<float>(static_cast<int>(crawler.walkTime * 6.0f) % 2);
        renderer.drawSprite(assets.crawler, {p.x - 0.0f, p.y - 2.0f}, {frame * size, 0.0f, size, size},
                            animal::Color::white(), crawler.patrol.direction < 0.0f);
    }
    vines_.forEachPixel([&](int x, int y, bool leaf) {
        renderer.fillRect({static_cast<float>(x), static_cast<float>(y), 1.0f, 1.0f}, leaf ? kVineLeaf : kVineStem);
    });
}

void CaveRoom::drawGems(animal::Renderer2D& renderer, const CaveAssets& assets, double time) const {
    const float size = static_cast<float>(kTileSize);
    for (const Gem& gem : gems_) {
        if (!gem.collected) {
            const float bob = std::round(std::sin(static_cast<float>(time) * 2.5f + gem.phase) * 1.5f);
            renderer.drawSprite(assets.props, {gem.center.x - 3.5f, gem.center.y - 3.5f + bob},
                                {2.0f * size, 0.0f, size, size});
        }
    }
}

void CaveRoom::drawInFrontOfPlayer(animal::Renderer2D& renderer) const {
    grass_.forEachPixel([&](int x, int y, animal::Color color) {
        renderer.fillRect({static_cast<float>(x), static_cast<float>(y), 1.0f, 1.0f}, color);
    });
    grass_.forEachFlower([&](animal::Vec2 p, const animal::PlantSpecies& species) {
        if (species.flowerGlow) {
            renderer.drawGlow(p, species.flowerColor);
        } else {
            renderer.fillRect({p.x, p.y, 1.0f, 1.0f}, species.flowerColor);
        }
    });
    effects_.draw(renderer);
}

void CaveRoom::drawCollision(animal::Renderer2D& renderer) const {
    const float size = static_cast<float>(kTileSize);
    for (int ty = 0; ty < tiles_.height(); ++ty) {
        for (int tx = 0; tx < tiles_.width(); ++tx) {
            if (tiles_.isSolid(tx, ty)) {
                renderer.fillRect({tx * size, ty * size, size, size}, animal::Color::rgba(255, 60, 60, 50));
            }
        }
    }
    mechanisms_.drawCollision(renderer);
}

void CaveRoom::addLights(animal::Renderer2D& renderer, double time) const {
    for (const Candle& candle : candles_) {
        animal::PointLight light;
        light.position = candle.position + animal::Vec2{3.5f, 1.5f}; // the flame
        light.radius = 60.0f;
        light.color = kCandleColor;
        light.intensity = 1.5f * kCandleFlicker.factor(time, candle.phase);
        light.height = 10.0f;
        renderer.addLight(light);
    }
    for (const LightSource& source : lights_) {
        animal::PointLight light = source.light;
        light.intensity *= source.animation.factor(time, source.phase);
        renderer.addLight(light);
    }
    for (const Gem& gem : gems_) {
        if (!gem.collected) {
            renderer.addLight({gem.center, 22.0f, kGemLight, 0.8f, 8.0f});
        }
    }
    mechanisms_.addLights(renderer, time);
    for (const DynamitePickup& stick : dynamite_) {
        if (!stick.taken) {
            const float pulse = 1.0f + 0.2f * std::sin(static_cast<float>(time) * 3.0f + stick.phase);
            renderer.addLight({pickupTopLeft(stick) + animal::Vec2{4.0f, 5.0f}, 20.0f, kPickupGlow, 0.7f * pulse, 6.0f,
                               false});
        }
    }
    effects_.addLights(renderer);
    grass_.forEachFlower([&](animal::Vec2 p, const animal::PlantSpecies& species) {
        if (species.flowerLightRadius > 0.0f) {
            renderer.addLight({p, species.flowerLightRadius, species.flowerLightColor, species.flowerLightIntensity, 6.0f});
        }
    });
}

void CaveRoom::addFluidSources(animal::Renderer2D& renderer, double time) const {
    float phase = 0.0f;
    for (const Vent& vent : vents_) {
        const auto t = static_cast<float>(time) + phase;
        const float puff = 0.75f + 0.25f * std::sin(t * 1.3f);
        animal::FluidSplat splat;
        splat.position = vent.position + animal::Vec2{4.0f, 5.0f};
        splat.radius = 5.0f;
        // A slight sideways wobble keeps the column from rising perfectly straight.
        splat.force = {90.0f * (std::sin(t * 2.1f) + 0.5f * std::sin(t * 5.3f)), -vent.force * puff};
        splat.color = kSmokeColor;
        splat.smoke = vent.rate * puff;
        renderer.addFluidSplat(splat);
        phase += 2.0f;
    }
}

void CaveRoom::addStirBodies(std::vector<animal::StirBody>& bodies) const {
    constexpr float kLanternRadius = 6.0f;
    constexpr float kCrawlerRadius = 5.0f;
    for (const Lantern& lantern : lanterns_) {
        bodies.push_back({lantern.swing.bob(), lantern.swing.bobVelocity(), kLanternRadius});
    }
    for (const Crawler& crawler : crawlers_) {
        const animal::IntRect box = crawler.body.worldCollider();
        const animal::Vec2 centre{static_cast<float>(box.x) + static_cast<float>(box.width) * 0.5f,
                                  static_cast<float>(box.y) + static_cast<float>(box.height) * 0.5f};
        bodies.push_back({centre, {crawler.patrol.direction * patrolParams_.speed, crawler.patrol.fallSpeed},
                          kCrawlerRadius});
    }
    mechanisms_.forEachMoving([&](const animal::PlatformSystem::Platform& platform) {
        const animal::Vec2 size{static_cast<float>(platform.rect.width), static_cast<float>(platform.rect.height)};
        bodies.push_back(
            {platform.exact + size * 0.5f, platform.velocity, std::clamp(std::max(size.x, size.y) * 0.45f, 6.0f, 24.0f)});
    });
}

float CaveRoom::vineSubstep() const {
    return stepDt_ / static_cast<float>(std::max(1, vines_.params().substeps));
}

std::vector<uint8_t> CaveRoom::fluidObstacles(animal::Vec2 canvasOrigin) const {
    const float cellSize = static_cast<float>(animal::Renderer2D::kCanvasExtent.width) / animal::kFluidGrid.width;
    std::vector<uint8_t> mask(size_t{animal::kFluidGrid.width} * animal::kFluidGrid.height);
    for (uint32_t cy = 0; cy < animal::kFluidGrid.height; ++cy) {
        for (uint32_t cx = 0; cx < animal::kFluidGrid.width; ++cx) {
            const float wx = canvasOrigin.x + (static_cast<float>(cx) + 0.5f) * cellSize;
            const float wy = canvasOrigin.y + (static_cast<float>(cy) + 0.5f) * cellSize;
            const auto tx = static_cast<int>(std::floor(wx / kTileSize));
            const auto ty = static_cast<int>(std::floor(wy / kTileSize));
            mask[cy * animal::kFluidGrid.width + cx] = (!tiles_.inBounds(tx, ty) || tiles_.isSolid(tx, ty)) ? 1 : 0;
        }
    }
    return mask;
}

std::vector<uint8_t> CaveRoom::lightOccluders(animal::Vec2 canvasOrigin) const {
    const animal::rhi::Extent2D canvas = animal::Renderer2D::kCanvasExtent;
    std::vector<uint8_t> mask(size_t{canvas.width} * canvas.height);
    for (uint32_t y = 0; y < canvas.height; ++y) {
        for (uint32_t x = 0; x < canvas.width; ++x) {
            const auto tx = static_cast<int>(std::floor((canvasOrigin.x + static_cast<float>(x)) / kTileSize));
            const auto ty = static_cast<int>(std::floor((canvasOrigin.y + static_cast<float>(y)) / kTileSize));
            mask[y * canvas.width + x] = tiles_.inBounds(tx, ty) && tiles_.isSolid(tx, ty) ? 1 : 0;
        }
    }
    return mask;
}

} // namespace game
