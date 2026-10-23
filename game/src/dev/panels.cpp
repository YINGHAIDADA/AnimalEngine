#include "dev/dev_state.h"
#include "sandbox_game.h"

#include <animal/app/application.h>
#include <animal/core/log.h>
#include <animal/devtools/tuning_panel.h>
#include <animal/framework/effects.h>
#include <animal/framework/foliage.h>
#include <animal/framework/look.h>
#include <animal/renderer/renderer2d.h>

#include <imgui.h>

#include <filesystem>
#include <format>
#include <iterator>
#include <set>

namespace game {

namespace {

using animal::devtools::JsonDocument;

const ImVec4 kErrorColor{1.0f, 0.42f, 0.38f, 1.0f};
const ImVec4 kNoteColor{0.6f, 0.62f, 0.68f, 1.0f};

std::string stem(const std::string& path) {
    const std::u8string s = std::filesystem::path(std::u8string(path.begin(), path.end())).filename().u8string();
    std::string name(s.begin(), s.end());
    for (const char* suffix : {".look.json", ".effect.json", ".plant.json", ".tmj"}) {
        if (name.ends_with(suffix)) {
            name.resize(name.size() - std::char_traits<char>::length(suffix));
        }
    }
    return name;
}

// Save / Revert buttons and the state of an editor; returns true when "Revert" was pressed.
bool fileBar(DevTools::State& s, PresetEditor& editor) {
    ImGui::TextUnformatted(s.sources.displayName(editor.path).c_str());
    if (editor.dirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.45f, 1.0f), "(modified)");
    }
    ImGui::BeginDisabled(!editor.dirty || !editor.error.empty());
    if (ImGui::Button("Save")) {
        savePreset(s, editor);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(s.sources.available() ? "Write the source file and the deployed copy"
                                                : "Source tree not found: only the deployed copy is written");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!editor.dirty);
    const bool revert = ImGui::Button("Revert");
    ImGui::EndDisabled();
    if (!editor.error.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kErrorColor, "Not applied: %s", editor.error.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Separator();
    return revert;
}

std::string propertyText(const animal::Properties::Value& value) {
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>) {
                return v ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return v;
            } else if constexpr (std::is_same_v<T, animal::Color>) {
                return std::format("#{:02x}{:02x}{:02x}", v.r, v.g, v.b);
            } else {
                return std::format("{}", v);
            }
        },
        value);
}

