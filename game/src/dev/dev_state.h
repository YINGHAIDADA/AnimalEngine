#pragma once

// Internal to the developer tools: the state shared by the core (dev_tools.cpp) and the panels.

#include "dev/dev_tools.h"

#include <animal/devtools/console.h>
#include <animal/devtools/file_watcher.h>
#include <animal/devtools/imgui_layer.h>
#include <animal/devtools/json_editor.h>
#include <animal/devtools/performance_panel.h>
#include <animal/devtools/source_assets.h>
#include <animal/devtools/tiled_map.h>
#include <animal/core/math.h>

#include <map>
#include <string>
#include <vector>

namespace animal {
struct EngineContext;
}

namespace game {

class SandboxGame;

// A data file opened in a JSON editor (looks, effects, plants).
struct PresetEditor {
    std::string path; // deployed path
    animal::devtools::JsonDocument doc;
    std::string error; // why the edited document is not applied (parse / validation)
    bool dirty = false; // edited since loaded or saved
};

// A room open in the level editor (ADR-0029): its Tiled document and edit history.
struct EditedRoom {
    animal::devtools::TiledMap map;
    std::vector<animal::devtools::JsonDocument> undo;
    std::vector<animal::devtools::JsonDocument> redo;
    bool dirty = false; // edited since loaded or saved
    std::string error;  // why the edited map is not applied (it does not load)
};

struct LevelEditor {
    enum class Tool { Select, Place, Brush, Rect };
    enum class Drag { None, Move, Resize, Paint, Rect, Property };

    bool active = false;
    Tool tool = Tool::Select;
    int snap = 4;        // px for placing and moving objects
    uint32_t brush = 2;  // tile global id
    std::string tileLayer = "tiles";
    size_t placeType = 0; // index into the object templates
    int selected = 0;     // object id, 0 = none
    bool showGrid = true;
    bool showLabels = true;

    Drag drag = Drag::None;
    animal::Vec2 dragStart;      // world px where the drag began
    animal::Vec2 dragOrigin;     // object position (move) or size (resize) at the start
    bool dragChanged = false;    // the document changed during this drag
    bool dragErase = false;      // right button: paint / fill with nothing
    animal::Vec2 contextPoint;   // world px of the last right click (context menu)

    std::map<std::string, EditedRoom> rooms; // by deployed path
};

struct DevTools::State {
    SandboxGame* game = nullptr;
    animal::devtools::ImGuiLayer imgui;
    animal::devtools::Console console;
    animal::devtools::PerformancePanel performance;
    animal::devtools::SourceAssets sources;
    bool visible = false;

    struct Windows {
        bool performance = true;
        bool rendering = false;
        bool look = false;
        bool effects = false;
        bool plants = false;
        bool tuning = false;
        bool rooms = false;
        bool animations = false;
        bool console = false;
        bool demo = false;
    } windows;

    LevelEditor editor;

    PresetEditor look;
    PresetEditor effect;
    PresetEditor plant;
    bool tuningDirty = false;
    std::string previewTag = "idle"; // Animations window
    double previewTime = 0.0;

    // Hot reload (ADR-0028)
    animal::devtools::FileWatcher watcher;
    bool hotReload = true;
    uint32_t reloads = 0;
    uint32_t reloadFailures = 0;
};

// Reads `path` into the editor (replacing its document); false (and editor.error) on failure.
bool openPreset(PresetEditor& editor, const std::string& path);
// Writes the document to its source and deployed files.
bool savePreset(DevTools::State& state, PresetEditor& editor);
// The tools wrote a deployed data file (and its source): the watcher must not report it.
void acknowledgeWrite(DevTools::State& state, const std::string& dataPath);

// The windows; a friend of SandboxGame (they inspect and edit its state).
struct DevPanels {
    using State = DevTools::State;
    static void performance(State& state, animal::EngineContext& context);
    static void rendering(State& state, animal::EngineContext& context);
    static void look(State& state, animal::EngineContext& context);
    static void effects(State& state, animal::EngineContext& context);
    static void plants(State& state, animal::EngineContext& context);
    static void tuning(State& state, animal::EngineContext& context);
    static void rooms(State& state, animal::EngineContext& context);
    static void animations(State& state, animal::EngineContext& context);

    // Level editor (level_editor.cpp).
    static void editorFrame(State& state, animal::EngineContext& context);
    static void setEditing(State& state, animal::EngineContext& context, bool active);
    static EditedRoom* editorRoom(State& state); // the current room, opened on first use
    // Applies the edited document to the running game (if it loads); false keeps the old room.
    static bool editorCommit(State& state, animal::EngineContext& context, EditedRoom& room);
    static bool editorSave(State& state, const std::string& path, EditedRoom& room);

    // Hot reload (hot_reload.cpp): starts watching, then applies changed files every frame.
    static void startWatching(State& state);
    static void hotReload(State& state, animal::EngineContext& context);
    static void reloadFile(State& state, animal::EngineContext& context, const std::string& changed);
};

} // namespace game
