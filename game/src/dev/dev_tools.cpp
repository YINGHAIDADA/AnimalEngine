#include "dev/dev_tools.h"

#include "sandbox_game.h"

#include <animal/app/application.h>
#include <animal/core/log.h>
#include <animal/devtools/imgui_layer.h>
#include <animal/platform/input.h>
#include <animal/platform/platform.h>
#include <animal/renderer/renderer2d.h>

#include <imgui.h>

namespace game {

using animal::platform::Key;

struct DevTools::State {
    SandboxGame* game = nullptr;
    animal::devtools::ImGuiLayer imgui;
    bool visible = false;
    bool showDemo = false;
};

DevTools::DevTools() : state_(std::make_unique<State>()) {}

DevTools::~DevTools() = default;

bool DevTools::handleArgument(std::string_view argument) {
    if (argument != "--tools" && !argument.starts_with("--tools=")) {
        return false;
    }
    state_->visible = true;
    std::string_view list = argument.size() > 8 ? argument.substr(8) : std::string_view();
    while (!list.empty()) {
        const size_t comma = list.find(',');
        const std::string_view name = list.substr(0, comma);
        if (name == "demo") {
            state_->showDemo = true;
        }
        list = comma == std::string_view::npos ? std::string_view() : list.substr(comma + 1);
    }
    return true;
}

bool DevTools::init(animal::EngineContext& context, SandboxGame& game) {
    state_->game = &game;
    const std::string userDir = animal::platform::userDataDirectory("AnimalEngine", "Sandbox");
    if (!state_->imgui.init(*context.device, *context.window, context.dataDirectory + "shaders/",
                            userDir.empty() ? std::string() : userDir + "devtools.ini")) {
        animal::log::error("DevTools: the debug UI could not be initialized");
        return false;
    }
    return true;
}

void DevTools::frame(animal::EngineContext& context) {
    State& s = *state_;
    animal::platform::Input& input = *context.input;
    if (input.pressed(Key::Grave)) { // reads as released while a text field has the keyboard
        s.visible = !s.visible;
    }
    if (!s.visible) {
        input.setCaptured(false, false);
        return;
    }
    s.imgui.beginFrame(*context.events);

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("ImGui demo", nullptr, &s.showDemo);
            ImGui::EndMenu();
        }
        ImGui::Text("%.1f FPS  %.2f ms", context.stats.fps, context.stats.frameMilliseconds);
        ImGui::EndMainMenuBar();
    }
    if (s.showDemo) {
        ImGui::ShowDemoWindow(&s.showDemo);
    }
    input.setCaptured(s.imgui.wantsKeyboard(), s.imgui.wantsMouse());
}

void DevTools::render(animal::EngineContext&, animal::rhi::CommandList& commands) {
    state_->imgui.render(commands);
}

void DevTools::shutdown(animal::EngineContext&) {
    state_->imgui.shutdown();
}

bool DevTools::editing() const {
    return false;
}

} // namespace game
