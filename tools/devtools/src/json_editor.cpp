#include <animal/devtools/json_editor.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <format>
#include <unordered_map>

namespace animal::devtools {

namespace {

struct AddFieldState {
    char name[64] = {};
    char value[256] = {};
    std::string error;
};

std::unordered_map<ImGuiID, AddFieldState>& addStates() {
    static std::unordered_map<ImGuiID, AddFieldState> states;
    return states;
}

double rounded(double v) {
    return std::round(v * 10000.0) / 10000.0;
}

bool isHexColor(const std::string& s) {
    return s.size() == 7 && s[0] == '#' &&
           std::all_of(s.begin() + 1, s.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

bool containsColorName(const std::string& key) {
    std::string lower = key;
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("color") != std::string::npos || lower.find("colour") != std::string::npos;
}

bool isByteColorArray(const std::string& key, const JsonDocument& v) {
    return containsColorName(key) && v.is_array() && (v.size() == 3 || v.size() == 4) &&
           std::all_of(v.begin(), v.end(), [](const JsonDocument& e) {
               return e.is_number_integer() && e.get<int>() >= 0 && e.get<int>() <= 255;
           });
}

bool isSmallNumberArray(const JsonDocument& v) {
    return v.is_array() && !v.empty() && v.size() <= 4 &&
           std::all_of(v.begin(), v.end(), [](const JsonDocument& e) { return e.is_number(); });
}

float dragSpeed(double v) {
    return std::max(0.001f, static_cast<float>(std::fabs(v)) * 0.01f);
}

void noteEdit(JsonEdit& edit, bool changed) {
    if (changed) {
        edit.changed = true;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        edit.committed = true;
    }
}

// A number written back: integers stay integers while whole (no churn in hand-written files).
JsonDocument numberValue(double v, bool wasInteger) {
    const double r = rounded(v);
    if (wasInteger && r == std::floor(r) && std::fabs(r) < 2.0e9) {
        return static_cast<int>(r);
    }
    return r;
}

void editValue(const std::string& label, JsonDocument& value, JsonEdit& edit, bool locked,
               const JsonEditorOptions& options);

void editChildren(JsonDocument& container, JsonEdit& edit, const JsonEditorOptions& options) {
    if (container.is_object()) {
        for (auto& [key, child] : container.items()) {
            editValue(key, child, edit, false, options);
        }
    } else {
        for (size_t i = 0; i < container.size(); ++i) {
            editValue(std::format("[{}]", i), container[i], edit, false, options);
        }
    }
}

void editValue(const std::string& label, JsonDocument& value, JsonEdit& edit, bool locked,
               const JsonEditorOptions& options) {
    const bool integerKey = std::ranges::find(options.integerKeys, label) != options.integerKeys.end();
    ImGui::PushID(label.c_str());
    if (locked) {
        ImGui::BeginDisabled();
    }
    const char* name = label.c_str();
    if (value.is_boolean()) {
        bool b = value.get<bool>();
        if (ImGui::Checkbox(name, &b)) {
            value = b;
            edit.changed = edit.committed = true;
        }
    } else if (value.is_number_integer() && integerKey) {
        int i = value.get<int>();
        const bool changed = ImGui::DragInt(name, &i, std::max(0.2f, dragSpeed(i)));
        if (changed) {
            value = i;
        }
        noteEdit(edit, changed);
    } else if (value.is_number()) {
        const bool wasInteger = value.is_number_integer();
        auto f = static_cast<float>(value.get<double>());
        const bool changed = ImGui::DragFloat(name, &f, dragSpeed(f), 0.0f, 0.0f, "%.4g");
        if (changed) {
            value = numberValue(f, wasInteger);
        }
        noteEdit(edit, changed);
    } else if (value.is_string()) {
        std::string s = value.get<std::string>();
        if (isHexColor(s)) {
            unsigned r = 0, g = 0, b = 0;
            std::sscanf(s.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
            float rgb[3] = {r / 255.0f, g / 255.0f, b / 255.0f};
            const bool changed = ImGui::ColorEdit3(name, rgb);
            if (changed) {
                value = std::format("#{:02x}{:02x}{:02x}", static_cast<int>(std::lround(rgb[0] * 255.0f)),
                                    static_cast<int>(std::lround(rgb[1] * 255.0f)),
                                    static_cast<int>(std::lround(rgb[2] * 255.0f)));
            }
            noteEdit(edit, changed);
        } else {
            char buffer[512];
            std::snprintf(buffer, sizeof(buffer), "%s", s.c_str());
            const bool changed = ImGui::InputText(name, buffer, sizeof(buffer));
            if (changed) {
                value = std::string(buffer);
            }
            noteEdit(edit, changed);
        }
    } else if (isByteColorArray(label, value)) {
        float rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        for (size_t i = 0; i < value.size(); ++i) {
            rgba[i] = value[i].get<int>() / 255.0f;
        }
        const bool changed = value.size() == 4 ? ImGui::ColorEdit4(name, rgba) : ImGui::ColorEdit3(name, rgba);
        if (changed) {
            for (size_t i = 0; i < value.size(); ++i) {
                value[i] = static_cast<int>(std::lround(rgba[i] * 255.0f));
            }
        }
        noteEdit(edit, changed);
    } else if (isSmallNumberArray(value)) {
        const bool integers =
            std::all_of(value.begin(), value.end(), [](const JsonDocument& e) { return e.is_number_integer(); });
        float v[4] = {};
        double magnitude = 0.0;
        for (size_t i = 0; i < value.size(); ++i) {
            v[i] = value[i].get<float>();
            magnitude = std::max(magnitude, std::fabs(static_cast<double>(v[i])));
        }
        const int n = static_cast<int>(value.size());
        const float speed = integers ? std::max(0.2f, dragSpeed(magnitude)) : dragSpeed(magnitude);
        const bool changed = ImGui::DragScalarN(name, ImGuiDataType_Float, v, n, speed, nullptr, nullptr, "%.4g");
        if (changed) {
            for (int i = 0; i < n; ++i) {
                const size_t index = static_cast<size_t>(i);
                value[index] = numberValue(v[i], value[index].is_number_integer());
            }
        }
        noteEdit(edit, changed);
    } else if (value.is_object() || value.is_array()) {
        if (ImGui::TreeNode(name)) {
            editChildren(value, edit, options);
            ImGui::TreePop();
        }
    } else {
        ImGui::TextDisabled("%s: null", name);
    }
    if (locked) {
        ImGui::EndDisabled();
    }
    ImGui::PopID();
}

void formatInto(const JsonDocument& v, int indent, int depth, std::string& out) {
    const auto pad = [&](int d) { out.append(static_cast<size_t>(indent * d), ' '); };
    switch (v.type()) {
    case JsonDocument::value_t::object: {
        if (v.empty()) {
            out += "{}";
            return;
        }
        out += "{\n";
        size_t i = 0;
        for (const auto& [key, child] : v.items()) {
            pad(depth + 1);
            out += JsonDocument(key).dump();
            out += ": ";
            formatInto(child, indent, depth + 1, out);
            out += ++i < v.size() ? ",\n" : "\n";
        }
        pad(depth);
        out += "}";
        return;
    }
    case JsonDocument::value_t::array: {
        if (v.empty()) {
            out += "[]";
            return;
        }
        const bool inline_ = std::all_of(v.begin(), v.end(), [](const JsonDocument& e) { return e.is_primitive(); });
        if (inline_) {
            out += "[";
            for (size_t i = 0; i < v.size(); ++i) {
                formatInto(v[i], indent, depth + 1, out);
                if (i + 1 < v.size()) {
                    out += ", ";
                }
            }
            out += "]";
            return;
        }
        out += "[\n";
        for (size_t i = 0; i < v.size(); ++i) {
            pad(depth + 1);
            formatInto(v[i], indent, depth + 1, out);
            out += i + 1 < v.size() ? ",\n" : "\n";
        }
        pad(depth);
        out += "]";
        return;
    }
    case JsonDocument::value_t::number_float:
        out += JsonDocument(rounded(v.get<double>())).dump(); // whole values print as "2.0"
        return;
    default:
        out += v.dump();
        return;
    }
}

} // namespace

JsonEdit editJsonObject(const char* id, JsonDocument& object, const JsonEditorOptions& options) {
    JsonEdit edit;
    if (!object.is_object()) {
        ImGui::TextDisabled("(not a JSON object)");
        return edit;
    }
    ImGui::PushID(id);
    ImGui::PushItemWidth(-ImGui::GetFontSize() * 11.0f);
    std::string removeKey;
    for (auto& [key, value] : object.items()) {
        const bool locked = std::ranges::find(options.lockedKeys, key) != options.lockedKeys.end();
        if (options.allowAddRemove && !locked) {
            ImGui::PushID(key.c_str());
            if (ImGui::SmallButton("x")) {
                removeKey = key;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Remove \"%s\"", key.c_str());
            }
            ImGui::PopID();
            ImGui::SameLine();
        }
        editValue(key, value, edit, locked, options);
    }
    ImGui::PopItemWidth();
    if (!removeKey.empty()) {
        object.erase(removeKey);
        edit.changed = edit.committed = true;
    }

    if (options.allowAddRemove) {
        AddFieldState& add = addStates()[ImGui::GetID("add")];
        ImGui::Separator();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
        ImGui::InputTextWithHint("##name", "field", add.name, sizeof(add.name));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
        ImGui::InputTextWithHint("##value", "JSON value", add.value, sizeof(add.value));
        ImGui::SameLine();
        if (ImGui::Button("Add") && add.name[0] != '\0') {
            try {
                object[add.name] = JsonDocument::parse(add.value);
                add.name[0] = add.value[0] = '\0';
                add.error.clear();
                edit.changed = edit.committed = true;
            } catch (const JsonDocument::exception&) {
                add.error = "not a JSON value (strings need quotes)";
            }
        }
        if (!add.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.38f, 1.0f), "%s", add.error.c_str());
        }
    }
    ImGui::PopID();
    return edit;
}

std::string formatJson(const JsonDocument& value, int indent) {
    std::string out;
    formatInto(value, indent, 0, out);
    out += "\n";
    return out;
}

} // namespace animal::devtools
