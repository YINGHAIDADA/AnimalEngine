#pragma once

#include <animal/core/math.h>
#include <animal/renderer/camera2d.h>
#include <animal/renderer/pixel_canvas.h>
#include <animal/renderer/sprite_batch.h>
#include <animal/resources/loaders.h>
#include <animal/rhi/rhi.h>

#include <memory>
#include <string>
#include <string_view>

namespace animal {

class DebugFont;

// Frame structure:
//   beginFrame -> beginWorld .. drawSprite .. endWorld   (into the 320x180 canvas)
//              -> beginScreen .. drawText / fillRect .. endScreen   (window pixels)
//              -> endFrame (uploads batched vertices; call before Device::submit)
class Renderer2D {
public:
    static constexpr rhi::Extent2D kCanvasExtent{320, 180};

    Renderer2D();
    ~Renderer2D();
    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    bool init(rhi::Device& device, const std::string& dataDirectory);
    void shutdown();

    void beginFrame(rhi::CommandList& commands);

    void beginWorld(const Camera2D& camera, Color clearColor);
    // Draws `source` (texture pixels) with its top-left at `position` (world pixels, snapped to
    // whole pixels), optionally mirrored horizontally.
    void drawSprite(const LoadedTexture& texture, Vec2 position, const RectF& source, Color tint = Color::white(),
                    bool flipX = false);
    void endWorld();

    void beginScreen(Color letterboxColor);
    void drawText(Vec2 position, std::string_view text, Color color);
    // Solid rectangle in the current pass's coordinates (world pixels inside beginWorld, window
    // pixels inside beginScreen).
    void fillRect(const RectF& rect, Color color);
    void strokeRect(const RectF& rect, Color color, float thickness = 1.0f);
    void endScreen();

    void endFrame();

    const PresentLayout& presentLayout() const { return presentLayout_; }
    uint32_t overlayScale() const;
    float textLineHeight() const;
    const SpriteBatchStats& stats() const { return batch_.stats(); }

private:
    rhi::Device* device_ = nullptr;
    rhi::CommandList* commands_ = nullptr;
    SpriteBatch batch_;
    std::unique_ptr<DebugFont> font_;
    rhi::TextureHandle canvas_;
    PresentLayout presentLayout_;
};

} // namespace animal
