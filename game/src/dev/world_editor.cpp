// World layout (ADR-0029): where the rooms sit in the Tiled world file, plus creating, duplicating
// and removing rooms. Every change is checked by the game's own world parser (overlaps, sizes),
// written to the source tree and the deployed copy, and reloaded at once.

#include "dev/dev_state.h"
#include "sandbox_game.h"

#include <animal/app/application.h>
#include <animal/core/file.h>
#include <animal/core/log.h>
#include <animal/framework/level.h>
#include <animal/renderer/renderer2d.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

namespace game {

namespace {

using animal::devtools::JsonDocument;
using animal::devtools::TiledMap;
using animal::devtools::TiledWorld;

std::string stemOf(const std::string& fileName) {
    std::string name = fileName.substr(fileName.find_last_of('/') + 1);
    if (const size_t dot = name.find('.'); dot != std::string::npos) {
        name.resize(dot);
    }
    return name;
}

bool validRoomName(const std::string& name) {
    return !name.empty() && name.size() <= 48 &&
           std::ranges::all_of(name, [](unsigned char c) { return std::islower(c) || std::isdigit(c) || c == '_'; });
}

std::optional<TiledWorld> readWorld(const std::string& path, std::string& error) {
    const std::optional<std::string> text = animal::readText(path);
    if (!text) {
        error = "cannot read " + path;
        return std::nullopt;
    }
    return TiledWorld::parse(*text, &error);
}

animal::IntRect rectOf(const TiledWorld::Map& m) {
    return {m.x, m.y, m.width, m.height};
}

bool overlapsAny(const TiledWorld& world, const animal::IntRect& rect, const std::string& except) {
    for (const TiledWorld::Map& m : world.maps()) {
        if (m.fileName != except && rectOf(m).overlaps(rect)) {
            return true;
        }
    }
    return false;
}

// A new room's tiles: background everywhere and a floor (grass-topped ground on a wall row) along the
// bottom, chosen by the tileset's properties so it works with any tileset following ADR-0010.
void clearToFloor(TiledMap& map, const animal::Room& room) {
    // Tile properties are indexed by the tileset's own ids; the map stores global ids.
    const auto firstGid = static_cast<uint32_t>(
        map.document()["tilesets"].empty() ? 1 : map.document()["tilesets"][0].value("firstgid", 1));
    uint32_t ground = 0;
    uint32_t wall = 0;
    uint32_t background = 0;
    for (uint32_t id = 1; id < room.tileProperties.size(); ++id) {
        const animal::Properties& p = room.tileProperties[id];
        if (p.getBool("light")) {
            continue;
        }
        if (p.getBool("solid") && p.getBool("grass") && !ground) {
            ground = id + firstGid;
        } else if (p.getBool("solid") && !p.getBool("grass") && !wall) {
            wall = id + firstGid;
        } else if (!p.getBool("solid") && !p.getBool("grass") && !background) {
            background = id + firstGid;
        }
    }
    const std::vector<std::string> layers = map.tileLayers();
    if (!layers.empty()) { // only the first tile layer gets the floor
        for (int y = 0; y < map.height(); ++y) {
            for (int x = 0; x < map.width(); ++x) {
                const uint32_t gid = y == map.height() - 1 ? (wall ? wall : ground)
                                     : y == map.height() - 2 ? (ground ? ground : wall)
                                                             : background;
                map.setTile(layers.front(), x, y, gid);
            }
        }
    }
    for (JsonDocument& layer : map.document()["layers"]) {
        if (layer.value("type", "") == "objectgroup") {
            layer["objects"] = JsonDocument::array();
        }
    }
    map.document()["nextobjectid"] = 1;
}

} // namespace

bool DevPanels::applyWorld(State& s, animal::EngineContext& context, const TiledWorld& world) {
    SandboxGame& g = *s.game;
    WorldEditor& w = s.worldEditor;
    const std::string text = world.serialize();
    std::string error;
    if (!animal::parseWorld(text, g.worldPath_, &error)) {
        w.error = error;
        return false;
    }
    const std::optional<std::string> previous = animal::readText(g.worldPath_);
    if (!s.sources.write(g.worldPath_, text, &error)) {
        w.error = error;
        return false;
    }
    acknowledgeWrite(s, g.worldPath_);
    if (!g.reloadWorld(g.worldPath_, *context.renderer)) {
        // The game refused it (e.g. a room in use would disappear): put the old layout back.
        if (previous) {
            s.sources.write(g.worldPath_, *previous);
            acknowledgeWrite(s, g.worldPath_);
        }
        w.error = "the game refused the layout (see the console)";
        return false;
    }
    if (previous) {
        w.undo.push_back(*previous);
    }
    w.error.clear();
    animal::log::info("World layout saved ({} rooms)", world.maps().size());
    return true;
}

bool DevPanels::createRoom(State& s, animal::EngineContext& context, const std::string& name, bool copyCurrent) {
    SandboxGame& g = *s.game;
    WorldEditor& w = s.worldEditor;
    if (!validRoomName(name)) {
        w.error = "room names use a-z, 0-9 and _ (they are save keys)";
        return false;
    }
    std::optional<TiledWorld> world = readWorld(g.worldPath_, w.error);
    const std::string fileName = "rooms/" + name + ".tmj";
    const std::string path = animal::resolvePath(g.worldPath_, fileName);
    if (!world || !g.currentRoom_) {
        return false;
    }
    if (world->find(fileName) || animal::fileExists(path)) {
        w.error = fileName + " already exists";
        return false;
    }
    // Start from the current room (as edited, if it is open in the level editor).
    EditedRoom* edited = editorRoom(s);
    const animal::Room* current = g.roomData(*g.currentRoom_);
    if (!edited || !current) {
        w.error = "the current room cannot be read";
        return false;
    }
    TiledMap map = edited->map;
    if (!copyCurrent) {
        clearToFloor(map, *current);
    }
    // Next to the current room: right, below, left, above, then further out to the right.
    const animal::IntRect here = g.currentRoom_->rect;
    std::vector<animal::IntRect> candidates = {{here.x + here.width, here.y, here.width, here.height},
                                               {here.x, here.y + here.height, here.width, here.height},
                                               {here.x - here.width, here.y, here.width, here.height},
                                               {here.x, here.y - here.height, here.width, here.height}};
    for (int i = 2; i < 32; ++i) {
        candidates.push_back({here.x + i * here.width, here.y, here.width, here.height});
    }
    const auto free = std::ranges::find_if(candidates, [&](const animal::IntRect& r) { return !overlapsAny(*world, r, ""); });
    if (free == candidates.end()) {
        w.error = "no free place next to the current room";
        return false;
    }
    std::string error;
    if (!s.sources.write(path, map.serialize(), &error)) {
        w.error = error;
        return false;
    }
    acknowledgeWrite(s, path);
    world->add({fileName, free->x, free->y, here.width, here.height});
    if (!applyWorld(s, context, *world)) {
        return false;
    }
    for (const animal::WorldRoom& room : g.world_.rooms) {
        if (room.path == path) {
            g.teleport(room, *context.renderer);
        }
    }
    animal::log::info("Created room '{}'{}", name, copyCurrent ? " (copy of the current room)" : "");
    return true;
}

void DevPanels::world(State& s, animal::EngineContext& context) {
    SandboxGame& g = *s.game;
    WorldEditor& w = s.worldEditor;
    std::string readError;
    std::optional<TiledWorld> world = readWorld(g.worldPath_, readError);
    if (!world) {
        ImGui::TextColored({1.0f, 0.42f, 0.38f, 1.0f}, "%s", readError.c_str());
        return;
    }

    // --- Toolbar -----------------------------------------------------------------------------------
    if (ImGui::Button("New room...")) {
        ImGui::OpenPopup("new room");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(w.undo.empty());
    if (ImGui::Button("Undo layout")) {
        const std::string previous = w.undo.back();
        w.undo.pop_back();
        if (std::optional<TiledWorld> old = TiledWorld::parse(previous)) {
            const size_t depth = w.undo.size();
            applyWorld(s, context, *old);
            w.undo.resize(depth); // undoing is not itself undoable
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Snap to rooms", &w.snapToRooms);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    ImGui::SliderFloat("Zoom", &w.zoom, 0.08f, 0.6f, "%.2f");
    if (!w.error.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored({1.0f, 0.42f, 0.38f, 1.0f}, "%s", w.error.c_str());
        ImGui::PopTextWrapPos();
    }
    if (ImGui::BeginPopup("new room")) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
        const bool entered = ImGui::InputTextWithHint("##name", "name (a-z 0-9 _)", w.newName, sizeof(w.newName),
                                                      ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Checkbox("Copy the current room", &w.copyCurrent);
        ImGui::TextDisabled(w.copyCurrent ? "Same tiles and objects, new save keys"
                                          : "Empty, with a floor; same look and wind");
        if ((ImGui::Button("Create") || entered) && createRoom(s, context, w.newName, w.copyCurrent)) {
            w.newName[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::TextDisabled("Drag rooms to move them, double-click to go there, right-click for more");

    // --- Layout ------------------------------------------------------------------------------------
    const std::vector<TiledWorld::Map> maps = world->maps();
    animal::IntRect bounds = maps.empty() ? animal::IntRect{0, 0, 320, 176} : rectOf(maps.front());
    for (const TiledWorld::Map& m : maps) {
        const int right = std::max(bounds.x + bounds.width, m.x + m.width);
        const int bottom = std::max(bounds.y + bounds.height, m.y + m.height);
        bounds.x = std::min(bounds.x, m.x);
        bounds.y = std::min(bounds.y, m.y);
        bounds.width = right - bounds.x;
        bounds.height = bottom - bounds.y;
    }
    const int margin = maps.empty() ? 320 : maps.front().width; // room to drag into
    ImGui::BeginChild("layout", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const auto toScreen = [&](float x, float y) {
        return ImVec2{origin.x + (x - static_cast<float>(bounds.x - margin)) * w.zoom,
                      origin.y + (y - static_cast<float>(bounds.y - margin)) * w.zoom};
    };
    ImGui::Dummy({static_cast<float>(bounds.width + 2 * margin) * w.zoom,
                  static_cast<float>(bounds.height + 2 * margin) * w.zoom});
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const std::string currentFile = g.currentRoom_ ? g.currentRoom_->path : std::string();
    for (const TiledWorld::Map& m : maps) {
        const std::string path = animal::resolvePath(g.worldPath_, m.fileName);
        const bool current = path == currentFile;
        const bool dragging = w.dragging == m.fileName;
        animal::IntRect rect = rectOf(m);
        if (dragging) {
            const float stepX = w.snapToRooms ? static_cast<float>(m.width) : 8.0f;
            const float stepY = w.snapToRooms ? static_cast<float>(m.height) : 8.0f;
            rect.x += static_cast<int>(std::round(w.dragOffset.x / stepX) * stepX);
            rect.y += static_cast<int>(std::round(w.dragOffset.y / stepY) * stepY);
        }
        const bool blocked = dragging && overlapsAny(*world, rect, m.fileName);
        const ImVec2 a = toScreen(static_cast<float>(rect.x), static_cast<float>(rect.y));
        const ImVec2 b = toScreen(static_cast<float>(rect.x + rect.width), static_cast<float>(rect.y + rect.height));
        draw->AddRectFilled(a, b, current ? IM_COL32(60, 120, 110, 200) : IM_COL32(50, 55, 70, 200));
        draw->AddRect(a, b, blocked ? IM_COL32(255, 90, 90, 255) : IM_COL32(200, 210, 220, 255), 0.0f,
                      dragging ? 2.0f : 1.0f);
        const std::string label = stemOf(m.fileName);
        draw->AddText({a.x + 4.0f, a.y + 3.0f}, IM_COL32(235, 235, 235, 255), label.c_str());
        draw->AddText({a.x + 4.0f, a.y + 3.0f + ImGui::GetFontSize()}, IM_COL32(150, 160, 170, 255),
                      std::format("{},{}", rect.x, rect.y).c_str());
        if (current) {
            if (const animal::Entity* player = g.entities_.get(g.player_)) {
                const ImVec2 p = toScreen(static_cast<float>(rect.x + player->body.position.x),
                                          static_cast<float>(rect.y + player->body.position.y));
                draw->AddCircleFilled(p, 3.0f, IM_COL32(255, 255, 120, 255));
            }
        }

        // Interaction: the button covers the room where it is now (not where it is being dragged).
        const ImVec2 at = toScreen(static_cast<float>(m.x), static_cast<float>(m.y));
        ImGui::SetCursorScreenPos(at);
        ImGui::PushID(m.fileName.c_str());
        ImGui::InvisibleButton("room", {std::max(4.0f, m.width * w.zoom), std::max(4.0f, m.height * w.zoom)});
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            w.dragging = m.fileName;
            const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            w.dragOffset = {delta.x / w.zoom, delta.y / w.zoom};
        }
        if (ImGui::IsItemDeactivated() && dragging) {
            if (!blocked && (rect.x != m.x || rect.y != m.y)) {
                TiledWorld next = *world;
                next.move(m.fileName, rect.x, rect.y);
                applyWorld(s, context, next);
            }
            w.dragging.clear();
            w.dragOffset = {};
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            for (const animal::WorldRoom& room : g.world_.rooms) {
                if (room.path == path && !current) {
                    g.teleport(room, *context.renderer);
                }
            }
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("room menu");
        }
        if (ImGui::BeginPopup("room menu")) {
            const bool inUse = current || (g.startRoom_ && g.startRoom_->path == path) ||
                               (g.checkpointRoom_ && g.checkpointRoom_->path == path);
            ImGui::TextDisabled("%s", m.fileName.c_str());
            if (ImGui::MenuItem("Go there", nullptr, false, !current)) {
                for (const animal::WorldRoom& room : g.world_.rooms) {
                    if (room.path == path) {
                        g.teleport(room, *context.renderer);
                    }
                }
            }
            if (ImGui::MenuItem("Remove from the world", nullptr, false, !inUse)) {
                TiledWorld next = *world;
                next.remove(m.fileName);
                if (applyWorld(s, context, next)) {
                    animal::log::info("Removed {} from the world; the file is kept", m.fileName);
                }
            }
            if (inUse && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("The current, start or checkpoint room cannot be removed");
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

} // namespace game
