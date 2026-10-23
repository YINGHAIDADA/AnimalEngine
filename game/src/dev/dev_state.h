#pragma once

// Internal to the developer tools: the state shared by the core (dev_tools.cpp) and the panels.

#include "dev/dev_tools.h"

#include <animal/devtools/console.h>
#include <animal/devtools/file_watcher.h>
#include <animal/devtools/imgui_layer.h>
#include <animal/devtools/json_editor.h>
#include <animal/devtools/performance_panel.h>
#include <animal/devtools/source_assets.h>

#include <string>

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
        bool console = false;
        bool demo = false;
    } windows;

    PresetEditor look;
    PresetEditor effect;
    PresetEditor plant;
    bool tuningDirty = false;

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

    // Hot reload (hot_reload.cpp): starts watching, then applies changed files every frame.
    static void startWatching(State& state);
    static void hotReload(State& state, animal::EngineContext& context);
    static void reloadFile(State& state, animal::EngineContext& context, const std::string& changed);
};

} // namespace game
