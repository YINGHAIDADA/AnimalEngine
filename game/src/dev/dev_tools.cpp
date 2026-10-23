#include "dev/dev_tools.h"

#include "dev/dev_state.h"
#include "sandbox_game.h"

#include <animal/app/application.h>
#include <animal/core/file.h>
#include <animal/core/log.h>
#include <animal/platform/input.h>
#include <animal/platform/platform.h>
#include <animal/renderer/renderer2d.h>

#include <imgui.h>

namespace game {

using animal::platform::Key;

namespace {

// --tools=<names>: windows to open at start.
void openWindow(DevTools::State::Windows& w, std::string_view name) {
    if (name == "performance") w.performance = true;
    else if (name == "rendering") w.rendering = true;
    else if (name == "look") w.look = true;
    else if (name == "effects") w.effects = true;
    else if (name == "plants") w.plants = true;
    else if (name == "tuning") w.tuning = true;
    else if (name == "rooms") w.rooms = true;
    else if (name == "animations") w.animations = true;
    else if (name == "world") w.world = true;
    else if (name == "console") w.console = true;
    else if (name == "demo") w.demo = true;
    else animal::log::warn("--tools: unknown window '{}'", name);
}

void mainMenu(DevTools::State& s, animal::EngineContext& context) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    DevTools::State::Windows& w = s.windows;
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Performance", nullptr, &w.performance);
        ImGui::MenuItem("Rendering", nullptr, &w.rendering);
        ImGui::MenuItem("Rooms", nullptr, &w.rooms);
        ImGui::MenuItem("World", nullptr, &w.world);
        ImGui::Separator();
        ImGui::MenuItem("Look", nullptr, &w.look);
        ImGui::MenuItem("Effects", nullptr, &w.effects);
        ImGui::MenuItem("Plants", nullptr, &w.plants);
        ImGui::MenuItem("Tuning", nullptr, &w.tuning);
        ImGui::MenuItem("Animations", nullptr, &w.animations);
        ImGui::Separator();
        ImGui::MenuItem("Console", nullptr, &w.console);
        ImGui::MenuItem("ImGui demo", nullptr, &w.demo);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Editor")) {
        bool editing = s.editor.active;
        if (ImGui::MenuItem("Edit room", "Tab", &editing)) {
            DevPanels::setEditing(s, context, editing);
        }
        ImGui::MenuItem("World layout", nullptr, &w.world);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Reload")) {
        ImGui::MenuItem("Watch files", nullptr, &s.hotReload);
        ImGui::TextDisabled("%s", s.sources.available() ? "source tree: assets/source, shaders"
                                                        : "deployed data only (no source tree)");
        ImGui::TextDisabled("%u reloaded, %u failed", s.reloads, s.reloadFailures);
        ImGui::EndMenu();
    }
    ImGui::TextDisabled("`  hide");
    const float right = ImGui::GetWindowWidth() - ImGui::GetFontSize() * 16.0f;
    ImGui::SameLine(right);
    ImGui::Text("%5.1f FPS %5.2f ms", context.stats.fps, context.stats.frameMilliseconds);
    const uint32_t errors = s.console.unseenErrors();
    const uint32_t warnings = s.console.unseenWarnings();
    if (errors + warnings > 0) {
        ImGui::SameLine();
        const ImVec4 color = errors > 0 ? ImVec4(1.0f, 0.42f, 0.38f, 1.0f) : ImVec4(1.0f, 0.8f, 0.35f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        if (ImGui::SmallButton(errors > 0 ? "errors" : "warnings")) {
            w.console = true;
        }
        ImGui::PopStyleColor();
    }
    context.overlayTop = ImGui::GetWindowHeight() * ImGui::GetIO().DisplayFramebufferScale.y;
    ImGui::EndMainMenuBar();
}

// `position`: first-use placement in points; negative x counts from the right edge.
template <typename Draw>
void window(const char* title, bool& open, ImVec2 position, ImVec2 size, Draw&& draw) {
    if (!open) {
        return;
    }
    if (position.x < 0.0f) {
        position.x += ImGui::GetIO().DisplaySize.x - size.x;
    }
    ImGui::SetNextWindowPos(position, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(title, &open)) {
        draw();
    }
    ImGui::End();
}

} // namespace

DevTools::DevTools() : state_(std::make_unique<State>()) {
    state_->console.attach(); // from the start, so startup messages are kept
}

