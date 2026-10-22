// Look presets, Tiled overrides and blending (ADR-0015).

#include <animal/framework/look.h>

#include "test_harness.h"

#include <cmath>
#include <map>
#include <string>

namespace {

using namespace animal;

bool near(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}

bool near(Vec3 a, Vec3 b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

TextReader readerFor(std::map<std::string, std::string> files) {
    return [files = std::move(files)](const std::string& path) -> std::optional<std::string> {
        const auto it = files.find(path);
        return it != files.end() ? std::optional<std::string>(it->second) : std::nullopt;
    };
}

std::optional<Look> parse(const std::string& json, std::string* error = nullptr) {
    return parseLook(json, "looks/a.look.json", readerFor({}), Look::engineDefaults(), error);
}

TEST(look_parses_every_kind_over_defaults) {
    const auto look = parse(R"({"format": "animal-look", "version": 1,
        "ambient": "#336699", "bands": 4, "dither": false, "exposure": 1.25,
        "shadowTint": [0.9, 1.0, 1.2]})");
    CHECK(look.has_value());
    CHECK(near(look->ambient, {0.2f, 0.4f, 0.6f}));
    CHECK(look->bands == 4 && !look->dither);
    CHECK(near(look->exposure, 1.25f));
    CHECK(near(look->shadowTint, {0.9f, 1.0f, 1.2f}));
    const Look defaults = Look::engineDefaults();
    CHECK(near(look->vignette, defaults.vignette)); // untouched fields keep the defaults
    CHECK(near(look->rimAmbient, defaults.rimAmbient));
}

TEST(look_inherits_its_base_and_rejects_cycles) {
    const TextReader read = readerFor({
        {"looks/cave.look.json", R"({"format": "animal-look", "version": 1, "exposure": 1.2, "vignette": 0.3})"},
        {"looks/crystal.look.json", R"({"format": "animal-look", "version": 1, "base": "cave.look.json",
                                        "vignette": 0.1})"},
        {"looks/loop_a.look.json", R"({"format": "animal-look", "version": 1, "base": "loop_b.look.json"})"},
        {"looks/loop_b.look.json", R"({"format": "animal-look", "version": 1, "base": "loop_a.look.json"})"},
    });
    std::string error;
    const auto crystal = parseLook(R"({"format": "animal-look", "version": 1, "base": "crystal.look.json"})",
                                   "looks/room.look.json", read, Look::engineDefaults(), &error);
    CHECK(crystal.has_value());
    CHECK(near(crystal->exposure, 1.2f)); // from cave, two levels up
    CHECK(near(crystal->vignette, 0.1f)); // crystal overrides cave

    CHECK(!parseLook(R"({"format": "animal-look", "version": 1, "base": "loop_a.look.json"})",
                     "looks/room.look.json", read, Look::engineDefaults(), &error));
    CHECK(error.find("refers back to itself") != std::string::npos);

    CHECK(!parseLook(R"({"format": "animal-look", "version": 1, "base": "missing.look.json"})",
                     "looks/room.look.json", read, Look::engineDefaults(), &error));
    CHECK(error.find("looks/missing.look.json") != std::string::npos);
}

TEST(look_rejects_foreign_unknown_and_mistyped_fields) {
    std::string error;
    CHECK(!parse(R"({"format": "animal-save", "version": 1})", &error));
    CHECK(error.find("not a look file") != std::string::npos);
    CHECK(!parse(R"({"format": "animal-look", "version": 2})", &error));
    CHECK(error.find("version") != std::string::npos);
    CHECK(!parse(R"({"format": "animal-look", "version": 1, "exposur": 1.0})", &error));
    CHECK(error.find("unknown field \"exposur\"") != std::string::npos);
    CHECK(!parse(R"({"format": "animal-look", "version": 1, "bands": 2.5})", &error));
    CHECK(error.find("\"bands\" must be a whole number") != std::string::npos);
    CHECK(!parse(R"({"format": "animal-look", "version": 1, "ambient": "blue"})", &error));
    CHECK(!parse(R"({"format": "animal-look", "version": 1, "smokeTint": [1, 1]})", &error));
    CHECK(!parse("{not json", &error));
    CHECK(error.find("looks/a.look.json") != std::string::npos);
}

TEST(look_overrides_from_tiled_properties) {
    Properties properties;
    properties.set("look.exposure", 2);                      // ints convert for float fields
    properties.set("look.ambient", Color::rgba(51, 102, 153));
    properties.set("look.highlightTint", std::string("1.1 1 0.9"));
    properties.set("look.smokeTint", Color::rgba(255, 0, 0));
    properties.set("look.bands", 2.5f);                      // mistyped: skipped and reported
    properties.set("look.exposre", 1.0f);                    // unknown: reported
    properties.set("fireflies", 4);                          // not a look property: ignored

    Look look = Look::engineDefaults();
    const int bandsBefore = look.bands;
    const auto problems = applyLookOverrides(look, properties);
    CHECK(near(look.exposure, 2.0f));
    CHECK(near(look.ambient, {0.2f, 0.4f, 0.6f}));
    CHECK(near(look.highlightTint, {1.1f, 1.0f, 0.9f}));
    CHECK(near(look.smokeTint, {1.0f, 0.0f, 0.0f}));
    CHECK(look.bands == bandsBefore);
    CHECK(problems.size() == 2);
}

TEST(look_mix_blends_numbers_and_switches_discrete_fields) {
    Look a = Look::engineDefaults();
    Look b = a;
    a.exposure = 1.0f;
    b.exposure = 2.0f;
    a.ambient = {0.0f, 0.0f, 0.0f};
    b.ambient = {1.0f, 0.5f, 0.0f};
    a.bands = 3;
    b.bands = 8;
    b.shadows = !a.shadows;

    const Look start = mixLooks(a, b, 0.0f);
    CHECK(near(start.exposure, 1.0f) && start.bands == 3 && start.shadows == a.shadows);
    const Look middle = mixLooks(a, b, 0.5f);
    CHECK(near(middle.exposure, 1.5f));
    CHECK(near(middle.ambient, {0.5f, 0.25f, 0.0f}));
    CHECK(middle.bands == 8 && middle.shadows == b.shadows);
    const Look end = mixLooks(a, b, 1.0f);
    CHECK(near(end.exposure, 2.0f) && near(end.ambient, b.ambient));
}

TEST(look_applies_without_touching_switches) {
    Look look = Look::engineDefaults();
    look.exposure = 1.7f;
    look.bands = 6;
    look.smokeTint = {1.0f, 0.5f, 0.25f};
    LightingSettings lighting;
    lighting.enabled = false;
    PostSettings post;
    post.enabled = false;
    FluidSettings fluid;
    fluid.buoyancy = 99.0f;
    applyLook(look, lighting, post, fluid);
    CHECK(lighting.bands == 6 && !lighting.enabled);
    CHECK(near(post.exposure, 1.7f) && !post.enabled);
    CHECK(near(fluid.smokeTint, {1.0f, 0.5f, 0.25f}) && near(fluid.buoyancy, 99.0f));
}

} // namespace
