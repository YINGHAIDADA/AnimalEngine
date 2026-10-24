#pragma once

#include <animal/rhi/rhi.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace animal {

namespace platform {
class Input;
class Window;
struct FrameEvents;
} // namespace platform

namespace rhi {
class CommandList;
}

namespace audio {
class AudioSystem;
}

class Renderer2D;

struct AppConfig {
    std::string title = "Animal Engine";
    std::string applicationId = "Game"; // the per-user folder (saves, logs/, crashes/)
    int windowWidth = 1280; // points; 0 = the largest multiple of 320×180 that fits the display
    int windowHeight = 720;
    rhi::Backend backend = rhi::Backend::OpenGL;
    bool vsync = true;
};

struct FrameStats {
    double fps = 0.0;
    double frameMilliseconds = 0.0;
    uint64_t frameIndex = 0;
};

struct EngineContext {
    platform::Window* window = nullptr;
    rhi::Device* device = nullptr;
    platform::Input* input = nullptr;
    const platform::FrameEvents* events = nullptr; // this frame's OS events (valid during onFrame)
    Renderer2D* renderer = nullptr;
    audio::AudioSystem* audio = nullptr; // silent without a device (--mute, headless)
    std::string dataDirectory; // with trailing separator
    bool customData = false;   // --data: a data directory other than the deployed one (test data)
    int frameCap = 0;          // frames per second the loop sleeps down to (0: none; use with vsync off)
    FrameStats stats;
    bool demo = false;          // --demo: the game drives itself with scripted input
    bool deterministic = false; // --deterministic: one fixed step per frame (reproducible runs)
    bool escapeQuits = true;    // Escape ends the run (games turn this off while it means "back")
    int exitCode = 0;           // non-zero: the run failed (e.g. a playthrough expectation was not met)
    float overlayTop = 0.0f;    // window pixels kept free above the debug overlay (tool menu bars)
    bool overlaySuppressed = false; // tools that need the whole view hide the debug overlay
    bool quitRequested = false;
};

// Owns the main loop: fixed-timestep simulation (60 Hz, bounded catch-up) and variable-rate
// rendering. Command line: --backend=opengl|vulkan|null (--headless = null: no window)  --no-vsync  --hide-overlay  --resize-test
// --deterministic (one fixed step per frame)  --demo (scripted input)  --frames=N  --screenshot=path.png
class Application {
public:
    static constexpr double kFixedTimestep = 1.0 / 60.0;
    static constexpr int kMaxStepsPerFrame = 5;

    virtual ~Application() = default;
    int run(int argc, char** argv);

protected:
    virtual AppConfig configure() { return {}; }
    // Command-line arguments the engine does not know; return false to reject (the run stops).
    virtual bool onArgument(std::string_view argument);
    virtual bool onInit(EngineContext& context) = 0;
    // Once per rendered frame, before the fixed steps (input sampling, debug toggles).
    virtual void onFrame(EngineContext& context);
    virtual void onFixedUpdate(EngineContext& context, double dt) = 0;
    // Render the world into the canvas: renderer.beginWorld() .. renderer.endWorld().
    virtual void onRender(EngineContext& context, Renderer2D& renderer) = 0;
    // Screen-space HUD in window pixels, drawn over the presented canvas (before the debug overlay).
    virtual void onScreen(EngineContext& context, Renderer2D& renderer);
    virtual void onDebugText(EngineContext& context, std::vector<std::string>& lines);
    // Developer tools over the finished frame (after the debug overlay): record a render pass that
    // loads and stores the swapchain texture.
    virtual void onToolsPass(EngineContext& context, rhi::CommandList& commands);
    virtual void onShutdown(EngineContext& context);
};

} // namespace animal
