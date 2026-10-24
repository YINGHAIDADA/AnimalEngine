#include <animal/core/log.h>
#include <animal/framework/look.h>

#include <json.hpp>

#include <algorithm>
#include <locale>
#include <sstream>
#include <variant>

namespace animal {

namespace {

using Json = nlohmann::json;
constexpr const char* kFormat = "animal-look";
constexpr int kVersion = 1;

enum class Kind {
    Float,
    Int,
    Bool,
    Color, // "#rrggbb" in files, a colour property in Tiled; stored as 0..1
    Tint,  // [r, g, b] in files, a colour or "r g b" in Tiled; may exceed 1
};

struct Field {
    const char* name;
    Kind kind;
    std::variant<float Look::*, int Look::*, bool Look::*, Vec3 Look::*> member;
};

const Field kFields[] = {
    {"ambient", Kind::Color, &Look::ambient},
    {"bands", Kind::Int, &Look::bands},
    {"dither", Kind::Bool, &Look::dither},
    {"shadows", Kind::Bool, &Look::shadows},
    {"shadowPenetration", Kind::Float, &Look::shadowPenetration},
    {"rimIntensity", Kind::Float, &Look::rimIntensity},
    {"rimAmbient", Kind::Color, &Look::rimAmbient},
    {"backLight", Kind::Float, &Look::backLight},
    {"backFlatten", Kind::Float, &Look::backFlatten},
    {"depthFog", Kind::Float, &Look::depthFog},
    {"depthFogColor", Kind::Color, &Look::depthFogColor},
    {"indirect", Kind::Float, &Look::indirect},
    {"ambientOcclusion", Kind::Float, &Look::ambientOcclusion},
    {"ambientGradient", Kind::Float, &Look::ambientGradient},
    {"bloomThreshold", Kind::Float, &Look::bloomThreshold},
    {"bloomKnee", Kind::Float, &Look::bloomKnee},
    {"bloomIntensity", Kind::Float, &Look::bloomIntensity},
    {"bloomBands", Kind::Int, &Look::bloomBands},
    {"exposure", Kind::Float, &Look::exposure},
    {"contrast", Kind::Float, &Look::contrast},
    {"saturation", Kind::Float, &Look::saturation},
    {"shadowTint", Kind::Tint, &Look::shadowTint},
    {"highlightTint", Kind::Tint, &Look::highlightTint},
    {"vignette", Kind::Float, &Look::vignette},
    {"smokeTint", Kind::Tint, &Look::smokeTint},
    {"smokeOpacity", Kind::Float, &Look::smokeOpacity},
};

const Field* findField(std::string_view name) {
    for (const Field& field : kFields) {
        if (name == field.name) {
            return &field;
        }
    }
    return nullptr;
}

const char* expected(Kind kind) {
    switch (kind) {
    case Kind::Float:
        return "a number";
    case Kind::Int:
        return "a whole number";
    case Kind::Bool:
        return "true or false";
    case Kind::Color:
        return "a colour \"#rrggbb\"";
    case Kind::Tint:
        return "a tint [r, g, b]";
    }
    return "";
}

Vec3 toVec3(Color c) {
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f};
}

// One of the values below, already converted for the field's kind.
using Converted = std::variant<float, int, bool, Vec3>;

void store(Look& look, const Field& field, const Converted& value) {
    std::visit(
        [&look, &value](auto member) {
            using T = std::remove_reference_t<decltype(look.*member)>;
            look.*member = std::get<T>(value);
        },
        field.member);
}

std::optional<Converted> fromJson(Kind kind, const Json& v) {
    switch (kind) {
    case Kind::Float:
        return v.is_number() ? std::optional<Converted>(v.get<float>()) : std::nullopt;
    case Kind::Int:
        return v.is_number_integer() ? std::optional<Converted>(v.get<int>()) : std::nullopt;
    case Kind::Bool:
        return v.is_boolean() ? std::optional<Converted>(v.get<bool>()) : std::nullopt;
    case Kind::Color:
        if (v.is_string()) {
            if (const auto c = parseColor(v.get<std::string>())) {
                return toVec3(*c);
            }
        }
        return std::nullopt;
    case Kind::Tint:
        if (v.is_array() && v.size() == 3 && std::all_of(v.begin(), v.end(), [](const Json& e) { return e.is_number(); })) {
            return Vec3{v[0].get<float>(), v[1].get<float>(), v[2].get<float>()};
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<Vec3> parseTriple(const std::string& text) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    Vec3 v;
    if (in >> v.x >> v.y >> v.z && (in >> std::ws).eof()) {
        return v;
    }
    return std::nullopt;
}

std::optional<Converted> fromProperty(Kind kind, const Properties::Value& v) {
    switch (kind) {
    case Kind::Float:
        if (const float* f = std::get_if<float>(&v)) {
            return *f;
        }
        if (const int* i = std::get_if<int>(&v)) {
            return static_cast<float>(*i);
        }
        return std::nullopt;
    case Kind::Int:
        return std::holds_alternative<int>(v) ? std::optional<Converted>(std::get<int>(v)) : std::nullopt;
    case Kind::Bool:
        return std::holds_alternative<bool>(v) ? std::optional<Converted>(std::get<bool>(v)) : std::nullopt;
    case Kind::Color:
        return std::holds_alternative<Color>(v) ? std::optional<Converted>(toVec3(std::get<Color>(v))) : std::nullopt;
    case Kind::Tint:
        if (const Color* c = std::get_if<Color>(&v)) {
            return toVec3(*c);
        }
        if (const std::string* s = std::get_if<std::string>(&v)) {
            if (const auto triple = parseTriple(*s)) {
                return *triple;
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<Look> parseLookChain(std::string_view text, const std::string& path, const TextReader& read,
                                   const Look& defaults, std::string* error, std::vector<std::string>& chain) {
    auto fail = [error](const std::string& message) -> std::optional<Look> {
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
        return fail(path + ": a look file must be a JSON object");
    }
    if (!json.contains("format") || json["format"] != kFormat) {
        return fail(path + ": not a look file (\"format\" must be \"" + kFormat + "\")");
    }
    if (!json.contains("version") || !json["version"].is_number_integer() || json["version"].get<int>() != kVersion) {
        return fail(path + ": unsupported version (expected " + std::to_string(kVersion) + ")");
    }

    Look look = defaults;
    chain.push_back(path);
    if (json.contains("base")) {
        if (!json["base"].is_string()) {
            return fail(path + ": \"base\" must be a file name");
        }
        const std::string basePath = resolvePath(path, json["base"].get<std::string>());
        if (std::find(chain.begin(), chain.end(), basePath) != chain.end()) {
            return fail(path + ": base \"" + basePath + "\" refers back to itself");
        }
        const std::optional<std::string> baseText = read(basePath);
        if (!baseText) {
            return fail(path + ": cannot read base \"" + basePath + "\"");
        }
        const std::optional<Look> base = parseLookChain(*baseText, basePath, read, defaults, error, chain);
        if (!base) {
            return std::nullopt;
        }
        look = *base;
    }

    for (const auto& [name, value] : json.items()) {
        if (name == "format" || name == "version" || name == "base") {
            continue;
        }
        const Field* field = findField(name);
        if (!field) {
            return fail(path + ": unknown field \"" + name + "\"");
        }
        const std::optional<Converted> converted = fromJson(field->kind, value);
        if (!converted) {
            return fail(path + ": \"" + name + "\" must be " + expected(field->kind));
        }
        store(look, *field, *converted);
    }
    return look;
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

Vec3 lerp(Vec3 a, Vec3 b, float t) {
    return {lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t)};
}

} // namespace

Look Look::engineDefaults() {
    const LightingSettings lighting;
    const PostSettings post;
    const FluidSettings fluid;
    Look look;
    look.ambient = lighting.ambient;
    look.bands = lighting.bands;
    look.dither = lighting.dither;
    look.shadows = lighting.shadows;
    look.shadowPenetration = lighting.shadowPenetration;
    look.rimIntensity = lighting.rimIntensity;
    look.rimAmbient = lighting.rimAmbient;
    look.backLight = lighting.backLight;
    look.backFlatten = lighting.backFlatten;
    look.depthFog = lighting.depthFog;
    look.depthFogColor = lighting.depthFogColor;
    look.indirect = lighting.indirect;
    look.ambientOcclusion = lighting.ambientOcclusion;
    look.ambientGradient = lighting.ambientGradient;
    look.bloomThreshold = post.bloomThreshold;
    look.bloomKnee = post.bloomKnee;
    look.bloomIntensity = post.bloomIntensity;
    look.bloomBands = post.bloomBands;
    look.exposure = post.exposure;
    look.contrast = post.contrast;
    look.saturation = post.saturation;
    look.shadowTint = post.shadowTint;
    look.highlightTint = post.highlightTint;
    look.vignette = post.vignette;
    look.smokeTint = fluid.smokeTint;
    look.smokeOpacity = fluid.opacity;
    return look;
}

std::optional<Look> parseLook(std::string_view json, const std::string& path, const TextReader& read,
                              const Look& defaults, std::string* error) {
    std::vector<std::string> chain;
    return parseLookChain(json, path, read, defaults, error, chain);
}

std::optional<Look> loadLook(const std::string& path, const Look& defaults) {
    const std::optional<std::string> text = readText(path);
    if (!text) {
        log::error("Cannot read look '{}'", path);
        return std::nullopt;
    }
    std::string error;
    std::optional<Look> look = parseLook(*text, path, readText, defaults, &error);
    if (!look) {
        log::error("Invalid look: {}", error);
    }
    return look;
}

std::vector<std::string> applyLookOverrides(Look& look, const Properties& properties, std::string_view prefix) {
    std::vector<std::string> problems;
    for (const std::string& name : properties.names()) {
        if (!name.starts_with(prefix)) {
            continue;
        }
        const std::string fieldName = name.substr(prefix.size());
        const Field* field = findField(fieldName);
        if (!field) {
            problems.push_back("unknown look property \"" + name + "\"");
            continue;
        }
        const std::optional<Converted> converted = fromProperty(field->kind, *properties.find(name));
        if (!converted) {
            problems.push_back("look property \"" + name + "\" must be " + expected(field->kind));
            continue;
        }
        store(look, *field, *converted);
    }
    return problems;
}

Look mixLooks(const Look& a, const Look& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    if (t <= 0.0f) {
        return a;
    }
    Look out = b; // whole-number and on / off fields
    for (const Field& field : kFields) {
        if (const auto* member = std::get_if<float Look::*>(&field.member)) {
            out.**member = lerp(a.**member, b.**member, t);
        } else if (const auto* vec = std::get_if<Vec3 Look::*>(&field.member)) {
            out.**vec = lerp(a.**vec, b.**vec, t);
        }
    }
    return out;
}

void applyLook(const Look& look, LightingSettings& lighting, PostSettings& post, FluidSettings& fluid) {
    lighting.ambient = look.ambient;
    lighting.bands = look.bands;
    lighting.dither = look.dither;
    lighting.shadows = look.shadows;
    lighting.shadowPenetration = look.shadowPenetration;
    lighting.rimIntensity = look.rimIntensity;
    lighting.rimAmbient = look.rimAmbient;
    lighting.backLight = look.backLight;
    lighting.backFlatten = look.backFlatten;
    lighting.depthFog = look.depthFog;
    lighting.depthFogColor = look.depthFogColor;
    lighting.indirect = look.indirect;
    lighting.ambientOcclusion = look.ambientOcclusion;
    lighting.ambientGradient = look.ambientGradient;
    post.bloomThreshold = look.bloomThreshold;
    post.bloomKnee = look.bloomKnee;
    post.bloomIntensity = look.bloomIntensity;
    post.bloomBands = look.bloomBands;
    post.exposure = look.exposure;
    post.contrast = look.contrast;
    post.saturation = look.saturation;
    post.shadowTint = look.shadowTint;
    post.highlightTint = look.highlightTint;
    post.vignette = look.vignette;
    fluid.smokeTint = look.smokeTint;
    fluid.opacity = look.smokeOpacity;
}

} // namespace animal
