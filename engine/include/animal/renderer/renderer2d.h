#pragma once

#include <animal/core/math.h>
#include <animal/renderer/camera2d.h>
#include <animal/renderer/fluid.h>
#include <animal/renderer/gpu_particles.h>
#include <animal/renderer/lighting.h>
#include <animal/renderer/pixel_canvas.h>
#include <animal/renderer/post.h>
#include <animal/renderer/sprite_batch.h>
#include <animal/renderer/water.h>
#include <animal/resources/loaders.h>
#include <animal/rhi/rhi.h>

#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace animal {

class DebugFont;
class FluidPass;
class LightingPass;
class PostPass;
class WaterPass;
class GpuParticlePass;
struct WaterFrame;

// Frame structure (ADR-0007):
//   beginFrame
//   beginWorld .. drawSprite / drawGlow / addLight / addFluidSplat .. endWorld
//                material pass -> fluid step -> lights -> composite (+ smoke) -> glow -> bloom + grade
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
    // Recreates every GPU resource from the shader files on disk (hot reload of a shader whose
    // resource bindings changed, ADR-0028). Built aside first: on failure nothing changes. Settings
    // are kept; smoke, GPU particles, occluders, fluid obstacles and GPU emitters start over and must
    // be set again. Between frames only.
    bool rebuild();

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
    void addFluidSplat(const FluidSplat& splat);
    void addWater(const WaterSurface& surface); // ADR-0018
    void endWorld();

    // Smoke simulation (ADR-0008). Obstacles: kFluidGrid bytes, row 0 at the top, non-zero =
    // solid. The time step is the simulation time to advance this frame (0 = paused).
    void setFluidObstacles(std::span<const uint8_t> mask);
    // Shadows (ADR-0012). Occluders: one byte per canvas pixel (kCanvasExtent), row 0 at the top,
    // non-zero = blocks light. Canvas space, so re-upload when the camera moves.
    void setLightOccluders(std::span<const uint8_t> mask);
    void setFluidTimeStep(float seconds) { fluidTimeStep_ = seconds; }
    void resetFluid(); // clears velocity and smoke (e.g. on room change); the simulation sleeps until smoke is added

    // GPU particles (ADR-0020). Emitters are set per room (their particles start over); the step
    // (simulation time and wind) must be set before beginWorld each frame.
    void setGpuEmitters(std::span<const GpuEmitter> emitters);
    void setGpuParticleStep(float seconds, Vec2 wind) {
        gpuParticleStep_ = seconds;
        gpuParticleWind_ = wind;
    }
    uint32_t gpuParticleCount() const; // slots in use

    // The finished canvas (after bloom and grading, before the screen pass): what regression
    // captures compare (--canvas-screenshot).
    rhi::TextureHandle finalCanvas() const { return final_; }
    // The simulation sleeps (costs nothing) once no smoke has been added for long enough that what
    // is left has faded out; adding smoke wakes it.
    bool fluidSleeping() const { return fluidQuietSeconds_ > fluidFadeSeconds(); }
    FluidSettings& fluid() { return fluidSettings_; }
    PostSettings& post() { return postSettings_; }

    void beginWorldOverlay(const Camera2D& camera);
    void endWorldOverlay();

    void beginScreen(Color letterboxColor);
    // `size` multiplies the HUD text size (titles).
    void drawText(Vec2 position, std::string_view text, Color color, uint32_t size = 1);
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
    float textWidth(std::string_view text, uint32_t size = 1) const;
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

    void swapResources(Renderer2D& other);

    rhi::Device* device_ = nullptr;
    std::string dataDirectory_;
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
    std::unique_ptr<FluidPass> fluidPass_;
    std::unique_ptr<PostPass> postPass_;
    std::unique_ptr<WaterPass> waterPass_;
    std::unique_ptr<GpuParticlePass> gpuParticles_;
    float gpuParticleStep_ = 0.0f;
    Vec2 gpuParticleWind_;
    std::unique_ptr<WaterFrame> water_; // this frame's water bodies
    PostSettings postSettings_;
    FluidSettings fluidSettings_;
    std::vector<FluidSplat> fluidSplats_;
    float fluidTimeStep_ = 0.0f;
    float fluidQuietSeconds_ = std::numeric_limits<float>::infinity(); // simulated time since smoke was last added
    float fluidFadeSeconds() const;
    Vec2 worldOrigin_; // world position of the canvas top-left in the current world pass
    LightingSettings lightingSettings_;
    std::vector<PointLight> lights_;
    Mat4 worldViewProjection_;

    rhi::TextureHandle canvas_; // lit scene (lighting, smoke, glow)
    rhi::TextureHandle final_;  // after bloom and grading; shown on screen, overlays draw here
    rhi::TextureHandle albedo_;
    rhi::TextureHandle normal_;
    rhi::TextureHandle emissive_;
    rhi::TextureHandle light_;
    rhi::TextureHandle rimLight_;
    rhi::TextureHandle lit_; // composite output when water is drawn over it
    rhi::TextureHandle flags_; // material flags: r = rim strength
    rhi::TextureHandle flatNormal_;
    rhi::TextureHandle noEmission_;
    PresentLayout presentLayout_;
};

} // namespace animal
