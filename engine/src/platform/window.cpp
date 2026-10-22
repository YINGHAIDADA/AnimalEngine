#include <animal/core/log.h>
#include <animal/platform/window.h>

#include <SDL3/SDL.h>

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

} // namespace

std::unique_ptr<Window> Window::create(const WindowDesc& desc) {
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    switch (desc.api) {
    case GraphicsApi::OpenGL:
        configureOpenGLAttributes(desc.debugContext);
        flags |= SDL_WINDOW_OPENGL;
        break;
    case GraphicsApi::Vulkan:
        flags |= SDL_WINDOW_VULKAN;
        break;
    }

    SDL_Window* window = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, flags);
    if (!window) {
        log::error("SDL_CreateWindow failed: {}", SDL_GetError());
        return nullptr;
    }
    return std::unique_ptr<Window>(new Window(window, desc.api));
}

Window::~Window() {
    SDL_DestroyWindow(window_);
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

void Window::show() {
    SDL_ShowWindow(window_);
}

} // namespace animal::platform
