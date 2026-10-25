#include <animal/framework/foliage.h>
#include <animal/core/log.h>
#include <animal/framework/level.h>

#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>

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
    // Mostly a slow, broad wave; a small faster ripple keeps neighbouring blades from moving in lockstep.
    return gust * (0.8f * std::sin(t + x * 0.02f) + 0.2f * std::sin(t * 1.7f + x * 0.07f));
}

Vec2 windVectorAt(const WindParams& wind, Vec2 position, double time) {
    Vec2 w{wind.strength * windAt(wind, position.x, time), 0.0f};
    if (wind.gustStrength > 0.0f && wind.gustInterval > 0.0f && wind.gustSpeed != 0.0f) {
        // A narrow pulse travelling at gustSpeed, repeating every gustInterval seconds.
        const double cycle = time / wind.gustInterval -
                             static_cast<double>(position.x) / (static_cast<double>(wind.gustSpeed) * wind.gustInterval);
        const auto phase = static_cast<float>(cycle - std::floor(cycle));
        const float width = wind.gustWidth / (std::fabs(wind.gustSpeed) * wind.gustInterval);
        if (phase < width) {
            const float pulse = std::sin(kPi * phase / width);
            w.x += (wind.gustSpeed > 0.0f ? 1.0f : -1.0f) * wind.gustStrength * pulse * pulse;
        }
    }
    for (const WindSource& source : wind.sources) {
        const Vec2 offset = position - source.position;
        const float along = offset.x * source.direction.x + offset.y * source.direction.y;
        const float across = std::fabs(offset.x * source.direction.y - offset.y * source.direction.x);
        if (along >= 0.0f && along < source.length && across < source.radius) {
            const float sideways = across / source.radius;
            w += source.direction * (source.strength * (1.0f - along / source.length) * (1.0f - sideways * sideways));
        }
    }
    return w;
}

// --- Plant species -------------------------------------------------------------------------

namespace {

using Json = nlohmann::json;

std::optional<Color> plantColor(const Json& v) {
    if (v.is_string()) {
        const std::string s = v.get<std::string>();
        return s.size() == 7 ? parseColor(s) : std::nullopt;
    }
    if (v.is_array() && v.size() == 4 &&
        std::all_of(v.begin(), v.end(), [](const Json& e) { return e.is_number_integer() && e >= 0 && e <= 255; })) {
        return Color::rgba(v[0].get<uint8_t>(), v[1].get<uint8_t>(), v[2].get<uint8_t>(), v[3].get<uint8_t>());
    }
    return std::nullopt;
}

} // namespace

