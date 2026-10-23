// Level editor (ADR-0029): edits the current room's Tiled document in place, applies every change to
// the running game through the same room parser the game loads with, and saves the .tmj (source and
// deployed copy). Tab toggles it; while it is on the room is frozen for the player.

#include "dev/dev_state.h"
#include "sandbox_game.h"

#include <animal/app/application.h>
#include <animal/core/log.h>
#include <animal/framework/level.h>
#include <animal/platform/input.h>
#include <animal/platform/window.h>
#include <animal/renderer/renderer2d.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>

namespace game {

namespace {

using animal::Vec2;
using animal::devtools::JsonDocument;
using animal::devtools::TiledMap;

constexpr size_t kUndoLimit = 200;

struct PropertyTemplate {
    const char* name;
    const char* type; // Tiled property type: bool, int, float, string, color, file
    const char* value; // JSON
};

struct ObjectTemplate {
    const char* type;
    bool point;
    int width;
    int height;
    ImU32 color;
    const char* help;
    std::vector<PropertyTemplate> initial; // set when placed
    std::vector<PropertyTemplate> known;   // offered by "Add property"
};

const std::vector<PropertyTemplate> kLightProperties = {
    {"color", "color", "\"#ffffd9a0\""}, {"radius", "float", "60.0"},   {"intensity", "float", "1.0"},
    {"height", "float", "12.0"},         {"shadows", "bool", "true"},   {"cone", "float", "40.0"},
    {"direction", "float", "90.0"},      {"animation", "string", "\"flicker\""}, {"speed", "float", "1.0"},
    {"amount", "float", "0.2"},
};

const std::vector<ObjectTemplate>& objectTemplates() {
    static const std::vector<ObjectTemplate> templates = {
        {"light", true, 0, 0, IM_COL32(255, 220, 120, 255), "Point light (centre)",
         {{"color", "color", "\"#ffffd9a0\""}, {"radius", "float", "60.0"}, {"intensity", "float", "1.0"}},
         kLightProperties},
        {"lantern", true, 0, 0, IM_COL32(255, 170, 80, 255), "Lantern on a chain (centre of its cell)", {},
         kLightProperties},
        {"candle", true, 0, 0, IM_COL32(255, 200, 90, 255), "Candle prop with embers (bottom centre)", {},
         {{"effect", "file", "\"../effects/ember.effect.json\""}}},
        {"trigger", false, 32, 24, IM_COL32(120, 200, 255, 255), "Shows a message when the player enters",
         {{"message", "string", "\"HELLO\""}, {"once", "bool", "true"}},
         {{"message", "string", "\"HELLO\""}, {"once", "bool", "true"}}},
        {"crawler", true, 0, 0, IM_COL32(255, 90, 160, 255), "Patrolling creature (feet)", {}, {}},
        {"gem", true, 0, 0, IM_COL32(90, 255, 230, 255), "Collectible (centre; the object id is its save key)", {},
         {}},
        {"vine", true, 0, 0, IM_COL32(120, 220, 90, 255), "Climbable vine (anchor; hangs to the floor)", {},
         {{"segments", "int", "8"}}},
        {"spawn", true, 0, 0, IM_COL32(255, 255, 255, 255), "Where a new game starts (feet)", {}, {}},
        {"water", false, 48, 16, IM_COL32(80, 160, 255, 255), "Pool; the top edge is the surface",
         {},
         {{"color", "color", "\"#ff285a78\""}, {"opacity", "float", "0.45"}, {"reflection", "float", "0.35"},
          {"refraction", "int", "1"}, {"effect", "file", "\"../effects/splash.effect.json\""}}},
        {"emitter", false, 64, 48, IM_COL32(230, 140, 255, 255), "Particles over an area (or a point)",
         {{"effect", "file", "\"../effects/dust.effect.json\""}},
         {{"effect", "file", "\"../effects/dust.effect.json\""}, {"count", "int", "20"}, {"rate", "float", "4.0"},
          {"floor", "float", "160.0"}}},
        {"plants", false, 64, 32, IM_COL32(150, 230, 110, 255), "Plants on exposed tile faces in the area",
         {{"plant", "file", "\"../plants/grass.plant.json\""}},
         {{"plant", "file", "\"../plants/grass.plant.json\""}}},
        {"vent", true, 0, 0, IM_COL32(200, 200, 200, 255), "Smoke vent with an updraft (bottom centre)", {},
         {{"rate", "float", "6.0"}, {"force", "float", "420.0"}}},
        {"wind", true, 0, 0, IM_COL32(180, 230, 255, 255), "Draft blowing along a direction (degrees)",
         {{"direction", "float", "270.0"}},
         {{"direction", "float", "270.0"}, {"strength", "float", "2.0"}, {"length", "float", "64.0"},
          {"radius", "float", "10.0"}}},
    };
    return templates;
}

const ObjectTemplate* templateOf(const std::string& type) {
    for (const ObjectTemplate& t : objectTemplates()) {
        if (type == t.type) {
            return &t;
        }
    }
    return nullptr;
}

const std::vector<PropertyTemplate>& mapProperties() {
    static const std::vector<PropertyTemplate> properties = {
        {"look", "file", "\"../looks/cave.look.json\""},
        {"wind.strength", "float", "1.0"},
        {"wind.gusts", "float", "1.0"},
        {"wind.gustInterval", "float", "8.0"},
        {"wind.gustSpeed", "float", "-80.0"},
        {"look.exposure", "float", "1.0"},
        {"look.contrast", "float", "1.0"},
        {"look.saturation", "float", "1.0"},
        {"look.bands", "int", "5"},
        {"look.vignette", "float", "0.2"},
        {"look.ambient", "color", "\"#ff12121c\""},
        {"look.bloomIntensity", "float", "1.0"},
    };
    return properties;
}

// Window points <-> world pixels through the present layout and the camera.
struct Mapping {
    float scale = 1.0f;   // points per world pixel
    ImVec2 origin;        // points of world (0, 0)

