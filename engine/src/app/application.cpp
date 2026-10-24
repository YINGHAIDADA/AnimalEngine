#include "input_script.h"

#include <animal/app/application.h>
#include <animal/audio/audio.h>
#include <animal/core/file.h>
#include <animal/core/log.h>
#include <animal/core/profiler.h>
#include <animal/platform/input.h>
#include <animal/platform/platform.h>
#include <animal/platform/window.h>
#include <animal/renderer/renderer2d.h>
#include <animal/resources/loaders.h>
#include <animal/rhi/command_list.h>
#include <animal/rhi/validation.h>

#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace animal {

namespace {

struct CommandLine {
    std::optional<rhi::Backend> backend;
    bool noVsync = false;
    bool hideOverlay = false;
    bool resizeTest = false;
    bool deterministic = false;
    bool demo = false;
    bool profile = false;
    uint64_t exitAfterFrames = 0;
    std::string screenshotPath;
    std::string canvasScreenshotPath; // the 320×180 final canvas, independent of window size and DPI
    std::string inputScriptPath;
    std::string recordInputPath; // --record-input: write the keys pressed as an input script
    std::string dataPath;        // --data: read the data from this directory (tests use their own)
    std::string packPath; // --pack: the resource pack to read the data from
    bool strict = false;  // --strict: RHI validation errors fail the run (exit code 4)
    bool mute = false;     // --mute: no audio device
    std::string crashTest; // --crash-test=segv|abort|exception: crash on purpose after start-up
    std::string crashDirectory; // --crash-dir: where crash reports go (default: <user dir>/crashes)
};

std::optional<CommandLine> parseCommandLine(int argc, char** argv,
                                            const std::function<bool(std::string_view)>& other) {
    CommandLine cl;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--backend=opengl") {
            cl.backend = rhi::Backend::OpenGL;
        } else if (arg == "--backend=vulkan") {
            cl.backend = rhi::Backend::Vulkan;
        } else if (arg == "--backend=null" || arg == "--headless") {
            cl.backend = rhi::Backend::Null;
        } else if (arg == "--no-vsync") {
            cl.noVsync = true;
        } else if (arg == "--hide-overlay") {
            cl.hideOverlay = true;
        } else if (arg == "--resize-test") {
            cl.resizeTest = true;
        } else if (arg == "--deterministic") {
            cl.deterministic = true;
        } else if (arg == "--demo") {
            cl.demo = true;
        } else if (arg.starts_with("--frames=")) {
            cl.exitAfterFrames = std::strtoull(std::string(arg.substr(9)).c_str(), nullptr, 10);
        } else if (arg.starts_with("--screenshot=")) {
            cl.screenshotPath = std::string(arg.substr(13));
        } else if (arg.starts_with("--canvas-screenshot=")) {
            cl.canvasScreenshotPath = std::string(arg.substr(20));
        } else if (arg == "--profile") {
            cl.profile = true;
        } else if (arg.starts_with("--crash-test=")) {
            cl.crashTest = std::string(arg.substr(13));
        } else if (arg.starts_with("--crash-dir=")) {
            cl.crashDirectory = std::string(arg.substr(12));
        } else if (arg == "--mute") {
            cl.mute = true;
        } else if (arg == "--strict") {
            cl.strict = true;
        } else if (arg.starts_with("--pack=")) {
            cl.packPath = std::string(arg.substr(7));
        } else if (arg.starts_with("--data=")) {
            cl.dataPath = std::string(arg.substr(7));
        } else if (arg.starts_with("--record-input=")) {
            cl.recordInputPath = std::string(arg.substr(15));
        } else if (arg.starts_with("--input-script=")) {
            cl.inputScriptPath = std::string(arg.substr(15));
        } else if (arg.starts_with("-psn_")) {
            // Added by macOS when launched from Finder.
        } else if (!other(arg)) {
            log::error("Unknown argument '{}'. Options: --backend=opengl|vulkan|null --headless --no-vsync --hide-overlay "
                       "--resize-test --deterministic --demo --profile --frames=N --screenshot=path.png "
                       "--canvas-screenshot=path.png --input-script=path.txt --record-input=path.txt --data=dir --pack=data.pak --strict --mute --crash-test=segv|abort|exception --crash-dir=path",
                       arg);
            return std::nullopt;
        }
    }
    return cl;
}

