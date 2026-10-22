// Wind field and plant species (ADR-0019).

#include <animal/framework/foliage.h>
#include <animal/framework/particles.h>
#include <animal/framework/tilemap.h>

#include "test_harness.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

using namespace animal;

constexpr float kDt = 1.0f / 60.0f;
constexpr TileId kSolid = 1;

TEST(wind_gusts_sweep_across_in_their_direction) {
    WindParams wind;
    wind.strength = 0.0f; // no breeze: only the gust remains
    wind.gustStrength = 2.0f;
    wind.gustInterval = 4.0f;
    wind.gustSpeed = -100.0f;
    wind.gustWidth = 50.0f;
    float strongest = 0.0f;
    double strongestAt100 = 0.0;
    int calmSamples = 0;
    for (int i = 0; i < 240; ++i) { // one interval
        const double t = i * kDt;
        const float w = windVectorAt(wind, {100.0f, 0.0f}, t).x;
        CHECK(w <= 0.0f); // westward only
        if (w < strongest) {
            strongest = w;
            strongestAt100 = t;
        }
        calmSamples += w == 0.0f ? 1 : 0;
    }
    CHECK(strongest < -1.9f);
    CHECK(calmSamples > 180); // a narrow front, calm in between
    // The same front reaches x = 50 half a second later (100 px/s westward).
    float at50 = 0.0f;
    for (int i = 0; i < 240; ++i) {
        at50 = std::min(at50, windVectorAt(wind, {50.0f, 0.0f}, strongestAt100 + 0.5 + (i - 120) * 1e-3).x);
    }
    CHECK(at50 < -1.9f);
}

TEST(wind_sources_blow_along_their_axis_and_fade) {
    WindParams wind;
    wind.strength = 0.0f;
    wind.sources.push_back({{100.0f, 100.0f}, {0.0f, -1.0f}, 3.0f, 60.0f, 8.0f}); // an updraft
    const Vec2 near = windVectorAt(wind, {100.0f, 95.0f}, 0.0);
    const Vec2 far = windVectorAt(wind, {100.0f, 50.0f}, 0.0);
    CHECK(near.y < -2.5f && std::fabs(near.x) < 1e-6f);
    CHECK(far.y < 0.0f && far.y > near.y); // weaker further up
    CHECK(windVectorAt(wind, {100.0f, 30.0f}, 0.0).y == 0.0f);  // beyond its length
    CHECK(windVectorAt(wind, {110.0f, 95.0f}, 0.0).y == 0.0f);  // beside it
    CHECK(windVectorAt(wind, {100.0f, 105.0f}, 0.0).y == 0.0f); // behind it
}

TEST(particles_rise_in_an_updraft) {
    WindParams wind;
    wind.strength = 0.0f;
    wind.sources.push_back({{50.0f, 100.0f}, {0.0f, -1.0f}, 3.0f, 80.0f, 10.0f});
    ParticleSystem particles;
    Particle p;
    p.position = {50.0f, 95.0f};
    p.windResponse = 20.0f;
    p.lifetime = 10.0f;
    particles.emit(p);
    for (int i = 0; i < 30; ++i) {
        particles.step(kDt, i * kDt, nullptr, {}, wind);
    }
    CHECK(particles.particles()[0].position.y < 90.0f);
}

TEST(plant_species_parse_and_reject) {
    std::string error;
    const auto moss = parsePlant(R"({"format": "animal-plant", "version": 1, "hanging": true, "heightMin": 2,
        "heightMax": 6, "colors": ["#102030", "#405060", [1, 2, 3, 255]], "flowerColor": "#ffffff",
        "flowerGlow": true, "flowerLightRadius": 9})", "world/plants/moss.plant.json", &error);
    CHECK(moss.has_value());
    CHECK(moss->name == "moss" && moss->hanging && moss->heightMax == 6);
    CHECK(moss->colors[2] == Color::rgba(1, 2, 3, 255));
    CHECK(moss->flowerGlow && moss->flowerLightRadius == 9.0f);
    CHECK(!parsePlant(R"({"format": "animal-plant", "version": 1, "heigth": 3})", "p.plant.json", &error));
    CHECK(error.find("unknown field \"heigth\"") != std::string::npos);
    CHECK(!parsePlant(R"({"format": "animal-plant", "version": 1, "heightMin": 5, "heightMax": 2})", "p.plant.json",
                      &error));
    CHECK(!parsePlant(R"({"format": "animal-plant", "version": 1, "colors": ["#ffffff"]})", "p.plant.json", &error));
}

// A platform: solid row 5 (y 40..48) from x = 2..7 tiles.
TileMap platformMap() {
    TileMap map(12, 10, 8);
    map.setSolid(kSolid, true);
    for (int x = 2; x < 8; ++x) {
        map.setTile(x, 5, kSolid);
    }
    return map;
}

TEST(plants_grow_on_faces_inside_their_area) {
    const TileMap map = platformMap();
    GrassField plants;
    PlantSpecies upright;
    upright.gaps = 0.0f;
    PlantSpecies hanging;
    hanging.hanging = true;
    hanging.gaps = 0.0f;
    const size_t a = plants.addSpecies(upright);
    const size_t b = plants.addSpecies(hanging);
    plants.plant(map, {16, 0, 24, 80}, a, 7);  // x 16..40: tiles 2-4 of the platform
    plants.plant(map, {0, 0, 96, 80}, b, 7);   // the whole underside
    size_t ups = 0;
    size_t downs = 0;
    for (const auto& blade : plants.blades()) {
        if (blade.species == a) {
            ++ups;
            CHECK(blade.baseY == 39.0f && blade.x >= 16.0f && blade.x < 40.0f);
        } else {
            ++downs;
            CHECK(blade.baseY == 48.0f && blade.x >= 16.0f && blade.x < 64.0f);
        }
    }
    CHECK(ups == 12 && downs == 24); // spacing 2 px, no gaps
    int lowest = 0;
    plants.forEachPixel([&](int, int y, Color) { lowest = std::max(lowest, y); });
    CHECK(lowest > 48); // hanging strands draw downwards
}

TEST(plants_shiver_in_strong_wind_and_settle_in_calm) {
    const TileMap map = platformMap();
    GrassField plants;
    plants.addSpecies(PlantSpecies{});
    plants.plant(map, {16, 0, 48, 80}, 0, 3);
    WindParams draft;
    draft.strength = 0.0f;
    draft.sources.push_back({{40.0f, 39.0f}, {0.0f, -1.0f}, 3.0f, 40.0f, 30.0f});
    float motion = 0.0f;
    for (int i = 0; i < 240; ++i) {
        plants.step(kDt, i * kDt, {}, draft);
        if (i > 180) {
            motion += std::fabs(plants.blades()[3].angularVelocity);
        }
    }
    CHECK(motion > 1.0f); // still moving after 3 s in the updraft

    WindParams calm;
    calm.strength = 0.0f;
    for (int i = 0; i < 300; ++i) {
        plants.step(kDt, i * kDt, {}, calm);
    }
    for (const auto& blade : plants.blades()) {
        CHECK(std::fabs(blade.angle) < 0.01f);
    }
}

} // namespace