    ImVec2 toScreen(Vec2 world) const { return {origin.x + world.x * scale, origin.y + world.y * scale}; }
    Vec2 toWorld(ImVec2 points) const { return {(points.x - origin.x) / scale, (points.y - origin.y) / scale}; }
};

Mapping mappingFor(const animal::EngineContext& context, Vec2 cameraTopLeft) {
    const animal::PresentLayout& layout = context.renderer->presentLayout();
    const animal::platform::PixelSize pixels = context.window->pixelSize();
    const animal::platform::PixelSize points = context.window->sizeInPoints();
    const float density =
        points.width > 0 && pixels.width > 0 ? static_cast<float>(pixels.width) / static_cast<float>(points.width) : 1.0f;
    Mapping m;
    m.scale = static_cast<float>(layout.scale) / density;
    m.origin = {(static_cast<float>(layout.x) - cameraTopLeft.x * static_cast<float>(layout.scale)) / density,
                (static_cast<float>(layout.y) - cameraTopLeft.y * static_cast<float>(layout.scale)) / density};
    return m;
}

float snapped(float v, int snap) {
    return snap <= 1 ? std::round(v) : std::round(v / static_cast<float>(snap)) * static_cast<float>(snap);
}

JsonDocument number(float v) {
    if (v == std::floor(v) && std::fabs(v) < 1.0e7f) {
        return static_cast<int>(v);
    }
    return std::round(static_cast<double>(v) * 1000.0) / 1000.0;
}

float numberOf(const JsonDocument& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number() ? it->get<float>() : 0.0f;
}

std::string typeOf(const JsonDocument& object) {
    // Tiled 1.9+ writes "type" in .tmj files for an object's class ("class" in older files).
    if (const auto it = object.find("type"); it != object.end() && it->is_string()) {
        return it->get<std::string>();
    }
    return object.value("class", "");
}

bool isPoint(const JsonDocument& object) {
    return object.value("point", false) || (numberOf(object, "width") <= 0.0f && numberOf(object, "height") <= 0.0f);
}

// Pick box in world pixels: rectangles as they are, points as the 8×8 prop around / above them.
animal::RectF pickBox(const JsonDocument& object) {
    const float x = numberOf(object, "x");
    const float y = numberOf(object, "y");
    if (isPoint(object)) {
        return {x - 4.0f, y - 8.0f, 8.0f, 12.0f};
    }
    return {x, y, numberOf(object, "width"), numberOf(object, "height")};
}

bool contains(const animal::RectF& r, Vec2 p) {
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
}

ImU32 colorOf(const std::string& type) {
    const ObjectTemplate* t = templateOf(type);
    return t ? t->color : IM_COL32(220, 220, 220, 255);
}

// "../effects/x.effect.json" candidates for a file property, relative to the room's directory.
std::vector<std::string> fileChoices(const std::string& dataDirectory, const std::string& property) {
    std::string folder;
    std::string suffix;
    if (property == "effect") {
        folder = "effects";
        suffix = ".effect.json";
    } else if (property == "plant") {
        folder = "plants";
        suffix = ".plant.json";
    } else if (property == "look") {
        folder = "looks";
        suffix = ".look.json";
    } else {
        return {};
    }
    std::vector<std::string> choices;
    std::error_code ec;
    const std::string dir = dataDirectory + "world/" + folder;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(std::u8string(dir.begin(), dir.end())), ec)) {
        const std::u8string u8 = entry.path().filename().u8string();
        const std::string name(u8.begin(), u8.end());
        if (name.ends_with(suffix)) {
            choices.push_back("../" + folder + "/" + name);
        }
    }
    std::ranges::sort(choices);
    return choices;
}

// The history of a room: snapshot before a change; a change that turns out to do nothing is dropped
// when the next snapshot comes.
void snapshot(EditedRoom& room) {
    room.undo.push_back(room.map.document());
    if (room.undo.size() > kUndoLimit) {
        room.undo.erase(room.undo.begin());
    }
    room.redo.clear();
}

// Edits the properties array of `owner` (object or map). Returns {changed, committed}.
struct PropertyEdit {
    bool changed = false;
    bool committed = false;
    bool activated = false; // an edit is starting: take a snapshot first
};