std::optional<PlantSpecies> parsePlant(std::string_view text, const std::string& path, std::string* error) {
    auto fail = [error](const std::string& message) -> std::optional<PlantSpecies> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    Json json;
    try {
        json = Json::parse(text);
    } catch (const Json::exception& e) {
        return fail(path + ": not valid JSON (" + e.what() + ")");
    }
    if (!json.is_object() || !json.contains("format") || json["format"] != "animal-plant") {
        return fail(path + ": not a plant file (\"format\" must be \"animal-plant\")");
    }
    if (!json.contains("version") || json["version"] != 1) {
        return fail(path + ": unsupported version (expected 1)");
    }
    PlantSpecies species;
    const std::u8string file = std::filesystem::path(std::u8string(path.begin(), path.end())).filename().u8string();
    species.name.assign(file.begin(), file.end());
    if (const size_t dot = species.name.find('.'); dot != std::string::npos) {
        species.name.resize(dot);
    }
    using Reader = std::function<const char*(const Json&)>;
    auto whole = [&species](int PlantSpecies::*m, int lo) -> Reader {
        return [&species, m, lo](const Json& v) -> const char* {
            if (!v.is_number_integer() || v.get<int>() < lo) {
                return lo > 0 ? "a whole number >= 1" : "a whole number >= 0";
            }
            species.*m = v.get<int>();
            return nullptr;
        };
    };
    auto number = [&species](float PlantSpecies::*m) -> Reader {
        return [&species, m](const Json& v) -> const char* {
            if (!v.is_number()) {
                return "a number";
            }
            species.*m = v.get<float>();
            return nullptr;
        };
    };
    auto flag = [&species](bool PlantSpecies::*m) -> Reader {
        return [&species, m](const Json& v) -> const char* {
            if (!v.is_boolean()) {
                return "true or false";
            }
            species.*m = v.get<bool>();
            return nullptr;
        };
    };
    const std::map<std::string, Reader> readers = {
        {"heightMin", whole(&PlantSpecies::heightMin, 1)},
        {"heightMax", whole(&PlantSpecies::heightMax, 1)},
        {"spacing", whole(&PlantSpecies::spacing, 1)},
        {"gaps", number(&PlantSpecies::gaps)},
        {"hanging", flag(&PlantSpecies::hanging)},
        {"stiffness", number(&PlantSpecies::stiffness)},
        {"damping", number(&PlantSpecies::damping)},
        {"windLean", number(&PlantSpecies::windLean)},
        {"flutter", number(&PlantSpecies::flutter)},
        {"maxBend", number(&PlantSpecies::maxBend)},
        {"colors",
         [&species](const Json& v) -> const char* {
             if (!v.is_array() || v.size() != 3) {
                 return "three colours [base, middle, tip]";
             }
             for (size_t i = 0; i < 3; ++i) {
                 const auto c = plantColor(v[i]);
                 if (!c) {
                     return "three colours [base, middle, tip]";
                 }
                 species.colors[i] = *c;
             }
             return nullptr;
         }},
        {"flowerColor",
         [&species](const Json& v) -> const char* {
             const auto c = plantColor(v);
             if (!c) {
                 return "a colour \"#rrggbb\" or [r, g, b, a]";
             }
             species.flowerColor = *c;
             return nullptr;
         }},
        {"flowerGlow", flag(&PlantSpecies::flowerGlow)},
        {"flowerLightRadius", number(&PlantSpecies::flowerLightRadius)},
        {"flowerLightColor",
         [&species](const Json& v) -> const char* {
             const auto c = plantColor(v);
             if (!c) {
                 return "a colour \"#rrggbb\" or [r, g, b, a]";
             }
             species.flowerLightColor = {c->r / 255.0f, c->g / 255.0f, c->b / 255.0f};
             return nullptr;
         }},
        {"flowerLightIntensity", number(&PlantSpecies::flowerLightIntensity)},
    };
    for (const auto& [name, value] : json.items()) {
        if (name == "format" || name == "version") {
            continue;
        }
        const auto it = readers.find(name);
        if (it == readers.end()) {
            return fail(path + ": unknown field \"" + name + "\"");
        }
        if (const char* expected = it->second(value)) {
            return fail(path + ": \"" + name + "\" must be " + expected);
        }
    }
    if (species.heightMax < species.heightMin) {
        return fail(path + ": \"heightMax\" must not be below \"heightMin\"");
    }
    return species;
}

const PlantSpecies* PlantLibrary::load(const std::string& path) {
    if (const auto it = cache_.find(path); it != cache_.end()) {
        return it->second ? &*it->second : nullptr;
    }
    std::optional<PlantSpecies> species;
    if (const auto text = readText(path)) {
        std::string error;
        species = parsePlant(*text, path, &error);
        if (!species) {
            log::error("Invalid plant: {}", error);
        }
    } else {
        log::error("Cannot read plant '{}'", path);
    }
    const auto it = cache_.emplace(path, std::move(species)).first;
    return it->second ? &*it->second : nullptr;
}

std::vector<std::string> PlantLibrary::paths() const {
    std::vector<std::string> result;
    for (const auto& [path, species] : cache_) {
        if (species) {
            result.push_back(path);
        }
    }
    return result;
}

