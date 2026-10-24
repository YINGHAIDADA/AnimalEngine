#pragma once

#include <animal/renderer/fluid.h>
#include <animal/rhi/rhi.h>

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace animal::rhi {
class CommandList;
}

namespace animal {

// Stable-fluids smoke on the GPU (fragment-shader passes, OpenGL 4.1 compatible), see ADR-0008.
class FluidPass {
public:
    static constexpr uint32_t kMaxSplats = kMaxFluidSplats;

    // A splat already converted to grid space: uv position, radius in uv-height units, and the
    // amount to add this step (velocity in cells/s, or premultiplied smoke color + density).
    struct GridSplat {
        Vec2 uv;
        float radius = 0.0f;
        float value[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    };

    bool init(rhi::Device& device, const std::string& shaderDirectory);
    void shutdown();

    // `mask` holds kFluidGrid.width × height bytes, row 0 at the top; non-zero = solid.
    void setObstacles(std::span<const uint8_t> mask);
    void reset() { needsClear_ = true; } // fields are cleared before the next step

    void beginFrame() { simulatedThisFrame_ = false; }
    // Records one simulation step (at most once per frame). Smoke always ends in smokeTexture().
    void simulate(rhi::CommandList& commands, float dt, std::span<const GridSplat> forces,
                  std::span<const GridSplat> smoke, const FluidSettings& settings);

    rhi::TextureHandle smokeTexture() const { return smoke_[0]; }

private:
    enum Program : size_t {
        Advect,
        Splat,
        Divergence,
        Pressure,
        Gradient,
        Curl,
        Vorticity,
        ProgramCount,
    };

    struct AdvectUniforms {
        float texel[4];
        float params[4];
    };

    struct SplatUniforms {
        float info[4];
        float shape[kMaxSplats][4];
        float value[kMaxSplats][4];
    };

    bool createTargets();
    rhi::BindGroupHandle uniformGroup(Program program, rhi::BufferHandle buffer);
    rhi::BindGroupHandle inputGroup(Program program, std::span<const rhi::TextureHandle> textures);
    void pass(rhi::CommandList& commands, Program program, rhi::BindGroupHandle uniforms,
              std::span<const rhi::TextureHandle> inputs, rhi::TextureHandle target);
    static SplatUniforms packSplats(std::span<const GridSplat> splats);

    rhi::Device* device_ = nullptr;
    std::array<rhi::ShaderHandle, ProgramCount> fragments_{};
    rhi::ShaderHandle vertex_;
    std::array<rhi::PipelineHandle, ProgramCount> pipelines_{};
    rhi::BufferHandle triangle_;
    rhi::SamplerHandle linear_;
    rhi::SamplerHandle nearest_;

    std::array<rhi::TextureHandle, 2> velocity_{};
    std::array<rhi::TextureHandle, 2> smoke_{};
    std::array<rhi::TextureHandle, 2> pressure_{};
    rhi::TextureHandle divergence_;
    rhi::TextureHandle curl_;
    rhi::TextureHandle obstacles_;
    size_t velocityIndex_ = 0;

    rhi::BufferHandle gridUniforms_;
    rhi::BufferHandle advectVelocityUniforms_;
    rhi::BufferHandle advectSmokeUniforms_;
    rhi::BufferHandle vorticityUniforms_;
    rhi::BufferHandle forceSplatUniforms_;
    rhi::BufferHandle smokeSplatUniforms_;

    std::map<std::array<uint64_t, 6>, rhi::BindGroupHandle> groups_;
    bool needsClear_ = true;
    bool simulatedThisFrame_ = false;
};

} // namespace animal
