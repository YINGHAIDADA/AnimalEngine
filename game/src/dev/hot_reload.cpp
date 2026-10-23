// Hot reload (ADR-0028): watches the source tree (or, without it, the deployed data), brings changed
// files into the data directory (copy, or compile for shaders) and reloads them into the running game.
// Every step validates before it replaces anything, so a broken file is reported and the game keeps
// running with the previous version.

#include "dev/dev_state.h"
#include "sandbox_game.h"

#include <animal/app/application.h>
#include <animal/core/file.h>
#include <animal/core/log.h>
#include <animal/platform/platform.h>
#include <animal/resources/loaders.h>

#include <algorithm>
#include <cctype>

namespace game {

namespace {

bool isShaderSource(const std::string& path) {
    return path.ends_with(".vert") || path.ends_with(".frag");
}

std::string upper(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

std::string fileName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Runs animal_shaderc like the build does; the output is only replaced when compilation succeeds.
bool compileShader(const std::string& source, const std::string& output) {
#if defined(ANIMAL_SHADERC_PATH)
    const std::string stage = source.substr(source.find_last_of('.') + 1);
    const animal::platform::ProcessResult result =
        animal::platform::runProcess({ANIMAL_SHADERC_PATH, "--stage", stage, "-o", output, source});
    if (result.exitCode != 0) {
        animal::log::error("Shader compilation failed for {}:\n{}", source,
                           result.output.empty() ? std::string("(no output)") : result.output);
        return false;
    }
    return true;
#else
    animal::log::error("Cannot compile {}: this build does not know animal_shaderc", source);
    (void)output;
    return false;
#endif
}

} // namespace

void acknowledgeWrite(DevTools::State& s, const std::string& dataPath) {
    if (const std::optional<std::string> source = s.sources.sourceOf(dataPath)) {
        s.watcher.acknowledge(*source);
    } else {
        s.watcher.acknowledge(dataPath);
    }
}

void DevPanels::startWatching(State& s) {
    const std::string root = s.sources.available() ? s.sources.sourceDirectory() : s.sources.dataDirectory();
    const std::vector<std::string> directories =
        s.sources.available()
            ? std::vector<std::string>{root + "assets/source/world", root + "assets/source/textures", root + "shaders"}
            : std::vector<std::string>{root + "world", root + "textures", root + "shaders"};
    for (const std::string& directory : directories) {
        s.watcher.watchDirectory(directory);
    }
    animal::log::info("Hot reload: watching {}{}", root,
                      s.sources.available() ? " (source tree)" : " (deployed data only)");
}

void DevPanels::hotReload(State& s, animal::EngineContext& context) {
    if (!s.hotReload) {
        return;
    }
    for (const std::string& changed : s.watcher.poll()) {
        reloadFile(s, context, changed);
    }
}

void DevPanels::reloadFile(State& s, animal::EngineContext& context, const std::string& changed) {
    SandboxGame& g = *s.game;
    const std::string name = s.sources.displayName(changed);
    std::string dataPath = changed;
    bool ok = true;
    std::optional<std::vector<std::byte>> previous; // the deployed copy before a source file replaced it

    if (s.sources.available()) {
        const std::optional<std::string> deployed = s.sources.dataOf(changed);
        if (!deployed) {
            return; // e.g. a shader include or notes next to the sources
        }
        dataPath = *deployed;
        if (isShaderSource(changed)) {
            ok = compileShader(changed, dataPath);
        } else if (name.starts_with("shaders/")) {
            return; // CMakeLists.txt and other files next to the shader sources
        } else {
            std::string error;
            previous = animal::readFile(dataPath);
            ok = s.sources.deploy(changed, &error);
            if (!ok) {
                animal::log::error("Hot reload: {}", error);
            }
        }
    }

    bool handled = true;
    if (ok) {
        const std::string relative = s.sources.displayName(dataPath);
        if (relative.ends_with(".ashd")) {
            ok = animal::reloadShaderFile(*context.device, dataPath) != animal::ReloadResult::Failed;
        } else if (relative.starts_with("textures/") && relative.ends_with(".png")) {
            ok = animal::reloadTextureFile(*context.device, dataPath) != animal::ReloadResult::Failed;
        } else if (relative.starts_with("world/")) {
            ok = g.reloadData(dataPath, *context.renderer);
        } else {
            handled = false;
        }
    }
    if (!handled) {
        return;
    }

    // Editors showing the file follow it unless they hold unsaved changes.
    if (const auto edited = s.editor.rooms.find(dataPath); edited != s.editor.rooms.end()) {
        if (edited->second.dirty) {
            animal::log::warn("{} changed on disk while the level editor has unsaved edits", name);
        } else {
            s.editor.rooms.erase(edited); // reopened from the new file when next edited
        }
    }
    for (PresetEditor* editor : {&s.look, &s.effect, &s.plant}) {
        if (editor->path == dataPath) {
            if (editor->dirty) {
                animal::log::warn("{} changed on disk while it has unsaved edits in the tools", name);
            } else {
                openPreset(*editor, dataPath);
            }
        }
    }
    if (ok) {
        ++s.reloads;
        animal::log::info("Hot reload: {}", name);
        g.showMessage("RELOADED " + upper(fileName(name)));
    } else {
        // Keep the deployed data matching what runs. (A shader or texture that only needs a restart
        // stays deployed: it is valid.)
        if (previous && dataPath.find("/world/") != std::string::npos) {
            animal::writeFileAtomic(dataPath, previous->data(), previous->size());
        }
        ++s.reloadFailures;
        animal::log::error("Hot reload failed: {} (the previous version stays in use)", name);
        g.showMessage("RELOAD FAILED " + upper(fileName(name)) + " - SEE CONSOLE");
    }
}

} // namespace game