bool saveScreenshot(rhi::Device& device, rhi::TextureHandle texture, rhi::Extent2D extent, const std::string& path) {
    std::vector<uint8_t> pixels = device.readTexture(texture);
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

std::string upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

// "GPU 1.23 MS  MATERIAL 0.05  FLUID 0.30 ..." for the outermost scope and its direct children.
std::string formatGpuTimings(const std::vector<rhi::GpuTiming>& timings) {
    if (timings.empty()) {
        return "GPU TIMING UNAVAILABLE";
    }
    std::string line;
    for (const rhi::GpuTiming& t : timings) {
        if (t.depth == 0) {
            line += std::format("{}GPU {:.2f} MS", line.empty() ? "" : "  ", t.milliseconds);
        } else if (t.depth == 1) {
            line += std::format("  {} {:.2f}", upper(t.name), t.milliseconds);
        }
    }
    return line;
}

// --profile: per-scope GPU averages and the CPU frame time over the run, logged at exit.
class Profile {
public:
    static constexpr uint64_t kWarmupFrames = 60;

    void sample(uint64_t frame, double frameMilliseconds, const std::vector<rhi::GpuTiming>& timings,
                uint32_t drawCalls, const std::vector<profiler::ZoneTiming>& zones) {
        if (frame < kWarmupFrames) {
            return;
        }
        cpu_.add(frameMilliseconds);
        draws_.add(drawCalls);
        for (const profiler::ZoneTiming& z : zones) {
            auto it = std::find_if(zones_.begin(), zones_.end(), [&z](const Scope& s) { return s.name == z.name; });
            if (it == zones_.end()) {
                zones_.push_back({z.name, z.depth, {}});
                it = zones_.end() - 1;
            }
            it->stat.add(z.milliseconds);
        }
        for (const rhi::GpuTiming& t : timings) {
            auto it = std::find_if(scopes_.begin(), scopes_.end(), [&t](const Scope& s) { return s.name == t.name; });
            if (it == scopes_.end()) {
                scopes_.push_back({t.name, t.depth, {}});
                it = scopes_.end() - 1;
            }
            it->stat.add(t.milliseconds);
        }
    }

    void report() const {
        if (cpu_.count == 0) {
            log::info("Profile: fewer than {} frames, nothing to report", kWarmupFrames + 1);
            return;
        }
        log::info("Profile over {} frames (after {} warm-up): average / max in ms", cpu_.count, kWarmupFrames);
        log::info("  {:<18} {:7.3f} {:7.3f}", "frame (wall)", cpu_.average(), cpu_.max);
        log::info("  {:<18} {:7.1f} {:7.0f}", "draw calls", draws_.average(), draws_.max);
        for (const Scope& z : zones_) {
            log::info("  {:<18} {:7.3f} {:7.3f}", std::string(z.depth * 2, ' ') + "cpu " + z.name, z.stat.average(),
                      z.stat.max);
        }
        for (const Scope& s : scopes_) {
            log::info("  {:<18} {:7.3f} {:7.3f}", std::string(s.depth * 2, ' ') + "gpu " + s.name, s.stat.average(),
                      s.stat.max);
        }
    }

private:
    struct Stat {
        double sum = 0.0;
        double max = 0.0;
        uint64_t count = 0;
        void add(double v) {
            sum += v;
            max = std::max(max, v);
            ++count;
        }
        double average() const { return count ? sum / static_cast<double>(count) : 0.0; }
    };
    struct Scope {
        std::string name;
        uint32_t depth;
        Stat stat;
    };
    Stat cpu_;
    Stat draws_;
    std::vector<Scope> scopes_;
    std::vector<Scope> zones_; // CPU zones of the frame (ANIMAL_PROFILE_ZONE)
};

void drawOverlay(EngineContext& context, Renderer2D& renderer, std::vector<std::string> lines) {
    const float scale = static_cast<float>(renderer.overlayScale());
    const float lineHeight = renderer.textLineHeight();
    const float margin = 4.0f * scale;

    size_t longest = 0;
    for (const std::string& line : lines) {
        longest = std::max(longest, line.size());
    }
    const float top = context.overlayTop;
    renderer.fillRect({margin, top + margin, static_cast<float>(longest) * 4.0f * scale + 2.0f * margin,
                       static_cast<float>(lines.size()) * lineHeight + 2.0f * margin - scale},
                      Color::rgba(0, 0, 0, 160));

    Vec2 position{2.0f * margin, top + 2.0f * margin};
    for (const std::string& line : lines) {
        renderer.drawText(position, line, Color::rgba(240, 240, 220));
        position.y += lineHeight;
    }
}

} // namespace

