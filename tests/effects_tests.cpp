// Effect presets and emitters (ADR-0017).

#include <animal/framework/effects.h>
#include <animal/framework/tilemap.h>

#include "test_harness.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

using namespace animal;

constexpr float kDt = 1.0f / 60.0f;
constexpr TileId kSolid = 1;

std::optional<EffectPreset> parse(const std::string& json, std::string* error = nullptr) {
    return parseEffect(json, "world/effects/test.effect.json", error);
}

// 10×6 tiles of 8 px; the left half is solid.
TileMap halfSolidMap() {
    TileMap map(10, 6, 8);
    map.setSolid(kSolid, true);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 5; ++x) {
            map.setTile(x, y, kSolid);
        }
    }
    return map;
}

TEST(effect_parses_fields_and_name) {
    const auto e = parse(R"({"format": "animal-effect", "version": 1, "count": 5, "rate": 2.5, "burst": 3,
        "velocityX": [-1, 2], "drag": 0.5, "lifetime": "forever", "startColor": [10, 20, 30, 40],
        "endColor": "#ff8000", "style": "lit", "lightRadius": 12, "lightColor": "#ffffff", "offset": [1, 2]})");
    CHECK(e.has_value());
    CHECK(e->name == "test");
    CHECK(e->count == 5 && e->burst == 3 && std::fabs(e->rate - 2.5f) < 1e-6f);
    CHECK(e->velocityX.min == -1.0f && e->velocityX.max == 2.0f);
    CHECK(e->lifetime.min > 1.0e8f);
    CHECK(e->startColor == Color::rgba(10, 20, 30, 40));
    CHECK(e->endColor == Color::rgba(255, 128, 0, 255));
    CHECK(e->style == EffectPreset::Style::Lit);
    CHECK((e->offset == Vec2{1.0f, 2.0f}));
    CHECK(std::fabs(e->lightColor.x - 1.0f) < 1e-6f);
    CHECK(!e->gpu);
    const auto gpu = parse(R"({"format": "animal-effect", "version": 1, "gpu": true, "count": 2000})");
    CHECK(gpu && gpu->gpu && gpu->count == 2000);
}

TEST(effect_rejects_bad_files) {
    std::string error;
    CHECK(!parse(R"({"format": "animal-look", "version": 1})", &error));
    CHECK(error.find("not an effect file") != std::string::npos);
    CHECK(!parse(R"({"format": "animal-effect", "version": 1, "cout": 5})", &error));
    CHECK(error.find("unknown field \"cout\"") != std::string::npos);
    CHECK(!parse(R"({"format": "animal-effect", "version": 1, "count": -1})", &error));
    CHECK(!parse(R"({"format": "animal-effect", "version": 1, "lifetime": 0})", &error));
    CHECK(!parse(R"({"format": "animal-effect", "version": 1, "style": "sparkly"})", &error));
    CHECK(error.find("\"style\" must be") != std::string::npos);
    CHECK(!parse(R"({"format": "animal-effect", "version": 1, "startColor": [1, 2, 3]})", &error));
}

TEST(effect_emitters_keep_counts_emit_rates_and_burst) {
    const TileMap map = halfSolidMap();
    EffectPreset ambient;
    ambient.count = 6;
    ambient.lifetime = {0.5f, 0.5f};
    EffectPreset stream;
    stream.rate = 30.0f;
    stream.lifetime = {100.0f, 100.0f};
    EffectPreset sparks;
    sparks.burst = 9;
    sparks.lifetime = {100.0f, 100.0f};

    EffectSystem effects;
    effects.reset(7);
    const auto a = *effects.addPreset(ambient);
    const auto s = *effects.addPreset(stream);
    const auto b = *effects.addPreset(sparks);
    CHECK(*effects.addPreset(ambient) == a); // same preset object, same id
    effects.addEmitter(a, {}, IntRect{40, 0, 40, 48});
    effects.addEmitter(a, {}, IntRect{40, 0, 40, 48}, 2); // count override
    effects.addEmitter(s, {60.0f, 20.0f});
    effects.start(map);
    CHECK(effects.countOf(a) == 8);

    for (int i = 0; i < 60; ++i) { // 1 s: ambient particles die (0.5 s) and are replaced
        effects.step(kDt, i * kDt, map, {}, {});
    }
    CHECK(effects.countOf(a) == 8);
    CHECK(effects.countOf(s) >= 29 && effects.countOf(s) <= 30);

    effects.burst(b, {60.0f, 20.0f});
    CHECK(effects.countOf(b) == 9);
}

TEST(effect_area_spawns_avoid_solid_tiles_and_switch_off) {
    const TileMap map = halfSolidMap();
    EffectPreset dust;
    dust.count = 200;
    dust.lifetime = {100.0f, 100.0f};
    EffectSystem effects;
    effects.reset(3);
    effects.addEmitter(*effects.addPreset(dust), {}, map.pixelBounds()); // whole map, half solid
    effects.start(map);
    CHECK(effects.particleCount() == 200);
    // Up to 20 tries per particle: with half the area solid, a miss has odds 2^-20.
    for (const Particle& p : effects.particles()) {
        CHECK(!map.isSolid(static_cast<int>(p.position.x) / 8, static_cast<int>(p.position.y) / 8));
    }
    effects.setEnabled(false);
    CHECK(effects.particleCount() == 0);
    effects.step(kDt, kDt, map, {}, {});
    CHECK(effects.particleCount() == 0);
    effects.setEnabled(true);
    effects.step(kDt, 2 * kDt, map, {}, {});
    CHECK(effects.particleCount() == 200); // population refilled
}

TEST(effect_simulation_is_deterministic_for_a_seed) {
    const TileMap map = halfSolidMap();
    EffectPreset preset;
    preset.rate = 20.0f;
    preset.velocityX = {-10.0f, 10.0f};
    preset.wander = 5.0f;
    preset.lifetime = {0.3f, 2.0f};
    auto run = [&](uint32_t seed) {
        EffectSystem effects;
        effects.reset(seed);
        effects.addEmitter(*effects.addPreset(preset), {60.0f, 20.0f});
        effects.start(map);
        for (int i = 0; i < 120; ++i) {
            effects.step(kDt, i * kDt, map, {}, {});
        }
        std::vector<Vec2> positions;
        for (const Particle& p : effects.particles()) {
            positions.push_back(p.position);
        }
        return positions;
    };
    const auto first = run(11);
    CHECK(!first.empty() && first == run(11));
    CHECK(first != run(12));
}

} // namespace