// A list of preset files on the left of a JSON editor (effects, plants).
template <typename Parse, typename Apply>
void presetBrowser(DevTools::State& s, PresetEditor& editor, const std::vector<std::string>& paths,
                   const std::set<std::string>& used, const animal::devtools::JsonEditorOptions& options,
                   Parse&& parse, Apply&& apply) {
    if (paths.empty()) {
        ImGui::TextDisabled("No presets loaded yet");
        return;
    }
    ImGui::BeginChild("list", ImVec2(ImGui::GetFontSize() * 9.0f, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    for (const std::string& path : paths) {
        const std::string name = stem(path);
        const bool inRoom = used.contains(path);
        if (!inRoom) {
            ImGui::PushStyleColor(ImGuiCol_Text, kNoteColor);
        }
        if (ImGui::Selectable(name.c_str(), editor.path == path)) {
            openPreset(editor, path);
        }
        if (!inRoom) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(inRoom ? "Used in this room" : "Not used in this room");
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("editor");
    if (editor.path.empty()) {
        ImGui::TextDisabled("Select a preset");
    } else {
        if (fileBar(s, editor)) {
            openPreset(editor, editor.path);
            std::string error;
            if (auto parsed = parse(editor.doc.dump(), editor.path, &error)) {
                apply(std::move(*parsed));
            }
        }
        const animal::devtools::JsonEdit edit = animal::devtools::editJsonObject("preset", editor.doc, options);
        if (edit.changed) {
            editor.dirty = true;
        }
        if (edit.committed) {
            std::string error;
            if (auto parsed = parse(editor.doc.dump(), editor.path, &error)) {
                editor.error.clear();
                apply(std::move(*parsed));
            } else {
                editor.error = error;
            }
        }
    }
    ImGui::EndChild();
}

} // namespace

bool openPreset(PresetEditor& editor, const std::string& path) {
    editor.path = path;
    editor.dirty = false;
    editor.error.clear();
    editor.doc = JsonDocument::object();
    const std::optional<std::string> text = animal::readText(path);
    if (!text) {
        editor.error = "cannot read the file";
        return false;
    }
    try {
        editor.doc = JsonDocument::parse(*text);
    } catch (const JsonDocument::exception& e) {
        editor.error = std::string("not valid JSON: ") + e.what();
        return false;
    }
    return true;
}

bool savePreset(DevTools::State& s, PresetEditor& editor) {
    std::string error;
    if (!s.sources.write(editor.path, animal::devtools::formatJson(editor.doc), &error)) {
        animal::log::error("Save failed: {}", error);
        return false;
    }
    editor.dirty = false;
    animal::log::info("Saved {}", s.sources.displayName(editor.path));
    return true;
}

void DevPanels::performance(State& s, animal::EngineContext& context) {
    s.performance.draw(context);
    SandboxGame& g = *s.game;
    ImGui::SeparatorText("Simulation");
    ImGui::Text("grass blades %zu   vines %zu", g.cave_.bladeCount(), g.cave_.vineCount());
    const animal::EffectSystem& effects = g.cave_.effects();
    ImGui::Text("CPU particles %zu (%.2f ms)   GPU particles %u", effects.particleCount(),
                effects.lastStepMilliseconds(), context.renderer->gpuParticleCount());
    ImGui::Text("smoke %s", !context.renderer->fluid().enabled ? "off"
                            : context.renderer->fluidSleeping() ? "asleep" : "simulating");
}

void DevPanels::rendering(State& s, animal::EngineContext& context) {
    SandboxGame& g = *s.game;
    animal::Renderer2D& renderer = *context.renderer;
    ImGui::Checkbox("Lighting (F3)", &renderer.lighting().enabled);
    ImGui::Checkbox("Shadows (F7)", &g.shadowsEnabled_);
    static constexpr const char* kBands[] = {"from the look", "smooth", "3", "5", "8"};
    int bands = static_cast<int>(g.bandsOverride_);
    if (ImGui::Combo("Light bands (F4)", &bands, kBands, static_cast<int>(std::size(kBands)))) {
        g.bandsOverride_ = static_cast<size_t>(bands);
    }
    ImGui::Checkbox("Smoke (F5)", &renderer.fluid().enabled);
    ImGui::Checkbox("Bloom and grading (F6)", &renderer.post().enabled);
    bool effects = g.effectsEnabled_;
    if (ImGui::Checkbox("Effects (F10)", &effects)) {
        g.setEffectsEnabled(effects, renderer);
    }
    ImGui::Checkbox("Collision view (F2)", &g.showCollision_);
    ImGui::Separator();
    if (ImGui::Button("Reload looks (F9)")) {
        g.reloadLooks();
    }
    ImGui::SameLine();
    if (ImGui::Button("Rebuild room")) {
        g.rebuildRoom(renderer);
    }
}

void DevPanels::rooms(State& s, animal::EngineContext& context) {
    SandboxGame& g = *s.game;
    for (const animal::WorldRoom& room : g.world_.rooms) {
        const bool current = &room == g.currentRoom_;
        ImGui::PushID(room.path.c_str());
        if (ImGui::Selectable(stem(room.path).c_str(), current, ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !current) {
            g.teleport(room, *context.renderer);
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 6.0f);
        ImGui::TextDisabled("%d,%d %dx%d", room.rect.x, room.rect.y, room.rect.width, room.rect.height);
        ImGui::PopID();
    }
    ImGui::TextDisabled("Double-click a room to go there");
    if (const animal::Entity* player = g.entities_.get(g.player_)) {
        ImGui::Text("player %d,%d", player->body.position.x, player->body.position.y);
    }
}

void DevPanels::look(State& s, animal::EngineContext&) {
    SandboxGame& g = *s.game;
    if (!g.currentRoom_) {
        return;
    }
    const animal::Room* room = g.roomData(*g.currentRoom_);
    const std::string file = room ? room->properties.getString("look") : std::string();
    if (file.empty()) {
        ImGui::TextDisabled("This room uses the engine default look (no \"look\" map property).");
        return;
    }
    const std::string path = animal::resolvePath(room->path, file);
    PresetEditor& editor = s.look;
    if (editor.path != path) {
        openPreset(editor, path);
    }
    const auto apply = [&] {
        std::string error;
        const std::optional<animal::Look> look = animal::parseLook(editor.doc.dump(), path, animal::readText,
                                                                   animal::Look::engineDefaults(), &error);
        if (!look) {
            editor.error = error;
            return;
        }
        editor.error.clear();
        g.looks_[path] = *look;
        g.lookFrom_ = g.lookTo_ = g.roomLook(*g.currentRoom_);
        g.lookBlend_ = 1.0f;
    };
    if (fileBar(s, editor)) {
        openPreset(editor, path);
        apply();
    }
    std::vector<std::string> overrides;
    for (const std::string& name : room->properties.names()) {
        if (name.starts_with("look.")) {
            overrides.push_back(name);
        }
    }
    if (!overrides.empty()) {
        ImGui::TextColored(kNoteColor, "This room overrides (map properties, applied after the preset):");
        for (const std::string& name : overrides) {
            ImGui::BulletText("%s = %s", name.c_str(), propertyText(*room->properties.find(name)).c_str());
        }
        ImGui::Separator();
    }
    animal::devtools::JsonEditorOptions options;
    options.integerKeys = {"bands", "bloomBands"}; // the look format's whole-number fields
    const animal::devtools::JsonEdit edit = animal::devtools::editJsonObject("look", editor.doc, options);
    if (edit.changed) {
        editor.dirty = true;
        apply(); // looks are cheap to apply: follow every drag
    }
}

void DevPanels::effects(State& s, animal::EngineContext& context) {
    SandboxGame& g = *s.game;
    presetBrowser(
        s, s.effect, g.effects_.paths(), g.cave_.presetPaths(), {.integerKeys = {"count", "burst"}},
        [](const std::string& text, const std::string& path, std::string* error) {
            return animal::parseEffect(text, path, error);
        },
        [&](animal::EffectPreset preset) {
            g.effects_.set(s.effect.path, std::move(preset));
            g.rebuildRoom(*context.renderer);
        });
}

void DevPanels::plants(State& s, animal::EngineContext& context) {
    SandboxGame& g = *s.game;
    presetBrowser(
        s, s.plant, g.plants_.paths(), g.cave_.presetPaths(), {.integerKeys = {"heightMin", "heightMax", "spacing"}},
        [](const std::string& text, const std::string& path, std::string* error) {
            return animal::parsePlant(text, path, error);
        },
        [&](animal::PlantSpecies species) {
            g.plants_.set(s.plant.path, std::move(species));
            g.rebuildRoom(*context.renderer);
        });
}

void DevPanels::tuning(State& s, animal::EngineContext&) {
    SandboxGame& g = *s.game;
    ImGui::TextUnformatted(s.sources.displayName(g.tuningPath_).c_str());
    if (s.tuningDirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.45f, 1.0f), "(modified)");
    }
    ImGui::BeginDisabled(!s.tuningDirty);
    if (ImGui::Button("Save")) {
        std::string error;
        if (s.sources.write(g.tuningPath_, g.tuning_.toJson(), &error)) {
            s.tuningDirty = false;
            animal::log::info("Saved {}", s.sources.displayName(g.tuningPath_));
        } else {
            animal::log::error("Save failed: {}", error);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
        g.tuning_.resetToBuiltIn();
        g.loadTuning();
        s.tuningDirty = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("All built-in")) {
        g.tuning_.resetToBuiltIn();
        s.tuningDirty = true;
    }
    ImGui::TextColored(kNoteColor, "The file keeps only values that differ from the code.");
    ImGui::Separator();
    if (animal::devtools::drawTuning(g.tuning_)) {
        s.tuningDirty = true;
    }
}

} // namespace game
