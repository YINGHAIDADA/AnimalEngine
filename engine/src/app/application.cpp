#include <animal/app/application.h>
#include <animal/core/log.h>
#include <animal/platform/input.h>
#include <animal/platform/platform.h>
#include <animal/platform/window.h>
#include <animal/renderer/renderer2d.h>
#include <animal/resources/loaders.h>
#include <animal/rhi/command_list.h>

#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <optional>
#include <string_view>

namespace animal {

namespace {

struct CommandLine {
    std::optional<rhi::Backend> backend;
    bool noVsync = false;
    bool hideOverlay = false;
    bool resizeTest = false;
    uint64_t exitAfterFrames = 0;
    std::string screenshotPath;
};

std::optional<CommandLine> parseCommandLine(int argc, char** argv) {
    CommandLine cl;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--backend=opengl") {
            cl.backend = rhi::Backend::OpenGL;
        } else if (arg == "--backend=vulkan") {
            cl.backend = rhi::Backend::Vulkan;
        } else if (arg == "--no-vsync") {
            cl.noVsync = true;
        } else if (arg == "--hide-overlay") {
            cl.hideOverlay = true;
        } else if (arg == "--resize-test") {
            cl.resizeTest = true;
        } else if (arg.starts_with("--frames=")) {
            cl.exitAfterFrames = std::strtoull(std::string(arg.substr(9)).c_str(), nullptr, 10);
        } else if (arg.starts_with("--screenshot=")) {
            cl.screenshotPath = std::string(arg.substr(13));
        } else if (arg.starts_with("-psn_")) {
            // Added by macOS when launched from Finder.
        } else {
            log::error("Unknown argument '{}'. Options: --backend=opengl|vulkan --no-vsync --hide-overlay "
                       "--resize-test --frames=N --screenshot=path.png",
                       arg);
            return std::nullopt;
        }
    }
    return cl;
}

bool saveScreenshot(rhi::Device& device, const std::string& path) {
    const rhi::Extent2D extent = device.swapchainExtent();
    std::vector<uint8_t> pixels = device.readTexture(device.swapchainTexture());
    if (pixels.empty()) {
        return false;
    }
    const auto slash = path.find_last_of("/\\");
    if (slash != std::string::npos) {
        SDL_CreateDirectory(path.substr(0, slash).c_str());
    }
    if (!writePng(path, extent.width, extent.height, pixels.data())) {
        log::error("Failed to write screenshot '{}'", path);
        return false;
    }
    log::info("Screenshot saved: {} ({}x{})", path, extent.width, extent.height);
    return true;
}

class FrameTimer {
public:
    void tick(double dt, FrameStats& stats) {
        ++stats.frameIndex;
        accumulated_ += dt;
        ++frames_;
        if (accumulated_ >= 0.5) {
            stats.fps = frames_ / accumulated_;
            stats.frameMilliseconds = 1000.0 * accumulated_ / frames_;
            accumulated_ = 0.0;
            frames_ = 0;
        }
    }

private:
    double accumulated_ = 0.0;
    int frames_ = 0;
};

void drawOverlay(EngineContext& context, Renderer2D& renderer, std::vector<std::string> lines) {
    const float scale = static_cast<float>(renderer.overlayScale());
    const float lineHeight = renderer.textLineHeight();
    const float margin = 4.0f * scale;

    size_t longest = 0;
    for (const std::string& line : lines) {
        longest = std::max(longest, line.size());
    }
    renderer.fillRect({margin, margin, static_cast<float>(longest) * 4.0f * scale + 2.0f * margin,
                       static_cast<float>(lines.size()) * lineHeight + 2.0f * margin - scale},
                      Color::rgba(0, 0, 0, 160));

    Vec2 position{2.0f * margin, 2.0f * margin};
    for (const std::string& line : lines) {
        renderer.drawText(position, line, Color::rgba(240, 240, 220));
        position.y += lineHeight;
    }
    (void)context;
}

} // namespace

void Application::onFrame(EngineContext&) {}

void Application::onDebugText(EngineContext&, std::vector<std::string>&) {}

void Application::onShutdown(EngineContext&) {}

