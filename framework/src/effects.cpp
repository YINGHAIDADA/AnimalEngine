#include <animal/core/log.h>
#include <animal/framework/effects.h>
#include <animal/framework/tilemap.h>
#include <animal/platform/platform.h>
#include <animal/renderer/lighting.h>
#include <animal/renderer/renderer2d.h>

#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>

namespace animal {

namespace {

using Json = nlohmann::json;
constexpr const char* kFormat = "animal-effect";
constexpr int kVersion = 1;
constexpr float kForever = 1.0e9f;

std::optional<FloatRange> toRange(const Json& v) {
    if (v.is_number()) {
        return FloatRange{v.get<float>(), v.get<float>()};
    }
    if (v.is_array() && v.size() == 2 && v[0].is_number() && v[1].is_number()) {
        return FloatRange{v[0].get<float>(), v[1].get<float>()};
    }
    return std::nullopt;
}

std::optional<Color> toColor(const Json& v) {
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

// Field name -> reader; a reader returns the expected-type description on failure.
using FieldReader = std::function<const char*(EffectPreset&, const Json&)>;

FieldReader number(float EffectPreset::*member) {
    return [member](EffectPreset& e, const Json& v) -> const char* {
        if (!v.is_number()) {
            return "a number";
        }
        e.*member = v.get<float>();
        return nullptr;
    };
}

FieldReader whole(int EffectPreset::*member) {
    return [member](EffectPreset& e, const Json& v) -> const char* {
        if (!v.is_number_integer() || v.get<int>() < 0) {
            return "a whole number >= 0";
        }
        e.*member = v.get<int>();
        return nullptr;
    };
}

FieldReader flag(bool EffectPreset::*member) {
    return [member](EffectPreset& e, const Json& v) -> const char* {
        if (!v.is_boolean()) {
            return "true or false";
        }
        e.*member = v.get<bool>();
        return nullptr;
    };
}

FieldReader range(FloatRange EffectPreset::*member) {
    return [member](EffectPreset& e, const Json& v) -> const char* {
        const auto r = toRange(v);
        if (!r) {
            return "a number or [min, max]";
        }
        e.*member = *r;
        return nullptr;
    };
}

FieldReader colour(Color EffectPreset::*member) {
    return [member](EffectPreset& e, const Json& v) -> const char* {
        const auto c = toColor(v);
        if (!c) {
            return "a colour \"#rrggbb\" or [r, g, b, a]";
        }
        e.*member = *c;
        return nullptr;
    };
}

const std::map<std::string, FieldReader>& fieldReaders() {
    static const std::map<std::string, FieldReader> readers = {
        {"count", whole(&EffectPreset::count)},
        {"rate", number(&EffectPreset::rate)},
        {"burst", whole(&EffectPreset::burst)},
        {"prewarm", flag(&EffectPreset::prewarm)},
        {"offset",
         [](EffectPreset& e, const Json& v) -> const char* {
             if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number()) {
                 return "[x, y]";
             }
             e.offset = {v[0].get<float>(), v[1].get<float>()};
             return nullptr;
         }},
        {"spreadX", range(&EffectPreset::spreadX)},
        {"spreadY", range(&EffectPreset::spreadY)},
        {"velocityX", range(&EffectPreset::velocityX)},
        {"velocityY", range(&EffectPreset::velocityY)},
        {"speed", range(&EffectPreset::speed)},
        {"angle", range(&EffectPreset::angle)},
        {"accelerationX", range(&EffectPreset::accelerationX)},
        {"accelerationY", range(&EffectPreset::accelerationY)},
        {"drag", number(&EffectPreset::drag)},
        {"windResponse", number(&EffectPreset::windResponse)},
        {"wander", number(&EffectPreset::wander)},
        {"pushResponse", number(&EffectPreset::pushResponse)},
        {"dieOnSolid", flag(&EffectPreset::dieOnSolid)},
        {"lifetime",
         [](EffectPreset& e, const Json& v) -> const char* {
             if (v.is_string() && v.get<std::string>() == "forever") {
                 e.lifetime = {kForever, kForever};
                 return nullptr;
             }
             const auto r = toRange(v);
             if (!r || r->min <= 0.0f || r->max < r->min) {
                 return "seconds > 0, [min, max] or \"forever\"";
             }
             e.lifetime = *r;
             return nullptr;
         }},
        {"fade", number(&EffectPreset::fade)},
        {"flicker", range(&EffectPreset::flicker)},
        {"startColor", colour(&EffectPreset::startColor)},
        {"endColor", colour(&EffectPreset::endColor)},
        {"style",
         [](EffectPreset& e, const Json& v) -> const char* {
             if (v == "lit") {
                 e.style = EffectPreset::Style::Lit;
             } else if (v == "glow") {
                 e.style = EffectPreset::Style::Glow;
             } else {
                 return "\"lit\" or \"glow\"";
             }
             return nullptr;
         }},
        {"halo", number(&EffectPreset::halo)},
        {"lightRadius", number(&EffectPreset::lightRadius)},
        {"lightColor",
         [](EffectPreset& e, const Json& v) -> const char* {
             const auto c = toColor(v);
             if (!c) {
                 return "a colour \"#rrggbb\" or [r, g, b, a]";
             }
             e.lightColor = {c->r / 255.0f, c->g / 255.0f, c->b / 255.0f};
             return nullptr;
         }},
        {"lightIntensity", number(&EffectPreset::lightIntensity)},
        {"lightHeight", number(&EffectPreset::lightHeight)},
    };
    return readers;
}

std::string effectName(const std::string& path) {
    std::u8string stem = std::filesystem::path(std::u8string(path.begin(), path.end())).filename().u8string();
    std::string name(stem.begin(), stem.end());
    constexpr std::string_view kSuffix = ".effect.json";
    if (name.ends_with(kSuffix)) {
        name.resize(name.size() - kSuffix.size());
    }
    return name;
}

float sample(Random& rng, FloatRange r) {
    return r.min == r.max ? r.min : rng.range(r.min, r.max);
}

} // namespace

std::optional<EffectPreset> parseEffect(std::string_view text, const std::string& path, std::string* error) {
    auto fail = [error](const std::string& message) -> std::optional<EffectPreset> {
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
    if (!json.is_object()) {
        return fail(path + ": an effect file must be a JSON object");
    }
    if (!json.contains("format") || json["format"] != kFormat) {
        return fail(path + ": not an effect file (\"format\" must be \"" + kFormat + "\")");
    }
    if (!json.contains("version") || !json["version"].is_number_integer() || json["version"].get<int>() != kVersion) {
        return fail(path + ": unsupported version (expected " + std::to_string(kVersion) + ")");
    }
    EffectPreset effect;
    effect.name = effectName(path);
    const auto& readers = fieldReaders();
    for (const auto& [name, value] : json.items()) {
        if (name == "format" || name == "version") {
            continue;
        }
        const auto it = readers.find(name);
        if (it == readers.end()) {
            return fail(path + ": unknown field \"" + name + "\"");
        }
        if (const char* expected = it->second(effect, value)) {
            return fail(path + ": \"" + name + "\" must be " + expected);
        }
    }
    return effect;
}

const EffectPreset* EffectLibrary::load(const std::string& path) {
    auto it = cache_.find(path);
    if (it == cache_.end()) {
        std::optional<EffectPreset> effect;
        if (const auto text = readText(path)) {
            std::string error;
            effect = parseEffect(*text, path, &error);
            if (!effect) {
                log::error("Invalid effect: {}", error);
            }
        } else {
            log::error("Cannot read effect '{}'", path);
        }
        it = cache_.emplace(path, std::move(effect)).first;
    }
    return it->second ? &*it->second : nullptr;
}

void EffectSystem::reset(uint32_t seed) {
    presets_.clear();
    presetSources_.clear();
    emitters_.clear();
    particles_.clear();
    rng_ = Random(seed);
}

std::optional<EffectSystem::PresetId> EffectSystem::addPreset(const EffectPreset& preset) {
    if (const auto it = std::find(presetSources_.begin(), presetSources_.end(), &preset); it != presetSources_.end()) {
        return static_cast<PresetId>(it - presetSources_.begin());
    }
    if (presets_.size() >= 255) {
        log::warn("EffectSystem: more than 255 effect presets in one room; '{}' ignored", preset.name);
        return std::nullopt;
    }
    presets_.push_back(preset);
    presetSources_.push_back(&preset);
    return static_cast<PresetId>(presets_.size() - 1);
}

void EffectSystem::addEmitter(PresetId preset, Vec2 position, std::optional<IntRect> area, std::optional<int> count,
                              std::optional<float> rate) {
    const EffectPreset& p = presets_[preset];
    emitters_.push_back({preset, position, area, count.value_or(p.count), rate.value_or(p.rate)});
}

void EffectSystem::burst(PresetId preset, Vec2 position) {
    if (!enabled_) {
        return;
    }
    const Emitter emitter{preset, position, std::nullopt, 0, 0.0f};
    for (int i = 0; i < presets_[preset].burst; ++i) {
        spawn(preset, &emitter, 0, nullptr, false);
    }
}

void EffectSystem::start(const TileMap& map) {
    if (!enabled_) {
        return;
    }
    for (size_t i = 0; i < emitters_.size(); ++i) {
        const Emitter& emitter = emitters_[i];
        for (int n = 0; n < emitter.count; ++n) {
            spawn(emitter.preset, &emitter, static_cast<uint16_t>(i + 1), &map, presets_[emitter.preset].prewarm);
        }
    }
}

Vec2 EffectSystem::openPosition(const IntRect& area, const TileMap* map) {
    Vec2 p;
    for (int attempt = 0; attempt < 20; ++attempt) {
        p = {static_cast<float>(area.x) + rng_.range(0.0f, static_cast<float>(area.width)),
             static_cast<float>(area.y) + rng_.range(0.0f, static_cast<float>(area.height))};
        if (!map || !map->isSolid(static_cast<int>(std::floor(p.x / static_cast<float>(map->tileSize()))),
                                  static_cast<int>(std::floor(p.y / static_cast<float>(map->tileSize()))))) {
            break;
        }
    }
    return p;
}

void EffectSystem::spawn(PresetId presetId, const Emitter* emitter, uint16_t source, const TileMap* map,
                         bool randomAge) {
    const EffectPreset& e = presets_[presetId];
    Particle p;
    p.kind = presetId;
    p.source = source;
    if (emitter && emitter->area) {
        p.position = openPosition(*emitter->area, map);
    } else {
        const Vec2 base = emitter ? emitter->position : Vec2{};
        p.position = base + e.offset + Vec2{sample(rng_, e.spreadX), sample(rng_, e.spreadY)};
    }
    p.velocity = {sample(rng_, e.velocityX), sample(rng_, e.velocityY)};
    if (e.speed.max > 0.0f) {
        const float angle = sample(rng_, e.angle) * kPi / 180.0f;
        p.velocity += Vec2{std::cos(angle), std::sin(angle)} * sample(rng_, e.speed);
    }
    p.acceleration = {sample(rng_, e.accelerationX), sample(rng_, e.accelerationY)};
    p.drag = e.drag;
    p.windResponse = e.windResponse;
    p.wander = e.wander;
    p.pushResponse = e.pushResponse;
    p.flicker = sample(rng_, e.flicker);
    p.lifetime = sample(rng_, e.lifetime);
    p.age = randomAge ? rng_.range(0.0f, p.lifetime * 0.8f) : 0.0f;
    p.fade = e.fade;
    p.startColor = e.startColor;
    p.endColor = e.endColor;
    p.dieOnSolid = e.dieOnSolid;
    p.seed = rng_.next();
    particles_.emit(p);
}

void EffectSystem::step(float dt, double time, const TileMap& map, std::span<const Influence> influences,
                        const WindParams& wind) {
    if (!enabled_) {
        lastStepMilliseconds_ = 0.0;
        return;
    }
    const uint64_t begin = platform::nowNanoseconds();
    particles_.step(dt, time, &map, influences, wind);

    std::vector<int> alive(emitters_.size() + 1, 0);
    for (const Particle& p : particles_.particles()) {
        if (p.source < alive.size()) {
            ++alive[p.source];
        }
    }
    for (size_t i = 0; i < emitters_.size(); ++i) {
        Emitter& emitter = emitters_[i];
        const auto source = static_cast<uint16_t>(i + 1);
        for (int n = alive[source]; n < emitter.count; ++n) {
            spawn(emitter.preset, &emitter, source, &map, false);
        }
        emitter.budget += emitter.rate * dt;
        while (emitter.budget >= 1.0f) {
            spawn(emitter.preset, &emitter, source, &map, false);
            emitter.budget -= 1.0f;
        }
    }
    lastStepMilliseconds_ = static_cast<double>(platform::nowNanoseconds() - begin) * 1e-6;
}

void EffectSystem::draw(Renderer2D& renderer) const {
    if (!enabled_) {
        return;
    }
    for (const Particle& p : particles_.particles()) {
        const EffectPreset& e = presets_[p.kind];
        const Color c = particles_.colorOf(p);
        if (e.style == EffectPreset::Style::Lit) {
            renderer.fillRect({std::round(p.position.x), std::round(p.position.y), 1.0f, 1.0f}, c);
            continue;
        }
        renderer.drawGlow(p.position, c);
        if (e.halo > 0.0f) {
            const Color halo{c.r, c.g, c.b, static_cast<uint8_t>(static_cast<float>(c.a) * std::min(e.halo, 1.0f))};
            for (const Vec2 d : {Vec2{1, 0}, Vec2{-1, 0}, Vec2{0, 1}, Vec2{0, -1}}) {
                renderer.drawGlow(p.position + d, halo);
            }
        }
    }
}

void EffectSystem::addLights(Renderer2D& renderer) const {
    if (!enabled_) {
        return;
    }
    for (const Particle& p : particles_.particles()) {
        const EffectPreset& e = presets_[p.kind];
        if (e.lightRadius <= 0.0f) {
            continue;
        }
        PointLight light;
        light.position = p.position;
        light.radius = e.lightRadius;
        light.color = e.lightColor;
        light.intensity = e.lightIntensity * static_cast<float>(particles_.colorOf(p).a) / 255.0f;
        light.height = e.lightHeight;
        renderer.addLight(light);
    }
}

void EffectSystem::setEnabled(bool enabled) {
    if (!enabled) {
        particles_.clear();
    }
    enabled_ = enabled;
}

size_t EffectSystem::countOf(PresetId preset) const {
    return particles_.countOf(preset);
}

} // namespace animal
