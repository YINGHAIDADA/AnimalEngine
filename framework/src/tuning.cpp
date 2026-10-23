#include <animal/framework/tuning.h>

#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace animal {

namespace {

using Json = nlohmann::ordered_json;
constexpr const char* kFormat = "animal-tuning";
constexpr int kVersion = 1;

// Whole numbers print without a fraction, others with at most 4 decimals.
std::string formatFloat(float v) {
    const double rounded = std::round(static_cast<double>(v) * 10000.0) / 10000.0;
    if (rounded == std::floor(rounded) && std::fabs(rounded) < 1.0e9) {
        return std::format("{}", static_cast<long long>(rounded));
    }
    std::string text = std::format("{:.4f}", rounded);
    while (text.back() == '0') {
        text.pop_back();
    }
    return text;
}

} // namespace

void Tuning::add(std::string name, float* value, float min, float max) {
    std::erase_if(entries_, [&](const Entry& e) { return e.name == name; });
    entries_.push_back({std::move(name), value, min, max, *value});
}

void Tuning::add(std::string name, int* value, int min, int max) {
    std::erase_if(entries_, [&](const Entry& e) { return e.name == name; });
    entries_.push_back({std::move(name), value, static_cast<float>(min), static_cast<float>(max), *value});
}

void Tuning::add(std::string name, bool* value) {
    std::erase_if(entries_, [&](const Entry& e) { return e.name == name; });
    entries_.push_back({std::move(name), value, 0.0f, 1.0f, *value});
}

const Tuning::Entry* Tuning::find(std::string_view name) const {
    const auto it = std::ranges::find_if(entries_, [name](const Entry& e) { return e.name == name; });
    return it == entries_.end() ? nullptr : &*it;
}

Tuning::Value Tuning::current(const Entry& entry) {
    return std::visit([](auto* p) -> Value { return *p; }, entry.target);
}

bool Tuning::atBuiltIn(const Entry& entry) {
    return current(entry) == entry.builtIn;
}

void Tuning::resetToBuiltIn() {
    for (const Entry& entry : entries_) {
        std::visit([&entry](auto* p) { *p = std::get<std::remove_reference_t<decltype(*p)>>(entry.builtIn); },
                   entry.target);
    }
}

Tuning::ApplyResult Tuning::apply(std::string_view text, const std::string& path, bool dryRun) {
    Json json;
    try {
        json = Json::parse(text);
    } catch (const Json::exception& e) {
        return {false, {path + ": not valid JSON (" + e.what() + ")"}};
    }
    if (!json.is_object() || !json.contains("format") || json["format"] != kFormat) {
        return {false, {path + ": not a tuning file (\"format\" must be \"" + kFormat + "\")"}};
    }
    if (!json.contains("version") || !json["version"].is_number_integer() || json["version"].get<int>() != kVersion) {
        return {false, {path + ": unsupported version (expected " + std::to_string(kVersion) + ")"}};
    }
    if (json.contains("values") && !json["values"].is_object()) {
        return {false, {path + ": \"values\" must be an object"}};
    }
    ApplyResult result{true, {}};
    std::vector<std::string>& problems = result.problems;
    for (const auto& [key, value] : json.items()) {
        if (key != "format" && key != "version" && key != "values") {
            problems.push_back(path + ": unknown field \"" + key + "\"");
        }
    }
    if (!json.contains("values")) {
        return result;
    }
    for (const auto& [name, value] : json["values"].items()) {
        const auto it = std::ranges::find_if(entries_, [&](const Entry& e) { return e.name == name; });
        if (it == entries_.end()) {
            problems.push_back(path + ": unknown parameter \"" + name + "\"");
            continue;
        }
        const bool ok = std::visit(
            [&value, dryRun](auto* p) {
                using T = std::remove_reference_t<decltype(*p)>;
                bool typed = false;
                if constexpr (std::is_same_v<T, bool>) {
                    typed = value.is_boolean();
                } else if constexpr (std::is_same_v<T, int>) {
                    typed = value.is_number_integer();
                } else {
                    typed = value.is_number();
                }
                if (typed && !dryRun) {
                    *p = value.template get<T>();
                }
                return typed;
            },
            it->target);
        if (!ok) {
            problems.push_back(path + ": \"" + name + "\" has the wrong type");
        }
    }
    return result;
}

std::string Tuning::toJson() const {
    std::string out = std::format("{{\n  \"format\": \"{}\",\n  \"version\": {},\n  \"values\": {{", kFormat, kVersion);
    bool first = true;
    for (const Entry& entry : entries_) {
        if (atBuiltIn(entry)) {
            continue;
        }
        const Value v = current(entry);
        std::string value;
        if (const float* f = std::get_if<float>(&v)) {
            value = formatFloat(*f);
        } else if (const int* i = std::get_if<int>(&v)) {
            value = std::to_string(*i);
        } else {
            value = std::get<bool>(v) ? "true" : "false";
        }
        out += std::format("{}\n    \"{}\": {}", first ? "" : ",", entry.name, value);
        first = false;
    }
    out += first ? "}\n}\n" : "\n  }\n}\n";
    return out;
}

} // namespace animal
