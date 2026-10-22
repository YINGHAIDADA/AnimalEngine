#pragma once

#include <animal/rhi/rhi.h>

#include <cstdint>
#include <string>
#include <vector>

namespace animal {

namespace platform {
class Input;
class Window;
} // namespace platform

class Renderer2D;

struct AppConfig {
    std::string title = "Animal Engine";
    int windowWidth = 1280;
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
    Renderer2D* renderer = nullptr;
    std::string dataDirectory; // with trailing separator
    FrameStats stats;
    bool quitRequested = false;
};

// Owns the main loop: fixed-timestep simulation (60 Hz, bounded catch-up) and variable-rate
// rendering. Command line: --backend=opengl|vulkan  --no-vsync  --frames=N  --screenshot=path.png
class Application {
public:
    static constexpr double kFixedTimestep = 1.0 / 60.0;
    static constexpr int kMaxStepsPerFrame = 5;

    virtual ~Application() = default;
    int run(int argc, char** argv);

protected:
    virtual AppConfig configure() { return {}; }
    virtual bool onInit(EngineContext& context) = 0;
    virtual void onFixedUpdate(EngineContext& context, double dt) = 0;
    // Render the world into the canvas: renderer.beginWorld() .. renderer.endWorld().
    virtual void onRender(EngineContext& context, Renderer2D& renderer) = 0;
    virtual void onDebugText(EngineContext& context, std::vector<std::string>& lines);
    virtual void onShutdown(EngineContext& context);
};

} // namespace animal
