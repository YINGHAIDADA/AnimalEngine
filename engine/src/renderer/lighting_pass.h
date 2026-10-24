#pragma once

#include <animal/core/math.h>
#include <animal/renderer/lighting.h>
#include <animal/rhi/rhi.h>

#include <array>
#include <span>
#include <string>
#include <vector>

namespace animal::rhi {
class CommandList;
}

namespace animal {

// Light accumulation (one additive quad per light, RGBA16F), indirect light (the light averaged to a
// quarter of the resolution and blurred wide) and the composite into the canvas, with ambient
// occlusion computed from the occluders. See docs/ADR/ADR-0007 and ADR-0039.
class LightingPass {
public:
    static constexpr uint32_t kMaxLights = 1024;

    struct Targets {
        rhi::TextureHandle albedo;
        rhi::TextureHandle normal;
        rhi::TextureHandle emissive;
        rhi::TextureHandle flags; // material flags: r = rim strength
        rhi::TextureHandle light;
        rhi::TextureHandle rim;   // rim light accumulation, same format as `light`
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

    // Canvas-sized mask, row 0 at the top, non-zero = blocks light. Also derives the ambient
    // occlusion of the open pixels from it.
    void setOccluders(std::span<const uint8_t> mask);

    void beginFrame();
    // Records the light pass into targets.light, then composites into `output`. Once per frame.
    // `canvasOrigin` is the world position of the canvas' top-left pixel (Camera2D::topLeft).
    void render(rhi::CommandList& commands, const Mat4& viewProjection, Vec2 canvasOrigin,
                std::span<const PointLight> lights, const LightingSettings& settings, const SmokeLook& smoke,
                rhi::TextureHandle output);
    void endFrame();

    uint32_t lightCount() const { return lastFrameLights_; }
    rhi::TextureHandle occluders() const { return occluders_; }

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
        float rim[4];    // x: rim intensity, y: back-wall normal flattening
    };

    struct CompositeUniforms {
        float ambient[4];
        float params[4];
        float canvasSize[4];
        float smoke[4];
        float rimAmbient[4];
        float depth[4]; // x: back-wall light, y: depth fog, z: ambient occlusion, w: ambient gradient
        float fog[4];   // rgb: depth fog colour, a: indirect light strength
    };

    bool initLightPipeline(const std::string& shaderDirectory);
    bool initIndirect(const std::string& shaderDirectory);
    void fullscreen(rhi::CommandList& commands, rhi::PipelineHandle pipeline, rhi::BindGroupHandle settings,
                    rhi::BindGroupHandle inputs, rhi::TextureHandle target);
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

    rhi::TextureHandle ambientOcclusion_; // R8, canvas size

    rhi::ShaderHandle indirectVertex_;
    rhi::ShaderHandle downsampleFragment_;
    rhi::ShaderHandle blurFragment_;
    rhi::PipelineHandle downsamplePipeline_;
    rhi::PipelineHandle blurPipeline_;
    std::array<rhi::TextureHandle, 2> indirect_{}; // quarter of the canvas, RGBA16F; [0] holds the result
    rhi::BufferHandle downsampleUniforms_;
    std::array<rhi::BufferHandle, 4> blurUniforms_{}; // wide horizontal / vertical, then narrow
    rhi::BindGroupHandle downsampleSettings_;
    rhi::BindGroupHandle downsampleInputs_;
    std::array<rhi::BindGroupHandle, 4> blurSettings_{};
    std::array<rhi::BindGroupHandle, 2> blurInputs_{}; // reading indirect_[0], indirect_[1]

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