int Application::run(int argc, char** argv) {
#if defined(ANIMAL_DEBUG)
    constexpr bool kDebugBuild = true;
#else
    constexpr bool kDebugBuild = false;
#endif
    log::setMinimumLevel(kDebugBuild ? log::Level::Debug : log::Level::Info);

    const std::optional<CommandLine> commandLine = parseCommandLine(argc, argv);
    if (!commandLine) {
        return 2;
    }
    AppConfig config = configure();
    if (commandLine->backend) {
        config.backend = *commandLine->backend;
    }
    if (commandLine->noVsync) {
        config.vsync = false;
    }
    if (!rhi::isBackendImplemented(config.backend)) {
        log::error("The {} backend is not available in this build", rhi::backendName(config.backend));
        return 1;
    }

    if (!platform::initialize()) {
        return 1;
    }

    int exitCode = 0;
    {
        platform::WindowDesc windowDesc;
        windowDesc.title = config.title;
        windowDesc.width = config.windowWidth;
        windowDesc.height = config.windowHeight;
        windowDesc.api = config.backend == rhi::Backend::Vulkan ? platform::GraphicsApi::Vulkan
                                                                : platform::GraphicsApi::OpenGL;
        windowDesc.debugContext = kDebugBuild;
        std::unique_ptr<platform::Window> window = platform::Window::create(windowDesc);

        std::unique_ptr<rhi::Device> device;
        if (window) {
            device = rhi::createDevice({config.backend, window.get(), config.vsync, kDebugBuild});
        }

        Renderer2D renderer;
        platform::Input input;
        EngineContext context;
        context.window = window.get();
        context.device = device.get();
        context.input = &input;
        context.renderer = &renderer;
        context.dataDirectory = platform::executableDirectory() + "data/";
        log::info("Backend: {} | data: {}", rhi::backendName(config.backend), context.dataDirectory);

        if (!device || !renderer.init(*device, context.dataDirectory) || !onInit(context)) {
            log::error("Startup failed; see the errors above");
            exitCode = 1;
        } else {
            window->show();
            rhi::CommandList commands;
            FrameTimer timer;
            bool overlayVisible = kDebugBuild && !commandLine->hideOverlay;
            uint64_t lastTime = platform::nowNanoseconds();
            double accumulator = 0.0;
            uint64_t framesRendered = 0;
            int screenshotCounter = 0;

            while (!context.quitRequested) {
                if (platform::pollEvents().quitRequested) {
                    break;
                }
                input.update();
                if (input.pressed(platform::Key::Escape)) {
                    break;
                }
                if (input.pressed(platform::Key::F1)) {
                    overlayVisible = !overlayVisible;
                }
                if (commandLine->resizeTest && framesRendered % 30 == 29) {
                    // Exercises swapchain recreation; ends on the configured size.
                    static constexpr int kSizes[][2] = {{800, 450}, {1000, 700}, {333, 222}};
                    const uint64_t step = framesRendered / 30;
                    if (step < std::size(kSizes)) {
                        window->setSize(kSizes[step][0], kSizes[step][1]);
                    } else {
                        window->setSize(config.windowWidth, config.windowHeight);
                    }
                }

                const uint64_t now = platform::nowNanoseconds();
                const double frameTime = std::min(static_cast<double>(now - lastTime) * 1e-9, 0.25);
                lastTime = now;
                timer.tick(frameTime, context.stats);
                onFrame(context);

                accumulator += frameTime;
                int steps = 0;
                while (accumulator >= kFixedTimestep && steps < kMaxStepsPerFrame) {
                    onFixedUpdate(context, kFixedTimestep);
                    accumulator -= kFixedTimestep;
                    ++steps;
                }
                if (steps == kMaxStepsPerFrame) {
                    accumulator = 0.0; // drop the backlog instead of spiraling
                }

                const bool lastFrame =
                    commandLine->exitAfterFrames != 0 && framesRendered + 1 >= commandLine->exitAfterFrames;
                std::string screenshotPath;
                if (lastFrame && !commandLine->screenshotPath.empty()) {
                    screenshotPath = commandLine->screenshotPath;
                } else if (input.pressed(platform::Key::F12)) {
                    screenshotPath = std::format("screenshots/screenshot_{:03}.png", ++screenshotCounter);
                }

                if (device->beginFrame()) {
                    commands.reset();
                    renderer.beginFrame(commands);
                    onRender(context, renderer);
                    renderer.beginScreen(Color::black());
                    if (overlayVisible) {
                        const rhi::Extent2D extent = device->swapchainExtent();
                        const PresentLayout& layout = renderer.presentLayout();
                        std::vector<std::string> lines = {
                            std::format("FPS {:.1f}  FRAME {:.2f} MS", context.stats.fps,
                                        context.stats.frameMilliseconds),
                            std::format("{} | {}", rhi::backendName(device->backend()), device->caps().deviceName),
                            std::format("WINDOW {}X{}  CANVAS {}X{}  SCALE {}X", extent.width, extent.height,
                                        Renderer2D::kCanvasExtent.width, Renderer2D::kCanvasExtent.height,
                                        layout.scale),
                            std::format("SPRITES {}  DRAWS {}", renderer.stats().quads, renderer.stats().drawCalls),
                        };
                        onDebugText(context, lines);
                        lines.emplace_back("F1 OVERLAY  F12 SCREENSHOT  ESC QUIT");
                        drawOverlay(context, renderer, std::move(lines));
                    }
                    renderer.endScreen();
                    renderer.endFrame();
                    device->submit(commands);
                    if (!screenshotPath.empty()) {
                        saveScreenshot(*device, screenshotPath);
                    }
                    device->endFrame();
                    ++framesRendered;
                }

                if (lastFrame) {
                    break;
                }
            }
            onShutdown(context);
        }

        if (device) {
            device->waitIdle();
            renderer.shutdown();
        }
    }

    platform::shutdown();
    return exitCode;
}

} // namespace animal
