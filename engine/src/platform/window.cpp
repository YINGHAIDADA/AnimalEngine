#include <animal/core/log.h>
#include <animal/platform/window.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <string>
#include <vector>

namespace animal::platform {

namespace {

void configureOpenGLAttributes(bool debugContext) {
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    int flags = SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG;
    if (debugContext) {
        flags |= SDL_GL_CONTEXT_DEBUG_FLAG;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, flags);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 0);
}

// SDL's default lookup is by file name only; recent macOS no longer searches /usr/local/lib for
// such names, so development builds also try the Vulkan SDK explicitly. Release builds will
// bundle the loader next to the executable (M7).
bool loadVulkanLoader() {
    if (SDL_Vulkan_LoadLibrary(nullptr)) {
        return true;
    }
    const std::string defaultError = SDL_GetError();

    std::vector<std::string> candidates;
#if defined(__APPLE__)
    const char* base = SDL_GetBasePath();
    if (base) {
        candidates.push_back(std::string(base) + "libvulkan.1.dylib");
    }
    if (const char* sdk = SDL_getenv("VULKAN_SDK")) {
        candidates.push_back(std::string(sdk) + "/macOS/lib/libvulkan.1.dylib");
        candidates.push_back(std::string(sdk) + "/lib/libvulkan.1.dylib");
    }
    candidates.emplace_back("/usr/local/lib/libvulkan.1.dylib");
#endif
    for (const std::string& path : candidates) {
        if (SDL_Vulkan_LoadLibrary(path.c_str())) {
            log::info("Vulkan loader: {}", path);
            return true;
        }
    }
    log::error("Could not load the Vulkan loader ({}). Install the Vulkan SDK or a Vulkan driver.", defaultError);
    return false;
}

// The largest multiple of the 320×180 canvas that leaves some room on the primary display.
void defaultSize(int& width, int& height) {
    SDL_Rect usable{};
    int scale = 4;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.w > 0 && usable.h > 0) {
        scale = std::max(1, std::min(usable.w * 85 / 100 / 320, usable.h * 85 / 100 / 180));
    }
    width = 320 * scale;
    height = 180 * scale;
}

} // namespace

std::unique_ptr<Window> Window::create(const WindowDesc& desc) {
    int width = desc.width;
    int height = desc.height;
    if (width <= 0 || height <= 0) {
        defaultSize(width, height);
    }
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    switch (desc.api) {
    case GraphicsApi::OpenGL:
        configureOpenGLAttributes(desc.debugContext);
        flags |= SDL_WINDOW_OPENGL;
        break;
    case GraphicsApi::Vulkan:
        if (!loadVulkanLoader()) {
            return nullptr;
        }
        flags |= SDL_WINDOW_VULKAN;
        break;
    }

    SDL_Window* window = SDL_CreateWindow(desc.title.c_str(), width, height, flags);
    if (!window) {
        log::error("SDL_CreateWindow failed: {}", SDL_GetError());
        if (desc.api == GraphicsApi::Vulkan) {
            SDL_Vulkan_UnloadLibrary();
        }
        return nullptr;
    }
    return std::unique_ptr<Window>(new Window(window, desc.api));
}

Window::~Window() {
    SDL_DestroyWindow(window_);
    if (api_ == GraphicsApi::Vulkan) {
        SDL_Vulkan_UnloadLibrary();
    }
}

PixelSize Window::pixelSize() const {
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) {
        return {};
    }
    return {static_cast<uint32_t>(w > 0 ? w : 0), static_cast<uint32_t>(h > 0 ? h : 0)};
}

PixelSize Window::sizeInPoints() const {
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    return {static_cast<uint32_t>(w > 0 ? w : 0), static_cast<uint32_t>(h > 0 ? h : 0)};
}

void Window::setTextInput(bool enabled) {
    if (enabled == SDL_TextInputActive(window_)) {
        return;
    }
    if (enabled) {
        SDL_StartTextInput(window_);
    } else {
        SDL_StopTextInput(window_);
    }
}

void Window::show() {
    SDL_ShowWindow(window_);
}

void Window::setSize(int width, int height) {
    SDL_SetWindowSize(window_, width, height);
    SDL_SyncWindow(window_);
}

void Window::setFullscreen(bool fullscreen) {
    if (fullscreen && !this->fullscreen()) {
        windowed_ = placement();
    }
    if (!SDL_SetWindowFullscreen(window_, fullscreen)) {
        log::warn("Cannot {} fullscreen: {}", fullscreen ? "enter" : "leave", SDL_GetError());
    }
    SDL_SyncWindow(window_);
}

bool Window::fullscreen() const {
    return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
}

WindowRect Window::placement() const {
    if (fullscreen()) {
        return windowed_;
    }
    WindowRect r;
    SDL_GetWindowPosition(window_, &r.x, &r.y);
    SDL_GetWindowSize(window_, &r.width, &r.height);
    return r;
}

void Window::setPlacement(const WindowRect& rect) {
    WindowRect r = rect;
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    bool visible = false;
    for (int i = 0; i < count && !visible; ++i) {
        SDL_Rect usable{};
        if (SDL_GetDisplayUsableBounds(displays[i], &usable)) {
            // At least a corner big enough to grab and drag back.
            const int overlapW = std::min(r.x + r.width, usable.x + usable.w) - std::max(r.x, usable.x);
            const int overlapH = std::min(r.y + r.height, usable.y + usable.h) - std::max(r.y, usable.y);
            visible = overlapW >= 64 && overlapH >= 64;
        }
    }
    SDL_free(displays);
    if (!visible) {
        SDL_Rect usable{};
        if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable)) {
            r.width = std::min(r.width, usable.w);
            r.height = std::min(r.height, usable.h);
            r.x = usable.x + (usable.w - r.width) / 2;
            r.y = usable.y + (usable.h - r.height) / 2;
        }
        log::info("The saved window position is off every display; centring the window");
    }
    SDL_SetWindowSize(window_, r.width, r.height);
    SDL_SetWindowPosition(window_, r.x, r.y);
    SDL_SyncWindow(window_);
}

} // namespace animal::platform
