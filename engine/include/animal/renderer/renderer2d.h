#pragma once

#include <animal/core/math.h>
#include <animal/renderer/camera2d.h>
#include <animal/renderer/lighting.h>
#include <animal/renderer/pixel_canvas.h>
#include <animal/renderer/sprite_batch.h>
#include <animal/resources/loaders.h>
#include <animal/rhi/rhi.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace animal {

class DebugFont;
class LightingPass;

// Frame structure (ADR-0007):
//   beginFrame
//   beginWorld .. drawSprite / drawGlow / addLight .. endWorld
//                                                       material pass -> lights -> composite -> glow
//   [beginWorldOverlay .. fillRect / strokeRect .. endWorldOverlay]   unlit, over the lit canvas
//   beginScreen .. drawText / fillRect .. endScreen     canvas scaled into the window + overlays
//   endFrame                                            uploads batched data; call before Device::submit
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
    void drawSprite(const SpriteMaterial& material, Vec2 position, const RectF& source,
                    Color tint = Color::white(), bool flipX = false);
    void drawSprite(const LoadedTexture& texture, Vec2 position, const RectF& source, Color tint = Color::white(),
                    bool flipX = false);
    // Unlit additive pixel (fireflies, embers) in world pixels; drawn over the lit result.
    void drawGlow(Vec2 position, Color color, float size = 1.0f);
    void addLight(const PointLight& light);
    void endWorld();

    void beginWorldOverlay(const Camera2D& camera);
    void endWorldOverlay();

    void beginScreen(Color letterboxColor);
    void drawText(Vec2 position, std::string_view text, Color color);
    void endScreen();

    // Solid rectangle in the current pass's coordinates (world pixels in the world passes,
    // window pixels in the screen pass).
    void fillRect(const RectF& rect, Color color);
    void strokeRect(const RectF& rect, Color color, float thickness = 1.0f);

    void endFrame();

    LightingSettings& lighting() { return lightingSettings_; }
    const PresentLayout& presentLayout() const { return presentLayout_; }
    uint32_t overlayScale() const;
    float textLineHeight() const;
    // Totals of the last completed frame.
    SpriteBatchStats stats() const;
    uint32_t lightCount() const;

private:
    enum class Phase {
        Idle,
        World,
        WorldOverlay,
        Screen,
    };

    SpriteBatch& activeBatch() { return phase_ == Phase::World ? litBatch_ : batch_; }
    rhi::TextureHandle createTarget(rhi::Format format, const char* name);
    rhi::TextureHandle createSolidTexture(Color color, const char* name);

    rhi::Device* device_ = nullptr;
    rhi::CommandList* commands_ = nullptr;
    Phase phase_ = Phase::Idle;

    SpriteBatch batch_;    // unlit: overlays and the screen pass
    SpriteBatch litBatch_; // material pass into the G-buffer
    SpriteBatch glowBatch_; // additive glow over the composited canvas

    struct GlowQuad {
        RectF rect;
        Color color;
    };
    std::vector<GlowQuad> glow_;
    std::unique_ptr<DebugFont> font_;
    std::unique_ptr<LightingPass> lightingPass_;
    LightingSettings lightingSettings_;
    std::vector<PointLight> lights_;
    Mat4 worldViewProjection_;

    rhi::TextureHandle canvas_;
    rhi::TextureHandle albedo_;
    rhi::TextureHandle normal_;
    rhi::TextureHandle emissive_;
    rhi::TextureHandle light_;
    rhi::TextureHandle flatNormal_;
    rhi::TextureHandle noEmission_;
    PresentLayout presentLayout_;
};

} // namespace animal