PropertyEdit editProperties(JsonDocument& owner, const std::vector<PropertyTemplate>& known,
                            const std::string& dataDirectory) {
    PropertyEdit edit;
    const auto note = [&edit](bool changed) {
        if (ImGui::IsItemActivated()) {
            edit.activated = true;
        }
        if (changed) {
            edit.changed = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            edit.committed = true;
        }
    };
    std::string remove;
    if (owner.contains("properties") && owner["properties"].is_array()) {
        ImGui::PushItemWidth(-ImGui::GetFontSize() * 9.0f);
        for (JsonDocument& p : owner["properties"]) {
            const std::string name = p.value("name", "");
            const std::string type = p.value("type", "string");
            JsonDocument& value = p["value"];
            ImGui::PushID(name.c_str());
            if (ImGui::SmallButton("x")) {
                remove = name;
            }
            ImGui::SameLine();
            const std::string label = name + " (" + type + ")";
            if (type == "bool") {
                bool b = value.is_boolean() && value.get<bool>();
                if (ImGui::Checkbox(label.c_str(), &b)) {
                    value = b;
                    edit.activated = edit.changed = edit.committed = true;
                }
            } else if (type == "int") {
                int i = value.is_number() ? value.get<int>() : 0;
                const bool changed = ImGui::DragInt(label.c_str(), &i, 0.2f);
                if (changed) {
                    value = i;
                }
                note(changed);
            } else if (type == "float") {
                auto f = value.is_number() ? value.get<float>() : 0.0f;
                const bool changed = ImGui::DragFloat(label.c_str(), &f, std::max(0.01f, std::fabs(f) * 0.01f), 0.0f, 0.0f, "%.3g");
                if (changed) {
                    value = std::round(static_cast<double>(f) * 1000.0) / 1000.0;
                }
                note(changed);
            } else if (type == "color") {
                const std::optional<animal::Color> c = animal::parseColor(value.is_string() ? value.get<std::string>() : "");
                float rgba[4] = {c ? c->r / 255.0f : 1.0f, c ? c->g / 255.0f : 1.0f, c ? c->b / 255.0f : 1.0f,
                                 c ? c->a / 255.0f : 1.0f};
                const bool changed = ImGui::ColorEdit4(label.c_str(), rgba, ImGuiColorEditFlags_AlphaBar);
                if (changed) {
                    const auto byte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
                    value = std::format("#{:02x}{:02x}{:02x}{:02x}", byte(rgba[3]), byte(rgba[0]), byte(rgba[1]), byte(rgba[2]));
                }
                note(changed);
            } else {
                const std::vector<std::string> choices = type == "file" ? fileChoices(dataDirectory, name)
                                                                         : std::vector<std::string>();
                std::string text = value.is_string() ? value.get<std::string>() : value.dump();
                if (!choices.empty()) {
                    if (ImGui::BeginCombo(label.c_str(), text.c_str())) {
                        for (const std::string& choice : choices) {
                            if (ImGui::Selectable(choice.c_str(), choice == text)) {
                                value = choice;
                                edit.activated = edit.changed = edit.committed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                } else {
                    char buffer[512];
                    std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
                    const bool changed = ImGui::InputText(label.c_str(), buffer, sizeof(buffer));
                    if (changed) {
                        value = std::string(buffer);
                    }
                    note(changed);
                }
            }
            ImGui::PopID();
        }
        ImGui::PopItemWidth();
    }
    if (!remove.empty()) {
        edit.activated = true;
        TiledMap::removeProperty(owner, remove);
        edit.changed = edit.committed = true;
    }
    // Add a known property (with its default) or any other by name and type.
    std::vector<const PropertyTemplate*> missing;
    for (const PropertyTemplate& p : known) {
        if (!TiledMap::property(owner, p.name)) {
            missing.push_back(&p);
        }
    }
    if (!missing.empty()) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
        if (ImGui::BeginCombo("##add", "Add property...")) {
            for (const PropertyTemplate* p : missing) {
                if (ImGui::Selectable(std::format("{} ({})", p->name, p->type).c_str())) {
                    edit.activated = true;
                    TiledMap::setProperty(owner, p->name, p->type, JsonDocument::parse(p->value));
                    edit.changed = edit.committed = true;
                }
            }
            ImGui::EndCombo();
        }
    }
    static char customName[64] = {};
    static int customType = 0;
    static constexpr const char* kTypes[] = {"string", "float", "int", "bool", "color", "file"};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    ImGui::InputTextWithHint("##custom", "other name", customName, sizeof(customName));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
    ImGui::Combo("##type", &customType, kTypes, IM_ARRAYSIZE(kTypes));
    ImGui::SameLine();
    if (ImGui::Button("Add") && customName[0] != '\0' && !TiledMap::property(owner, customName)) {
        static const char* kDefaults[] = {"\"\"", "0.0", "0", "false", "\"#ffffffff\"", "\"\""};
        edit.activated = true;
        TiledMap::setProperty(owner, customName, kTypes[customType], JsonDocument::parse(kDefaults[customType]));
        customName[0] = '\0';
        edit.changed = edit.committed = true;
    }
    return edit;
}

void drawOverlay(const LevelEditor& editor, const EditedRoom& room, const Mapping& m, Vec2 mouse, bool mouseInCanvas) {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const int width = room.map.width();
    const int height = room.map.height();
    const float tile = static_cast<float>(room.map.tileWidth());
    if (editor.showGrid) {
        const ImU32 grid = IM_COL32(255, 255, 255, 28);
        for (int x = 0; x <= width; ++x) {
            draw->AddLine(m.toScreen({x * tile, 0.0f}), m.toScreen({x * tile, height * tile}), grid);
        }
        for (int y = 0; y <= height; ++y) {
            draw->AddLine(m.toScreen({0.0f, y * tile}), m.toScreen({width * tile, y * tile}), grid);
        }
    }
    draw->AddRect(m.toScreen({0.0f, 0.0f}), m.toScreen({width * tile, height * tile}), IM_COL32(255, 255, 255, 90));

    for (const int id : room.map.objectIds()) {
        const JsonDocument& object = *room.map.object(id);
        const std::string type = typeOf(object);
        const bool selected = id == editor.selected;
        const ImU32 color = selected ? IM_COL32(255, 255, 80, 255) : (colorOf(type) & 0x00FFFFFF) | 0xC0000000;
        const float x = numberOf(object, "x");
        const float y = numberOf(object, "y");
        if (isPoint(object)) {
            const ImVec2 c = m.toScreen({x, y});
            const float r = std::max(3.0f, m.scale * 1.5f);
            draw->AddCircle(c, r, color, 12, selected ? 2.5f : 1.5f);
            draw->AddLine({c.x - r * 1.8f, c.y}, {c.x + r * 1.8f, c.y}, color);
            draw->AddLine({c.x, c.y - r * 1.8f}, {c.x, c.y + r * 1.8f}, color);
        } else {
            const ImVec2 a = m.toScreen({x, y});
            const ImVec2 b = m.toScreen({x + numberOf(object, "width"), y + numberOf(object, "height")});
            draw->AddRectFilled(a, b, (colorOf(type) & 0x00FFFFFF) | 0x18000000);
            draw->AddRect(a, b, color, 0.0f, selected ? 2.5f : 1.0f);
            if (selected) {
                draw->AddRectFilled({b.x - 5.0f, b.y - 5.0f}, {b.x + 5.0f, b.y + 5.0f}, IM_COL32(255, 255, 80, 255));
            }
        }
        if (editor.showLabels || selected) {
            const animal::RectF box = pickBox(object);
            const ImVec2 at = m.toScreen({box.x, box.y});
            const std::string label = std::format("{} {}", type, id);
            draw->AddText({at.x + 1.0f, at.y - ImGui::GetFontSize()}, IM_COL32(0, 0, 0, 200), label.c_str());
            draw->AddText({at.x, at.y - ImGui::GetFontSize() - 1.0f}, color, label.c_str());
        }
    }

    if (!mouseInCanvas) {
        return;
    }
    if (editor.tool == LevelEditor::Tool::Brush || editor.tool == LevelEditor::Tool::Rect) {
        const float tx = std::floor(mouse.x / tile) * tile;
        const float ty = std::floor(mouse.y / tile) * tile;
        draw->AddRect(m.toScreen({tx, ty}), m.toScreen({tx + tile, ty + tile}), IM_COL32(255, 255, 255, 200));
        if (editor.drag == LevelEditor::Drag::Rect) {
            const float x0 = std::floor(std::min(editor.dragStart.x, mouse.x) / tile) * tile;
            const float y0 = std::floor(std::min(editor.dragStart.y, mouse.y) / tile) * tile;
            const float x1 = (std::floor(std::max(editor.dragStart.x, mouse.x) / tile) + 1.0f) * tile;
            const float y1 = (std::floor(std::max(editor.dragStart.y, mouse.y) / tile) + 1.0f) * tile;
            draw->AddRect(m.toScreen({x0, y0}), m.toScreen({x1, y1}),
                          editor.dragErase ? IM_COL32(255, 90, 90, 255) : IM_COL32(120, 255, 160, 255), 0.0f, 2.0f);
        }
    } else if (editor.tool == LevelEditor::Tool::Place) {
        const ObjectTemplate& t = objectTemplates()[editor.placeType];
        const Vec2 p{snapped(mouse.x, editor.snap), snapped(mouse.y, editor.snap)};
        if (t.point) {
            draw->AddCircle(m.toScreen(p), 4.0f, t.color, 12, 1.5f);
        } else {
            draw->AddRect(m.toScreen(p), m.toScreen({p.x + t.width, p.y + t.height}), t.color);
        }
    }
}

} // namespace

EditedRoom* DevPanels::editorRoom(State& s) {
    SandboxGame& g = *s.game;
    if (!g.currentRoom_) {
        return nullptr;
    }
    const std::string& path = g.currentRoom_->path;
    if (const auto it = s.editor.rooms.find(path); it != s.editor.rooms.end()) {
        return &it->second;
    }
    const std::optional<std::string> text = animal::readText(path);
    std::string error;
    std::optional<TiledMap> map = text ? TiledMap::parse(*text, &error) : std::nullopt;
    if (!map) {
        animal::log::error("Level editor: cannot open {}: {}", path, text ? error : "unreadable");
        return nullptr;
    }
    return &s.editor.rooms.emplace(path, EditedRoom{std::move(*map), {}, {}, false, {}}).first->second;
}

bool DevPanels::editorCommit(State& s, animal::EngineContext& context, EditedRoom& room) {
    SandboxGame& g = *s.game;
    const std::string& path = g.currentRoom_->path;
    room.dirty = true;
    std::string error;
    std::optional<animal::Room> parsed = animal::parseRoom(room.map.serialize(), path, animal::readText, &error);
    if (parsed && (parsed->tiles.pixelBounds().width != g.currentRoom_->rect.width ||
                   parsed->tiles.pixelBounds().height != g.currentRoom_->rect.height)) {
        error = "the map size no longer matches the world file";
        parsed.reset();
    }
    if (!parsed) {
        room.error = error;
        return false;
    }
    room.error.clear();
    g.rooms_[path] = std::move(*parsed);
    g.collectGemFlags(*g.currentRoom_, g.rooms_[path]);
    return g.rebuildRoom(*context.renderer);
}

bool DevPanels::editorSave(State& s, const std::string& path, EditedRoom& room) {
    std::string error;
    if (!s.sources.write(path, room.map.serialize(), &error)) {
        animal::log::error("Level editor: save failed: {}", error);
        return false;
    }
    acknowledgeWrite(s, path);
    room.dirty = false;
    animal::log::info("Saved {}", s.sources.displayName(path));
    return true;
}

void DevPanels::setEditing(State& s, animal::EngineContext& context, bool active) {
    LevelEditor& e = s.editor;
    if (active == e.active) {
        return;
    }
    e.active = active && editorRoom(s) != nullptr;
    e.drag = LevelEditor::Drag::None;
    if (e.active) {
        s.visible = true;
        animal::log::info("Level editor on: the room is paused for the player (Tab to play)");
    } else {
        s.game->playerState_.velocity = {};
    }
    (void)context;
}

void DevPanels::editorFrame(State& s, animal::EngineContext& context) {
    SandboxGame& g = *s.game;
    LevelEditor& e = s.editor;
    ImGuiIO& io = ImGui::GetIO();
    if (!e.active) {
        return;
    }
    EditedRoom* room = editorRoom(s);
    if (!room) {
        e.active = false;
        return;
    }
    const std::string path = g.currentRoom_->path;
    TiledMap& map = room->map;
    const float tile = static_cast<float>(map.tileWidth());
    if (!map.object(e.selected)) {
        e.selected = 0;
    }

    // --- Keyboard ----------------------------------------------------------------------------------
    const auto undo = [&] {
        if (!room->undo.empty()) {
            room->redo.push_back(map.document());
            map.document() = std::move(room->undo.back());
            room->undo.pop_back();
            editorCommit(s, context, *room);
        }
    };
    const auto redo = [&] {
        if (!room->redo.empty()) {
            room->undo.push_back(map.document());
            map.document() = std::move(room->redo.back());
            room->redo.pop_back();
            editorCommit(s, context, *room);
        }
    };
    const auto removeSelected = [&] {
        if (e.selected) {
            snapshot(*room);
            map.removeObject(e.selected);
            e.selected = 0;
            editorCommit(s, context, *room);
        }
    };
    const auto duplicateSelected = [&] {
        if (const JsonDocument* object = map.object(e.selected)) {
            snapshot(*room);
            JsonDocument copy = *object;
            copy["x"] = number(numberOf(copy, "x") + tile);
            e.selected = map.addObject(std::move(copy));
            editorCommit(s, context, *room);
        }
    };
    if (!io.WantTextInput) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            io.KeyShift ? redo() : undo();
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            redo();
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            editorSave(s, path, *room);
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
            duplicateSelected();
        } else if (!io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
                removeSelected();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_V, false)) e.tool = LevelEditor::Tool::Select;
            if (ImGui::IsKeyPressed(ImGuiKey_O, false)) e.tool = LevelEditor::Tool::Place;
            if (ImGui::IsKeyPressed(ImGuiKey_B, false)) e.tool = LevelEditor::Tool::Brush;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) e.tool = LevelEditor::Tool::Rect;
            if (ImGui::IsKeyPressed(ImGuiKey_G, false)) e.showGrid = !e.showGrid;
            // Place tool: 1-9 / 0 pick a template, [ and ] step through them.
            const size_t count = objectTemplates().size();
            for (int k = 0; k < 10 && e.tool == LevelEditor::Tool::Place; ++k) {
                const auto index = static_cast<size_t>(k == 0 ? 9 : k - 1);
                if (index < count && ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_0 + k), false)) {
                    e.placeType = index;
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false)) e.placeType = (e.placeType + count - 1) % count;
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false)) e.placeType = (e.placeType + 1) % count;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                e.selected = 0;
                e.tool = LevelEditor::Tool::Select;
            }
        }
    }

    // --- Mouse on the canvas -------------------------------------------------------------------------
    const Mapping m = mappingFor(context, g.camera_.topLeft(animal::Renderer2D::kCanvasExtent));
    const Vec2 mouse = m.toWorld(io.MousePos);
    const bool inRoom = mouse.x >= 0.0f && mouse.y >= 0.0f && mouse.x < map.width() * tile && mouse.y < map.height() * tile;
    const bool canvasMouse = !io.WantCaptureMouse && inRoom;
    const int tx = static_cast<int>(std::floor(mouse.x / tile));
    const int ty = static_cast<int>(std::floor(mouse.y / tile));

    if (canvasMouse && e.drag == LevelEditor::Drag::None) {
        const bool left = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool right = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        switch (e.tool) {
        case LevelEditor::Tool::Select:
            if (left) {
                const JsonDocument* current = map.object(e.selected);
                if (current && !isPoint(*current)) {
                    const Vec2 corner{numberOf(*current, "x") + numberOf(*current, "width"),
                                      numberOf(*current, "y") + numberOf(*current, "height")};
                    if (std::fabs(mouse.x - corner.x) * m.scale < 6.0f && std::fabs(mouse.y - corner.y) * m.scale < 6.0f) {
                        e.drag = LevelEditor::Drag::Resize;
                        e.dragOrigin = {numberOf(*current, "width"), numberOf(*current, "height")};
                    }
                }
                if (e.drag == LevelEditor::Drag::None) {
                    // Smallest object under the mouse, so points inside areas stay reachable.
                    int picked = 0;
                    float pickedArea = 1.0e9f;
                    for (const int id : map.objectIds()) {
                        const animal::RectF box = pickBox(*map.object(id));
                        if (contains(box, mouse) && box.width * box.height <= pickedArea) {
                            picked = id;
                            pickedArea = box.width * box.height;
                        }
                    }
                    e.selected = picked;
                    if (picked) {
                        const JsonDocument& object = *map.object(picked);
                        e.drag = LevelEditor::Drag::Move;
                        e.dragOrigin = {numberOf(object, "x"), numberOf(object, "y")};
                    }
                }
                e.dragStart = mouse;
                e.dragChanged = false;
            } else if (right) {
                e.contextPoint = mouse;
                ImGui::OpenPopup("editor context");
            }
            break;
        case LevelEditor::Tool::Place:
            if (left) {
                const ObjectTemplate& t = objectTemplates()[e.placeType];
                JsonDocument object = {{"id", 0}, {"name", ""}, {"type", t.type}};
                if (t.point) {
                    object["point"] = true;
                }
                object["rotation"] = 0;
                object["visible"] = true;
                object["x"] = number(snapped(mouse.x, e.snap));
                object["y"] = number(snapped(mouse.y, e.snap));
                object["width"] = t.point ? 0 : t.width;
                object["height"] = t.point ? 0 : t.height;
                for (const PropertyTemplate& p : t.initial) {
                    TiledMap::setProperty(object, p.name, p.type, JsonDocument::parse(p.value));
                }
                snapshot(*room);
                e.selected = map.addObject(std::move(object));
                editorCommit(s, context, *room);
            } else if (right) {
                e.tool = LevelEditor::Tool::Select;
            }
            break;
        case LevelEditor::Tool::Brush:
            if ((left || right) && io.KeyAlt) {
                e.brush = map.tile(e.tileLayer, tx, ty); // eyedropper
            } else if (left || right) {
                e.drag = LevelEditor::Drag::Paint;
                e.dragErase = right;
                e.dragChanged = false;
                snapshot(*room);
            }
            break;
        case LevelEditor::Tool::Rect:
            if (left || right) {
                e.drag = LevelEditor::Drag::Rect;
                e.dragErase = right;
                e.dragStart = mouse;
            }
            break;
        }
    }

    const bool held = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right);
    switch (e.drag) {
    case LevelEditor::Drag::Move:
    case LevelEditor::Drag::Resize:
        if (JsonDocument* object = map.object(e.selected)) {
            const Vec2 delta = mouse - e.dragStart;
            if (!e.dragChanged && std::fabs(delta.x) < 1.0f && std::fabs(delta.y) < 1.0f) {
                break; // a click, not (yet) a drag
            }
            if (!e.dragChanged) {
                snapshot(*room);
                e.dragChanged = true;
            }
            if (e.drag == LevelEditor::Drag::Move) {
                (*object)["x"] = number(snapped(e.dragOrigin.x + delta.x, e.snap));
                (*object)["y"] = number(snapped(e.dragOrigin.y + delta.y, e.snap));
            } else {
                (*object)["width"] = number(std::max(static_cast<float>(e.snap), snapped(e.dragOrigin.x + delta.x, e.snap)));
                (*object)["height"] = number(std::max(static_cast<float>(e.snap), snapped(e.dragOrigin.y + delta.y, e.snap)));
            }
        }
        break;
    case LevelEditor::Drag::Paint:
        if (inRoom && map.tile(e.tileLayer, tx, ty) != (e.dragErase ? 0u : e.brush)) {
            map.setTile(e.tileLayer, tx, ty, e.dragErase ? 0u : e.brush);
            e.dragChanged = true;
            editorCommit(s, context, *room); // tiles show at once
        }
        break;
    default:
        break;
    }
    if (e.drag != LevelEditor::Drag::None && e.drag != LevelEditor::Drag::Property && !held) {
        if ((e.drag == LevelEditor::Drag::Move || e.drag == LevelEditor::Drag::Resize) && e.dragChanged) {
            editorCommit(s, context, *room);
        } else if (e.drag == LevelEditor::Drag::Paint && !e.dragChanged && !room->undo.empty()) {
            room->undo.pop_back(); // nothing painted
        } else if (e.drag == LevelEditor::Drag::Rect) {
            const int x0 = static_cast<int>(std::floor(std::min(e.dragStart.x, mouse.x) / tile));
            const int y0 = static_cast<int>(std::floor(std::min(e.dragStart.y, mouse.y) / tile));
            const int x1 = static_cast<int>(std::floor(std::max(e.dragStart.x, mouse.x) / tile));
            const int y1 = static_cast<int>(std::floor(std::max(e.dragStart.y, mouse.y) / tile));
            snapshot(*room);
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    map.setTile(e.tileLayer, x, y, e.dragErase ? 0u : e.brush);
                }
            }
            editorCommit(s, context, *room);
        }
        e.drag = LevelEditor::Drag::None;
    }

    if (ImGui::BeginPopup("editor context")) {
        if (ImGui::MenuItem("Move the player here")) {
            g.placePlayerFeet(e.contextPoint);
            g.playerState_ = {};
            g.climb_ = {};
        }
        if (ImGui::MenuItem("Delete", "Del", false, e.selected != 0)) {
            removeSelected();
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, e.selected != 0)) {
            duplicateSelected();
        }
        ImGui::EndPopup();
    }

    drawOverlay(e, *room, m, mouse, canvasMouse);

    // --- Windows -----------------------------------------------------------------------------------
    ImGui::SetNextWindowPos({10.0f, 30.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({330.0f, std::max(300.0f, io.DisplaySize.y - 40.0f)}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Level Editor", &e.active)) {
        ImGui::Text("%s%s", s.sources.displayName(path).c_str(), room->dirty ? "  (modified)" : "");
        if (ImGui::Button("Save (Ctrl+S)")) {
            editorSave(s, path, *room);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(room->undo.empty());
        if (ImGui::Button("Undo")) {
            undo();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(room->redo.empty());
        if (ImGui::Button("Redo")) {
            redo();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Revert")) {
            s.editor.rooms.erase(path);
            e.selected = 0;
            g.reloadData(path, *context.renderer);
            ImGui::End();
            return;
        }
        if (!room->error.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored({1.0f, 0.42f, 0.38f, 1.0f}, "Not applied: %s", room->error.c_str());
            ImGui::PopTextWrapPos();
        }
        std::vector<std::string> unsaved;
        for (const auto& [roomPath, r] : s.editor.rooms) {
            if (r.dirty && roomPath != path) {
                unsaved.push_back(roomPath);
            }
        }
        if (!unsaved.empty()) {
            ImGui::TextColored({1.0f, 0.85f, 0.45f, 1.0f}, "%zu other room(s) unsaved", unsaved.size());
            ImGui::SameLine();
            if (ImGui::SmallButton("Save all")) {
                for (const std::string& roomPath : unsaved) {
                    editorSave(s, roomPath, s.editor.rooms.at(roomPath));
                }
            }
        }

        ImGui::SeparatorText("Tool");
        int tool = static_cast<int>(e.tool);
        ImGui::RadioButton("Select (V)", &tool, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Place (O)", &tool, 1);
        ImGui::RadioButton("Brush (B)", &tool, 2);
        ImGui::SameLine();
        ImGui::RadioButton("Fill rect (R)", &tool, 3);
        e.tool = static_cast<LevelEditor::Tool>(tool);
        static constexpr int kSnaps[] = {1, 2, 4, 8};
        ImGui::TextUnformatted("Snap");
        for (const int snap : kSnaps) {
            ImGui::SameLine();
            if (ImGui::RadioButton(std::format("{}px", snap).c_str(), e.snap == snap)) {
                e.snap = snap;
            }
        }
        ImGui::Checkbox("Grid (G)", &e.showGrid);
        ImGui::SameLine();
        ImGui::Checkbox("Labels", &e.showLabels);
        ImGui::TextDisabled("Tab: play   right-click: menu   Del: delete");
        ImGui::TextDisabled("Place: 1-9/0 or [ ] pick the object");

        if (e.tool == LevelEditor::Tool::Place) {
            ImGui::SeparatorText("Object to place");
            for (size_t i = 0; i < objectTemplates().size(); ++i) {
                const ObjectTemplate& t = objectTemplates()[i];
                ImGui::PushStyleColor(ImGuiCol_Text, t.color);
                const std::string label = i < 10 ? std::format("{}  {}", (i + 1) % 10, t.type) : std::format("   {}", t.type);
                if (ImGui::Selectable(label.c_str(), e.placeType == i)) {
                    e.placeType = i;
                }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", t.help);
                }
            }
        }

        if (e.tool == LevelEditor::Tool::Brush || e.tool == LevelEditor::Tool::Rect) {
            ImGui::SeparatorText("Tiles (right button erases, Alt+click picks)");
            const animal::LoadedTexture& sheet = g.assets_.tileset.albedo;
            const float sheetWidth = static_cast<float>(context.device->textureExtent(sheet.handle).width);
            const int firstGid = map.document()["tilesets"].empty()
                                     ? 1
                                     : map.document()["tilesets"][0].value("firstgid", 1);
            const int count = static_cast<int>(sheetWidth / tile);
            const animal::Room* data = g.roomData(*g.currentRoom_);
            for (int i = 0; i <= count; ++i) {
                const auto gid = static_cast<uint32_t>(i == 0 ? 0 : firstGid + i - 1);
                ImGui::PushID(i);
                if (i > 0 && i % 6 != 1) {
                    ImGui::SameLine();
                }
                const bool chosen = e.brush == gid;
                if (chosen) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.9f, 0.3f, 1.0f));
                }
                bool clicked = false;
                if (i == 0) {
                    clicked = ImGui::Button("none", {36.0f, 36.0f});
                } else {
                    const float u0 = static_cast<float>(i - 1) * tile / sheetWidth;
                    const float u1 = static_cast<float>(i) * tile / sheetWidth;
                    clicked = ImGui::ImageButton("tile", s.imgui.textureId(sheet.handle), {28.0f, 28.0f}, {u0, 0.0f},
                                                 {u1, 1.0f});
                }
                if (chosen) {
                    ImGui::PopStyleColor();
                }
                if (clicked) {
                    e.brush = gid;
                }
                if (ImGui::IsItemHovered() && data && gid < data->tileProperties.size()) {
                    const animal::Properties& p = data->tileProperties[gid];
                    ImGui::SetTooltip("tile %u%s%s", gid, p.getBool("solid") ? "  solid" : "",
                                      p.getBool("light") ? "  light" : "");
                }
                ImGui::PopID();
            }
            if (map.tileLayers().size() > 1 && ImGui::BeginCombo("Layer", e.tileLayer.c_str())) {
                for (const std::string& layer : map.tileLayers()) {
                    if (ImGui::Selectable(layer.c_str(), layer == e.tileLayer)) {
                        e.tileLayer = layer;
                    }
                }
                ImGui::EndCombo();
            }
        }

        // Inspector edits: the document as it was before this frame's widgets goes into the history
        // when an edit starts (one-shot widgets change and finish in the same frame).
        const JsonDocument before = map.document();
        const auto track = [&](bool activated, bool changed, bool committed) {
            if ((activated || changed) && e.drag != LevelEditor::Drag::Property) {
                room->undo.push_back(before);
                if (room->undo.size() > kUndoLimit) {
                    room->undo.erase(room->undo.begin());
                }
                room->redo.clear();
                e.drag = LevelEditor::Drag::Property;
            }
            if (changed) {
                room->dirty = true;
            }
            if (committed) {
                editorCommit(s, context, *room);
                e.drag = LevelEditor::Drag::None;
            }
        };
        if (JsonDocument* object = map.object(e.selected)) {
            ImGui::SeparatorText(std::format("Object {}", e.selected).c_str());
            const std::string type = typeOf(*object);
            const ObjectTemplate* t = templateOf(type);
            if (t) {
                ImGui::TextDisabled("%s", t->help);
            }
            bool changed = false;
            bool committed = false;
            bool activated = false;
            const auto note = [&](bool c) {
                activated |= ImGui::IsItemActivated();
                changed |= c;
                committed |= ImGui::IsItemDeactivatedAfterEdit();
            };
            ImGui::PushItemWidth(-ImGui::GetFontSize() * 4.0f);
            if (ImGui::BeginCombo("type", type.c_str())) {
                for (const ObjectTemplate& candidate : objectTemplates()) {
                    if (ImGui::Selectable(candidate.type, type == candidate.type)) {
                        (*object)["type"] = candidate.type;
                        changed = committed = true;
                    }
                }
                ImGui::EndCombo();
            }
            char name[128];
            std::snprintf(name, sizeof(name), "%s", object->value("name", "").c_str());
            if (ImGui::InputText("name", name, sizeof(name))) {
                (*object)["name"] = std::string(name);
                note(true);
            } else {
                note(false);
            }
            float position[2] = {numberOf(*object, "x"), numberOf(*object, "y")};
            const bool moved = ImGui::DragFloat2("x, y", position, 1.0f, 0.0f, 0.0f, "%.0f");
            if (moved) {
                (*object)["x"] = number(std::round(position[0]));
                (*object)["y"] = number(std::round(position[1]));
            }
            note(moved);
            if (!isPoint(*object)) {
                float size[2] = {numberOf(*object, "width"), numberOf(*object, "height")};
                const bool resized = ImGui::DragFloat2("w, h", size, 1.0f, 1.0f, 4096.0f, "%.0f");
                if (resized) {
                    (*object)["width"] = number(std::max(1.0f, std::round(size[0])));
                    (*object)["height"] = number(std::max(1.0f, std::round(size[1])));
                }
                note(resized);
            }
            ImGui::PopItemWidth();
            ImGui::TextDisabled("Properties");
            const PropertyEdit p = editProperties(*object, t ? t->known : std::vector<PropertyTemplate>{},
                                                  s.sources.dataDirectory());
            track(activated || p.activated, changed || p.changed, committed || p.committed);
            if (ImGui::Button("Delete")) {
                removeSelected();
            }
            ImGui::SameLine();
            if (ImGui::Button("Duplicate")) {
                duplicateSelected();
            }
        }

        if (ImGui::CollapsingHeader("Room properties")) {
            ImGui::PushID("room");
            const PropertyEdit p = editProperties(map.document(), mapProperties(), s.sources.dataDirectory());
            ImGui::PopID();
            track(p.activated, p.changed, p.committed);
        }
    }
    ImGui::End();
}

} // namespace game
