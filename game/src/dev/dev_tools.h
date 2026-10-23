#pragma once

#include <memory>
#include <string_view>

namespace animal {
struct EngineContext;
namespace rhi {
class CommandList;
}
} // namespace animal

namespace game {

class SandboxGame;

// Developer tools of the sandbox (ADR-0026): debug panels, parameter tweaking, hot reload and
// the level editor. Without ANIMAL_DEV_TOOLS every call is a no-op.
class DevTools {
public:
    DevTools();
    ~DevTools();
    DevTools(const DevTools&) = delete;
    DevTools& operator=(const DevTools&) = delete;

    // --tools[=window,window...]: open the UI (and the named windows) at start.
    bool handleArgument(std::string_view argument);
    bool init(animal::EngineContext& context, SandboxGame& game);
    // Start of every frame, before the game reads input: UI, hot reload, editor.
    void frame(animal::EngineContext& context);
    void render(animal::EngineContext& context, animal::rhi::CommandList& commands);
    void shutdown(animal::EngineContext& context);

    // The level editor owns the room: the game must not simulate or take player input.
    bool editing() const;

    struct State;

private:
    std::unique_ptr<State> state_;
};

} // namespace game
