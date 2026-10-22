#pragma once

#include <animal/core/math.h>
#include <animal/renderer/lighting.h>
#include <animal/rhi/rhi.h>

#include <span>
#include <string>
#include <vector>

namespace animal::rhi {
class CommandList;
}

namespace animal {

// Light accumulation (one additive quad per light, RGBA16F) and the composite into the canvas.
// All buffers share the canvas size; see docs/ADR/ADR-0007.
class LightingPass {
public:
    static constexpr uint32_t kMaxLights = 1024;

    struct Targets {
        rhi::TextureHandle albedo;
        rhi::TextureHandle normal;
        rhi::TextureHandle emissive;
        rhi::TextureHandle light;
        rhi::TextureHandle smoke; // fluid smoke field (any size; sampled with linear filtering)
        rhi::Extent2D extent;
    };

    struct SmokeLook {
        Vec3 tint{1.0f, 1.0f, 1.0f};
        float opacity = 0.0f;
    };

    bool init(rhi::Device& device, const std::string& shaderDirectory, const Targets& targets,
              rhi::Format outputFormat);
    void shutdown();

    // Canvas-sized mask, row 0 at the top, non-zero = blocks light.
    void setOccluders(std::span<const uint8_t> mask);

    void beginFrame();
    // Records the light pass into targets.light, then composites into `output`. Once per frame.
    // `canvasOrigin` is the world position of the canvas' top-left pixel (Camera2D::topLeft).
    void render(rhi::CommandList& commands, const Mat4& viewProjection, Vec2 canvasOrigin,
                std::span<const PointLight> lights, const LightingSettings& settings, const SmokeLook& smoke,
                rhi::TextureHandle output);
    void endFrame();

    uint32_t lightCount() const { return lastFrameLights_; }

private:
    struct LightVertex {
        float x, y;
        float centerX, centerY;
        float r, g, b, a;
        float radius, height, shadows, unused;
        float texelX, texelY; // canvas pixel of the light (whole numbers): exact shadow rays on every backend
        float spotX, spotY, spotOuter, spotInner; // cone direction and cosines of the edge; outer < -1: omni
    };

    struct LightView {
        Mat4 viewProjection;
        float params[4]; // xy: canvas size, z: shadows enabled, w: penetration
    };

    struct CompositeUniforms {
        float ambient[4];
        float params[4];
        float canvasSize[4];
        float smoke[4];
    };

    bool initLightPipeline(const std::string& shaderDirectory);
    bool initCompositePipeline(const std::string& shaderDirectory, rhi::Format outputFormat);

    rhi::Device* device_ = nullptr;
    Targets targets_;
    rhi::SamplerHandle sampler_;
    rhi::SamplerHandle linearSampler_;

    rhi::ShaderHandle lightVertex_;
    rhi::ShaderHandle lightFragment_;
    rhi::PipelineHandle lightPipeline_;
    rhi::BufferHandle lightVertices_;
    rhi::BufferHandle lightIndices_;
    rhi::BufferHandle viewUniforms_;
    rhi::TextureHandle occluders_;
    rhi::BindGroupHandle lightViewGroup_;
    rhi::BindGroupHandle lightInputGroup_;

    rhi::ShaderHandle compositeVertex_;
    rhi::ShaderHandle compositeFragment_;
    rhi::PipelineHandle compositePipeline_;
    rhi::BufferHandle triangle_;
    rhi::BufferHandle compositeUniforms_;
    rhi::BindGroupHandle compositeSettingsGroup_;
    rhi::BindGroupHandle compositeInputGroup_;

    std::vector<LightVertex> vertices_;
    bool renderedThisFrame_ = false;
    uint32_t lastFrameLights_ = 0;
};

} // namespace animal