// --- Grass ---------------------------------------------------------------------------------

void GrassField::clear() {
    blades_.clear();
    species_.clear();
}

size_t GrassField::addSpecies(const PlantSpecies& species) {
    if (species_.size() >= 255) {
        return species_.size() - 1;
    }
    species_.push_back(species);
    return species_.size() - 1;
}

void GrassField::addStrands(const TileMap& map, int tx, int ty, size_t speciesIndex, uint32_t seed) {
    const PlantSpecies& species = species_[speciesIndex];
    const int size = map.tileSize();
    const int range = std::max(1, species.heightMax - species.heightMin + 1);
    for (int px = 0; px < size; px += std::max(1, species.spacing)) {
        const uint32_t h = hash(seed ^ hash(static_cast<uint32_t>(ty * 4096 + tx * size + px)) ^
                                hash(static_cast<uint32_t>(speciesIndex) + 1u));
        if (static_cast<float>(h % 1000u) < species.gaps * 1000.0f) {
            continue; // gaps keep the tufts irregular
        }
        Blade blade;
        blade.x = static_cast<float>(tx * size + px + static_cast<int>((h >> 8) % 2));
        blade.baseY = species.hanging ? static_cast<float>((ty + 1) * size) : static_cast<float>(ty * size - 1);
        blade.height = species.heightMin + static_cast<int>((h >> 12) % static_cast<uint32_t>(range));
        blade.shade = static_cast<uint8_t>((h >> 20) % 3);
        blade.species = static_cast<uint8_t>(speciesIndex);
        blades_.push_back(blade);
    }
}

void GrassField::generate(const TileMap& map, const std::function<bool(TileId)>& isGrassy, uint32_t seed) {
    if (species_.empty()) {
        addSpecies(PlantSpecies{});
    }
    for (int ty = 0; ty < map.height(); ++ty) {
        for (int tx = 0; tx < map.width(); ++tx) {
            if (isGrassy(map.tile(tx, ty)) && !map.isSolid(tx, ty - 1)) {
                addStrands(map, tx, ty, 0, seed);
            }
        }
    }
}

void GrassField::plant(const TileMap& map, const IntRect& area, size_t species, uint32_t seed) {
    if (species >= species_.size()) {
        return;
    }
    const bool hanging = species_[species].hanging;
    const int size = map.tileSize();
    const size_t before = blades_.size();
    for (int ty = 0; ty < map.height(); ++ty) {
        for (int tx = 0; tx < map.width(); ++tx) {
            const int faceY = hanging ? (ty + 1) * size : ty * size;
            const bool exposed = map.isSolid(tx, ty) && !map.isSolid(tx, hanging ? ty + 1 : ty - 1);
            if (exposed && faceY >= area.y && faceY <= area.bottom() && (tx + 1) * size > area.x &&
                tx * size < area.right()) {
                addStrands(map, tx, ty, species, seed);
            }
        }
    }
    // Keep only strands rooted inside the area horizontally.
    blades_.erase(std::remove_if(blades_.begin() + static_cast<std::ptrdiff_t>(before), blades_.end(),
                                 [&area](const Blade& b) {
                                     return b.x < static_cast<float>(area.x) || b.x >= static_cast<float>(area.right());
                                 }),
                  blades_.end());
}