DevTools::~DevTools() = default;

bool DevTools::handleArgument(std::string_view argument) {
    if (argument != "--tools" && !argument.starts_with("--tools=")) {
        return false;
    }
    state_->visible = true;
    std::string_view list = argument.size() > 8 ? argument.substr(8) : std::string_view();
    while (!list.empty()) {
        const size_t comma = list.find(',');
        openWindow(state_->windows, list.substr(0, comma));
        list = comma == std::string_view::npos ? std::string_view() : list.substr(comma + 1);
    }
    return true;
}

bool DevTools::init(animal::EngineContext& context, SandboxGame& game) {
    State& s = *state_;
    s.game = &game;
    if (!context.window || animal::packMountedAt(context.dataDirectory)) {
        // Headless runs have no window; packed data is read-only and has no tool shaders.
        animal::log::info("Developer tools are off: {}", context.window ? "the data comes from a resource pack"
                                                                          : "no window (headless)");
        s.enabled = false;
        s.visible = false;
        return true;
    }
    s.sources.init(context.dataDirectory);
    const std::string userDir = animal::platform::userDataDirectory("AnimalEngine", "Sandbox");
    if (!s.imgui.init(*context.device, *context.window, context.dataDirectory + "shaders/",
                      userDir.empty() ? std::string() : userDir + "devtools.ini")) {
        animal::log::error("DevTools: the debug UI could not be initialized");
        return false;
    }
    if (!context.deterministic && !context.demo) {
        DevPanels::startWatching(s); // reproducible runs never change under foot
    } else {
        s.hotReload = false;
    }
    return true;
}

void DevTools::frame(animal::EngineContext& context) {
    State& s = *state_;
    if (!s.enabled) {
        return;
    }
    s.performance.sample(context);
    DevPanels::hotReload(s, context);
    animal::platform::Input& input = *context.input;
    if (input.pressed(Key::Grave)) { // reads as released while a text field has the keyboard
        s.visible = !s.visible;
    }
    if (input.pressed(Key::Tab)) {
        DevPanels::setEditing(s, context, !s.editor.active);
    }
    if (!s.visible) {
        DevPanels::setEditing(s, context, false);
        input.setCaptured(false, false);
        context.overlayTop = 0.0f;
        context.escapeQuits = true;
        context.overlaySuppressed = false;
        return;
    }
    context.escapeQuits = false; // Escape belongs to the tools while they are shown
    context.overlaySuppressed = s.editor.active;
    s.imgui.beginFrame(*context.events);
    mainMenu(s, context);
    DevPanels::editorFrame(s, context);

    State::Windows& w = s.windows;
    window("Performance", w.performance, {-10, 30}, {360, 430}, [&] { DevPanels::performance(s, context); });
    window("Rendering", w.rendering, {10, 30}, {300, 250}, [&] { DevPanels::rendering(s, context); });
    window("Rooms", w.rooms, {10, 290}, {300, 200}, [&] { DevPanels::rooms(s, context); });
    window("Look", w.look, {320, 30}, {420, 520}, [&] { DevPanels::look(s, context); });
    window("Effects", w.effects, {340, 50}, {460, 540}, [&] { DevPanels::effects(s, context); });
    window("Plants", w.plants, {360, 70}, {440, 480}, [&] { DevPanels::plants(s, context); });
    window("Tuning", w.tuning, {380, 90}, {420, 560}, [&] { DevPanels::tuning(s, context); });
    window("Animations", w.animations, {400, 110}, {360, 420}, [&] { DevPanels::animations(s, context); });
    window("World", w.world, {350, 40}, {560, 440}, [&] { DevPanels::world(s, context); });
    if (w.console) {
        s.console.draw(&w.console);
    }
    if (w.demo) {
        ImGui::ShowDemoWindow(&w.demo);
    }
    input.setCaptured(s.imgui.wantsKeyboard(), s.imgui.wantsMouse());
}

void DevTools::render(animal::EngineContext&, animal::rhi::CommandList& commands) {
    if (state_->enabled) {
        state_->imgui.render(commands);
    }
}

void DevTools::shutdown(animal::EngineContext&) {
    state_->imgui.shutdown();
    state_->console.detach();
}

bool DevTools::editing() const {
    return state_->editor.active;
}

} // namespace game
