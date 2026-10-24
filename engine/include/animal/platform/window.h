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
    // In points (logical units); the pixel size depends on display density. 0: the largest multiple
    // of 320×180 that fits comfortably on the primary display.
    int width = 1280;
    int height = 720;
    GraphicsApi api = GraphicsApi::OpenGL;
    bool debugContext = false;
};

struct PixelSize {
    uint32_t width = 0;
    uint32_t height = 0;
};

// A window's position and size on the desktop, in points.
struct WindowRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

class Window {
public:
    static std::unique_ptr<Window> create(const WindowDesc& desc);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    PixelSize pixelSize() const;
    // Window size in points: the unit of mouse positions. pixelSize() / sizeInPoints() is the
    // display density (2 on Retina).
    PixelSize sizeInPoints() const;
    GraphicsApi api() const { return api_; }
    void show();
    void setSize(int width, int height); // in points
    // Borderless fullscreen on the window's display; leaving it restores the windowed placement.
    void setFullscreen(bool fullscreen);
    bool fullscreen() const;
    // The windowed placement (also while fullscreen: the one it returns to).
    WindowRect placement() const;
    // Moves and sizes the window; a placement no display can show (a display was unplugged) is
    // replaced by the same size centred on the primary display, shrunk to fit.
    void setPlacement(const WindowRect& rect);
    // Text input events (InputEvent::Type::Text) are only delivered while enabled.
    void setTextInput(bool enabled);

    // For RHI backends only (surface / context creation).
    SDL_Window* sdlWindow() const { return window_; }

private:
    Window(SDL_Window* window, GraphicsApi api) : window_(window), api_(api) {}

    SDL_Window* window_ = nullptr;
    GraphicsApi api_;
    WindowRect windowed_; // placement before entering fullscreen
};

} // namespace animal::platform