void GrassField::step(float dt, double time, std::span<const Influence> influences, const WindParams& wind) {
    const auto t = static_cast<float>(time);
    for (Blade& blade : blades_) {
        const PlantSpecies& species = species_[blade.species];
        const float height = static_cast<float>(blade.height);
        const float top = species.hanging ? blade.baseY : blade.baseY - height;
        const float bottom = species.hanging ? blade.baseY + height : blade.baseY;
        const Vec2 w = windVectorAt(wind, {blade.x, (top + bottom) * 0.5f}, time);
        float target = species.windLean * w.x;
        // Wind stronger than a breeze (gusts, drafts) also makes strands shiver.
        const float excess = std::max(0.0f, length(w) - 1.0f);
        target += species.flutter * excess * std::sin(t * 13.0f + blade.x * 1.7f + blade.baseY * 0.3f);

        for (const Influence& influence : influences) {
            const IntRect& r = influence.bounds;
            const float left = static_cast<float>(r.x) - params_.pushReach;
            const float right = static_cast<float>(r.right()) + params_.pushReach;
            const bool verticalOverlap = static_cast<float>(r.bottom()) > top && static_cast<float>(r.y) <= bottom;
            if (verticalOverlap && blade.x >= left && blade.x <= right) {
                const float offset = blade.x - centerX(r);
                const float halfSpan = (right - left) * 0.5f;
                const float proximity = 1.0f - std::min(1.0f, std::fabs(offset) / halfSpan);
                const float direction = offset >= 0.0f ? 1.0f : -1.0f;
                target += direction * species.maxBend * (0.35f + 0.65f * proximity);
                target += influence.velocity.x * params_.dragFactor;
            }
            if (influence.landed && !species.hanging) {
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

        target = std::clamp(target, -species.maxBend, species.maxBend);
        const float acceleration =
            -species.stiffness * (blade.angle - target) - species.damping * blade.angularVelocity;
        blade.angularVelocity += acceleration * dt;
        blade.angle = std::clamp(blade.angle + blade.angularVelocity * dt, -species.maxBend, species.maxBend);
    }
}

// --- Vines ---------------------------------------------------------------------------------

void VineSystem::pose(size_t vine, Vec2 anchor, Vec2 end, Vec2 velocity, float dt) {
    if (vine >= vines_.size() || vines_[vine].points.size() < 2) {
        return;
    }
    Vec2 direction = end - anchor;
    const float span = animal::length(direction);
    direction = span > 0.1f ? direction * (1.0f / span) : Vec2{0.0f, 1.0f};
    const float substep = dt / static_cast<float>(std::max(1, params_.substeps));
    const Vec2 displacement = velocity * substep;
    Vine& chain = vines_[vine];
    const int segments = static_cast<int>(chain.points.size()) - 1;
    for (int i = 0; i <= segments; ++i) {
        const Vec2 p = anchor + direction * (params_.segmentLength * static_cast<float>(i));
        chain.points[static_cast<size_t>(i)] = p;
        chain.previous[static_cast<size_t>(i)] = i == 0 ? p : p - displacement;
    }
}

void VineSystem::moveAnchor(size_t vine, Vec2 anchor) {
    if (vine >= vines_.size() || vines_[vine].points.empty()) {
        return;
    }
    const Vec2 delta = anchor - vines_[vine].points[0];
    if (delta.x == 0.0f && delta.y == 0.0f) {
        return;
    }
    Vine& chain = vines_[vine];
    for (size_t i = 0; i < chain.points.size(); ++i) {
        chain.points[i] = chain.points[i] + delta;
        chain.previous[i] = chain.previous[i] + delta;
    }
}

void VineSystem::setEndLength(size_t vine, float length) {
    if (vine >= vines_.size() || vines_[vine].points.size() < 2) {
        return;
    }
    Vine& chain = vines_[vine];
    const int segments = std::clamp(static_cast<int>(std::lround(length / params_.segmentLength)), 2,
                                    static_cast<int>(std::lround(220.0f / params_.segmentLength)));
    while (static_cast<int>(chain.points.size()) - 1 > segments) {
        chain.points.pop_back();
        chain.previous.pop_back();
        chain.forces.pop_back();
    }
    while (static_cast<int>(chain.points.size()) - 1 < segments) {
        const Vec2& last = chain.points.back();
        const Vec2& before = chain.points[chain.points.size() - 2];
        Vec2 step{0.0f, params_.segmentLength};
        const Vec2 along = last - before;
        const float span = animal::length(along);
        if (span > 0.1f) {
            step = along * (params_.segmentLength / span);
        }
        const Vec2 added = last + step;
        chain.points.push_back(added);
        chain.previous.push_back(added - (last - chain.previous.back()));
        chain.forces.push_back({});
    }
}

void VineSystem::addVine(Vec2 anchor, int segments) {
    Vine vine;
    for (int i = 0; i <= segments; ++i) {
        const Vec2 p{anchor.x, anchor.y + static_cast<float>(i) * params_.segmentLength};
        vine.points.push_back(p);
        vine.previous.push_back(p);
    }
    vine.forces.assign(vine.points.size(), Vec2{});
    vines_.push_back(std::move(vine));
}

float VineSystem::length(size_t vine) const {
    return static_cast<float>(vines_[vine].points.size() - 1) * params_.segmentLength;
}

namespace {

// Segment index and fraction for an arc length along a chain of `count` points.
std::pair<size_t, float> locate(float distance, float segmentLength, size_t count) {
    const float max = static_cast<float>(count - 1);
    const float t = std::clamp(distance / segmentLength, 0.0f, max);
    const size_t i = std::min(static_cast<size_t>(t), count - 2);
    return {i, t - static_cast<float>(i)};
}

} // namespace

Vec2 VineSystem::pointAt(const Grip& grip) const {
    const Vine& vine = vines_[grip.vine];
    const auto [i, f] = locate(grip.distance, params_.segmentLength, vine.points.size());
    return vine.points[i] + (vine.points[i + 1] - vine.points[i]) * f;
}

Vec2 VineSystem::velocityAt(const Grip& grip, float dt) const {
    const Vine& vine = vines_[grip.vine];
    const auto [i, f] = locate(grip.distance, params_.segmentLength, vine.points.size());
    const float h = dt / static_cast<float>(std::max(1, params_.substeps));
    const Vec2 a = vine.points[i] - vine.previous[i];
    const Vec2 b = vine.points[i + 1] - vine.previous[i + 1];
    return (a + (b - a) * f) * (1.0f / h);
}

void VineSystem::applyForce(const Grip& grip, Vec2 acceleration) {
    Vine& vine = vines_[grip.vine];
    const auto [i, f] = locate(grip.distance, params_.segmentLength, vine.points.size());
    vine.forces[i] += acceleration * (1.0f - f);
    vine.forces[i + 1] += acceleration * f;
}

void VineSystem::hold(const Grip& grip) {
    Vine& vine = vines_[grip.vine];
    vine.heldPoint = std::max<size_t>(1, locate(grip.distance, params_.segmentLength, vine.points.size()).first);
}

std::optional<VineSystem::Grip> VineSystem::findGrip(const IntRect& rect) const {
    const Vec2 center{static_cast<float>(rect.x) + static_cast<float>(rect.width) * 0.5f,
                      static_cast<float>(rect.y) + static_cast<float>(rect.height) * 0.5f};
    std::optional<Grip> best;
    float bestDistance = 1e9f;
    for (size_t v = 0; v < vines_.size(); ++v) {
        const float total = length(v);
        for (float s = params_.segmentLength; s <= total; s += 1.0f) { // skip the anchor itself
            const Vec2 p = pointAt({v, s});
            if (p.x >= static_cast<float>(rect.x) && p.x <= static_cast<float>(rect.right()) &&
                p.y >= static_cast<float>(rect.y) && p.y <= static_cast<float>(rect.bottom())) {
                const float d = animal::length(p - center);
                if (d < bestDistance) {
                    bestDistance = d;
                    best = Grip{v, s};
                }
            }
        }
    }
    return best;
}

void VineSystem::satisfyConstraint(Vine& vine, size_t i, size_t heldPoint) const {
    Vec2& a = vine.points[i - 1];
    Vec2& b = vine.points[i];
    const Vec2 delta = b - a;
    const float distance = std::max(animal::length(delta), 1e-4f);
    const float error = (distance - params_.segmentLength) / distance;
    // Inverse masses: the anchor is pinned, the climber's points are heavy.
    auto weight = [&](size_t point) {
        if (point == 0) {
            return 0.0f;
        }
        return heldPoint > 0 && (point == heldPoint || point == heldPoint + 1) ? params_.climberInverseMass : 1.0f;
    };
    const float wa = weight(i - 1);
    const float wb = weight(i);
    a = a + delta * (error * wa / (wa + wb));
    b = b - delta * (error * wb / (wa + wb));
}

void VineSystem::step(float dt, double time, std::span<const Influence> influences, const WindParams& wind) {
    // Substeps keep each correction small, so constraints converge without stretching and a
    // push anywhere propagates both up and down the chain (the whole vine swings together).
    const int substeps = std::max(1, params_.substeps);
    const float h = dt / static_cast<float>(substeps);
    const float freeDamping = std::pow(params_.damping, 1.0f / static_cast<float>(substeps));
    const float heldDamping = std::pow(params_.heldDamping, 1.0f / static_cast<float>(substeps));

    for (Vine& vine : vines_) {
        const size_t count = vine.points.size();
        const size_t heldPoint = vine.heldPoint;
        const float damping = heldPoint > 0 ? heldDamping : freeDamping;
        vine.heldPoint = 0;
        // Wind sampled per point once per step (drafts push only the part of the vine inside them).
        std::vector<Vec2> windForce(count);
        for (size_t i = 0; i < count; ++i) {
            windForce[i] = windVectorAt(wind, vine.points[i], time) * params_.windForce;
        }

        for (int s = 0; s < substeps; ++s) {
            for (size_t i = 1; i < count; ++i) {
                Vec2& p = vine.points[i];
                Vec2& prev = vine.previous[i];
                const Vec2 velocity = (p - prev) * damping;
                prev = p;
                p = p + velocity + (windForce[i] + Vec2{0.0f, params_.gravity} + vine.forces[i]) * (h * h);
            }

            // Influences push points out sideways and drag them along.
            for (const Influence& influence : influences) {
                if (!influence.pushesVines) {
                    continue;
                }
                const IntRect& r = influence.bounds;
                const float left = static_cast<float>(r.x) - 1.0f;
                const float right = static_cast<float>(r.right()) + 1.0f;
                const float top = static_cast<float>(r.y) - 1.0f;
                const float bottom = static_cast<float>(r.bottom()) + 1.0f;
                for (size_t i = 1; i < count; ++i) {
                    Vec2& p = vine.points[i];
                    if (p.x > left && p.x < right && p.y > top && p.y < bottom) {
                        const float halfWidth = 0.5f * (right - left);
                        const float center = left + halfWidth;
                        const float penetration = 1.0f - std::fabs(p.x - center) / halfWidth; // 0 at edge, 1 at center
                        const float direction = p.x >= center ? 1.0f : -1.0f;
                        p.x += direction * params_.pushAcceleration * penetration * h * h;
                        // Blend the point's velocity toward the (capped) influence velocity.
                        const float target = std::clamp(influence.velocity.x, -params_.maxPushSpeed,
                                                        params_.maxPushSpeed);
                        const float velocity = (p.x - vine.previous[i].x) / h;
                        const float blended =
                            velocity + (target - velocity) * std::min(1.0f, params_.dragCoupling * h);
                        vine.previous[i].x = p.x - blended * h;
                    }
                }
            }

            // Alternate sweep directions so corrections travel both ways along the chain.
            for (int iteration = 0; iteration < params_.iterations; ++iteration) {
                for (size_t i = 1; i < count; ++i) {
                    satisfyConstraint(vine, i, heldPoint);
                }
                for (size_t i = count - 1; i >= 1; --i) {
                    satisfyConstraint(vine, i, heldPoint);
                }
            }
        }
        std::fill(vine.forces.begin(), vine.forces.end(), Vec2{});
    }
}

} // namespace animal
