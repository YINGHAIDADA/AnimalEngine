#include <animal/devtools/tuning_panel.h>

#include <animal/framework/tuning.h>

#include <imgui.h>

#include <string>

namespace animal::devtools {

bool drawTuning(Tuning& tuning) {
    static char filter[64] = {};
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "filter", filter, sizeof(filter));

    bool changed = false;
    std::string group;
    bool groupOpen = false;
    for (const Tuning::Entry& entry : tuning.entries()) {
        if (filter[0] != '\0' && entry.name.find(filter) == std::string::npos) {
            continue;
        }
        const size_t dot = entry.name.find('.');
        const std::string entryGroup = dot == std::string::npos ? std::string("misc") : entry.name.substr(0, dot);
        if (entryGroup != group) {
            group = entryGroup;
            groupOpen = ImGui::CollapsingHeader(group.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        }
        if (!groupOpen) {
            continue;
        }
        const std::string label = dot == std::string::npos ? entry.name : entry.name.substr(dot + 1);
        const bool modified = !Tuning::atBuiltIn(entry);
        ImGui::PushID(entry.name.c_str());
        ImGui::BeginDisabled(!modified);
        if (ImGui::SmallButton("R")) {
            std::visit([&entry](auto* p) { *p = std::get<std::remove_reference_t<decltype(*p)>>(entry.builtIn); },
                       entry.target);
            changed = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Reset to the built-in value");
        }
        ImGui::SameLine();
        if (modified) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.45f, 1.0f));
        }
        ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 12.0f);
        if (float* const* f = std::get_if<float*>(&entry.target)) {
            changed |= ImGui::SliderFloat(label.c_str(), *f, entry.min, entry.max, "%.3g");
        } else if (int* const* i = std::get_if<int*>(&entry.target)) {
            changed |= ImGui::SliderInt(label.c_str(), *i, static_cast<int>(entry.min), static_cast<int>(entry.max));
        } else if (bool* const* b = std::get_if<bool*>(&entry.target)) {
            changed |= ImGui::Checkbox(label.c_str(), *b);
        }
        if (modified) {
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }
    return changed;
}

} // namespace animal::devtools
