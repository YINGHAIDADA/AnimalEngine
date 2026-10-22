#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct SDL_Window;

namespace animal::platform {

enum class GraphicsApi {
    OpenGL,
    Vulkan,
};

struct WindowDesc {
    std::string title = "Animal Engine";
    int width = 1280; // in points (logical units); the pixel size depends on display density
    int height = 720;
    GraphicsApi api = GraphicsApi::OpenGL;
    bool debugContext = false;
};

struct PixelSize {
    uint32_t width = 0;
    uint32_t height = 0;
};

class Window {
public:
    static std::unique_ptr<Window> create(const WindowDesc& desc);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    PixelSize pixelSize() const;
    GraphicsApi api() const { return api_; }
    void show();
    void setSize(int width, int height); // in points

    // For RHI backends only (surface / context creation).
    SDL_Window* sdlWindow() const { return window_; }

private:
    Window(SDL_Window* window, GraphicsApi api) : window_(window), api_(api) {}

    SDL_Window* window_ = nullptr;
    GraphicsApi api_;
};

} // namespace animal::platform