// --crash-test: proves that crash reports are written (tests and support).
[[noreturn]] static void crashOnPurpose(const std::string& mode) {
    log::error("--crash-test={}: crashing on purpose", mode);
    if (mode == "abort") {
        std::abort();
    }
    if (mode == "exception") {
        throw std::runtime_error("crash test");
    }
    volatile int* null = nullptr;
    *null = 1; // segv
    std::abort();
}

void Application::onFrame(EngineContext&) {}

void Application::onScreen(EngineContext&, Renderer2D&) {}

void Application::onDebugText(EngineContext&, std::vector<std::string>&) {}

void Application::onToolsPass(EngineContext&, rhi::CommandList&) {}

void Application::onShutdown(EngineContext&) {}

bool Application::onArgument(std::string_view) {
    return false;
}

int Application::run(int argc, char** argv) {
#if defined(ANIMAL_DEBUG)
    constexpr bool kDebugBuild = true;
#else
    constexpr bool kDebugBuild = false;
#endif
    log::setMinimumLevel(kDebugBuild ? log::Level::Debug : log::Level::Info);

    const std::optional<CommandLine> commandLine = parseCommandLine(argc, argv, [this](std::string_view arg) { return onArgument(arg); });
    if (!commandLine) {
        return 2;
    }
    AppConfig config = configure();
    // Diagnostics first, so start-up problems are captured too (ADR-0032).
    const std::string userDirectory = platform::userDataDirectory("AnimalEngine", config.applicationId.c_str());
    if (!userDirectory.empty()) {
        log::startFileLog(userDirectory + "logs", config.applicationId);
    }
    const std::string crashDirectory = !commandLine->crashDirectory.empty() ? commandLine->crashDirectory
                                       : !userDirectory.empty()            ? userDirectory + "crashes"
                                                                           : std::string();
    if (!crashDirectory.empty()) {
        platform::installCrashHandler(crashDirectory, log::fileLogPath());
    }
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
        const bool headless = config.backend == rhi::Backend::Null;
        std::unique_ptr<platform::Window> window = headless ? nullptr : platform::Window::create(windowDesc);

        std::unique_ptr<rhi::Device> device;
        if (window || headless) {
            device = rhi::createDevice({config.backend, window.get(), config.vsync, kDebugBuild});
        }

        Renderer2D renderer;
        platform::Input input;
        audio::AudioSystem audio;
        audio.init(!headless && !commandLine->mute);
        EngineContext context;
        context.window = window.get();
        context.device = device.get();
        context.input = &input;
        context.renderer = &renderer;
        context.audio = &audio;
        context.dataDirectory = platform::executableDirectory() + "data/";
        if (!commandLine->dataPath.empty()) {
            context.dataDirectory = commandLine->dataPath;
            if (context.dataDirectory.back() != '/' && context.dataDirectory.back() != '\\') {
                context.dataDirectory += '/';
            }
            context.customData = true;
        }
        // A release reads its data from data.pak next to the executable (ADR-0031); --pack picks one.
        const uint64_t loadStart = platform::nowNanoseconds();
        std::string packPath = commandLine->packPath;
        if (packPath.empty() && !context.customData && fileExists(platform::executableDirectory() + "data.pak")) {
            packPath = platform::executableDirectory() + "data.pak";
        }
        bool packFailed = false;
        if (!packPath.empty()) {
            std::string packError;
            if (!mountPack(packPath, context.dataDirectory, &packError)) {
                log::error("Cannot use the resource pack: {}", packError);
                packFailed = true;
            }
        }
        context.demo = commandLine->demo;
        context.deterministic = commandLine->deterministic;
        log::info("Backend: {} | data: {}", rhi::backendName(config.backend), context.dataDirectory);

        std::optional<InputScript> inputScript;
        std::optional<InputRecorder> recorder;
        if (!commandLine->recordInputPath.empty()) {
            recorder.emplace();
        }
        if (packFailed ||
            (!commandLine->inputScriptPath.empty() && !(inputScript = InputScript::load(commandLine->inputScriptPath)))) {
            exitCode = 1;
        } else if (!device || !renderer.init(*device, context.dataDirectory) || !onInit(context)) {
            log::error("Startup failed; see the errors above");
            exitCode = 1;
        } else {
            if (!commandLine->crashTest.empty()) {
                crashOnPurpose(commandLine->crashTest);
            }
            log::info("Startup: data loaded in {:.1f} ms ({})",
                      static_cast<double>(platform::nowNanoseconds() - loadStart) * 1e-6,
                      packPath.empty() ? "loose files" : "resource pack");
            if (window) {
                window->show();
            }
            rhi::CommandList commands;
            FrameTimer timer;
            Profile profile;
            bool overlayVisible = kDebugBuild && !commandLine->hideOverlay;
            uint64_t lastTime = platform::nowNanoseconds();
            double accumulator = 0.0;
            uint64_t framesRendered = 0;
            int screenshotCounter = 0;
            uint64_t frameStart = platform::nowNanoseconds();

            while (!context.quitRequested) {
                frameStart = platform::nowNanoseconds();
                profiler::beginFrame();
                std::optional<profiler::Zone> eventsZone(std::in_place, "events");
                platform::FrameEvents events = platform::pollEvents();
                if (events.quitRequested) {
                    break;
                }
                if (recorder) {
                    recorder->record(framesRendered, events.input);
                }
                if (events.audioDevicesChanged) {
                    audio.devicesChanged();
                }
                if (inputScript) {
                    // Canvas pixel centres in window points, from the last frame's layout.
                    const PresentLayout& layout = renderer.presentLayout();
                    const platform::PixelSize pixels = window ? window->pixelSize() : platform::PixelSize{1, 1};
                    const platform::PixelSize points = window ? window->sizeInPoints() : platform::PixelSize{1, 1};
                    const float density = points.width > 0 && pixels.width > 0
                                              ? static_cast<float>(pixels.width) / static_cast<float>(points.width)
                                              : 1.0f;
                    inputScript->emit(framesRendered, [&](float x, float y) {
                        const auto scale = static_cast<float>(layout.scale);
                        return platform::MousePosition{(static_cast<float>(layout.x) + (x + 0.5f) * scale) / density,
                                                       (static_cast<float>(layout.y) + (y + 0.5f) * scale) / density};
                    }, events.input);
                }
                input.update(events);
                context.events = &events;
                if (context.escapeQuits && input.pressed(platform::Key::Escape)) {
                    break;
                }
                if (input.pressed(platform::Key::F1)) {
                    overlayVisible = !overlayVisible;
                }
                if (window && commandLine->resizeTest && framesRendered % 30 == 29) {
                    // Exercises swapchain recreation; ends on the configured size.
                    static constexpr int kSizes[][2] = {{800, 450}, {1000, 700}, {333, 222}};
                    const uint64_t step = framesRendered / 30;
                    if (step < std::size(kSizes)) {
                        window->setSize(kSizes[step][0], kSizes[step][1]);
                    } else {
                        window->setSize(config.windowWidth, config.windowHeight);
                    }
                }

                eventsZone.reset();
                const uint64_t now = platform::nowNanoseconds();
                const double realFrameTime = std::min(static_cast<double>(now - lastTime) * 1e-9, 0.25);
                lastTime = now;
                timer.tick(realFrameTime, context.stats);
                if (commandLine->profile) {
                    profile.sample(framesRendered, realFrameTime * 1000.0, device->gpuTimings(),
                                   renderer.stats().drawCalls, profiler::lastFrame());
                }
                // Deterministic runs advance exactly one fixed step per frame (reproducible captures).
                const double frameTime = commandLine->deterministic ? kFixedTimestep : realFrameTime;
                {
                    ANIMAL_PROFILE_ZONE("update");
                    onFrame(context);
                }

                accumulator += frameTime;
                int steps = 0;
                while (accumulator >= kFixedTimestep && steps < kMaxStepsPerFrame) {
                    ANIMAL_PROFILE_ZONE("simulation");
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

                std::optional<profiler::Zone> acquireZone(std::in_place, "acquire");
                const bool frameStarted = device->beginFrame();
                acquireZone.reset();
                if (frameStarted) {
                    std::optional<profiler::Zone> renderZone(std::in_place, "render");
                    commands.reset();
                    renderer.beginFrame(commands);
                    onRender(context, renderer);
                    renderer.beginScreen(Color::black());
                    onScreen(context, renderer);
                    if (overlayVisible && !context.overlaySuppressed) {
                        const rhi::Extent2D extent = device->swapchainExtent();
                        const PresentLayout& layout = renderer.presentLayout();
                        std::vector<std::string> lines = {
                            std::format("FPS {:.1f}  FRAME {:.2f} MS", context.stats.fps,
                                        context.stats.frameMilliseconds),
                            std::format("{} | {}", rhi::backendName(device->backend()), device->caps().deviceName),
                            std::format("WINDOW {}X{}  CANVAS {}X{}  SCALE {}X", extent.width, extent.height,
                                        Renderer2D::kCanvasExtent.width, Renderer2D::kCanvasExtent.height,
                                        layout.scale),
                            std::format("SPRITES {}  DRAWS {}  LIGHTS {}", renderer.stats().quads,
                                        renderer.stats().drawCalls, renderer.lightCount()),
                            formatGpuTimings(device->gpuTimings()),
                        };
                        onDebugText(context, lines);
                        lines.emplace_back("F1 OVERLAY  F12 SCREENSHOT  ESC QUIT");
                        drawOverlay(context, renderer, std::move(lines));
                    }
                    renderer.endScreen();
                    onToolsPass(context, commands);
                    renderer.endFrame();
                    renderZone.reset();
                    {
                        ANIMAL_PROFILE_ZONE("submit");
                        device->submit(commands);
                    }
                    if (!screenshotPath.empty()) {
                        saveScreenshot(*device, device->swapchainTexture(), device->swapchainExtent(), screenshotPath);
                    }
                    if (lastFrame && !commandLine->canvasScreenshotPath.empty()) {
                        saveScreenshot(*device, renderer.finalCanvas(), Renderer2D::kCanvasExtent,
                                       commandLine->canvasScreenshotPath);
                    }
                    {
                        ANIMAL_PROFILE_ZONE("present"); // includes waiting for vsync
                        device->endFrame();
                    }
                    ++framesRendered;
                }
                profiler::endFrame();

                if (lastFrame) {
                    break;
                }
                // Frame cap (without vsync): sleep out the rest of the frame.
                if (context.frameCap > 0 && !commandLine->deterministic && window) {
                    const uint64_t budget = 1'000'000'000ull / static_cast<uint64_t>(context.frameCap);
                    const uint64_t spent = platform::nowNanoseconds() - frameStart;
                    if (spent < budget) {
                        platform::sleepNanoseconds(budget - spent);
                    }
                }
            }
            context.events = nullptr;
            if (commandLine->profile) {
                profile.report();
            }
            if (recorder) {
                recorder->save(commandLine->recordInputPath);
            }
            onShutdown(context);
            exitCode = std::max(exitCode, context.exitCode);
        }

        if (device) {
            device->waitIdle();
            renderer.shutdown();
            if (const auto* validating = dynamic_cast<const rhi::ValidatingDevice*>(device.get());
                commandLine->strict && validating && validating->errorCount() > 0) {
                log::error("--strict: {} RHI validation error(s)", validating->errorCount());
                exitCode = std::max(exitCode, 4);
            }
        }
    }

    unmountPacks();
    platform::shutdown();
    log::stopFileLog();
    return exitCode;
}

} // namespace animal
