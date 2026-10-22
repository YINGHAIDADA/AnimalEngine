#pragma once

#include <animal/core/math.h>
#include <animal/renderer/gpu_particles.h>
#include <animal/rhi/rhi.h>

#include <array>
#include <span>
#include <string>

namespace animal::rhi {
class CommandList;
}

namespace animal {

// Fragment-shader particle simulation (ADR-0020): particle state lives in two ping-ponged
// pairs of RGBA32F textures (position + velocity, age + lifetime + seed + emitter), one texel
// per slot; update() advances them in one pass, draw() renders one instanced quad per slot.
class GpuParticlePass {
public:
    static constexpr uint32_t kStateWidth = 128;

    struct Formats {
        std::array<rhi::Format, 4> material; // the material pass attachments
        rhi::Format glow;                    // the glow pass attachment
    };

    bool init(rhi::Device& device, const std::string& shaderDirectory, rhi::TextureHandle occluders,
              rhi::Extent2D canvas, const Formats& formats);
    void shutdown();

    // Replaces the emitters; their particles start over (staggered over one lifetime).
    void setEmitters(std::span<const GpuEmitter> emitters);
    bool active() const { return slotsUsed_ > 0; }
    bool hasStyle(GpuEmitter::Style style) const;
    uint32_t slotsUsed() const { return slotsUsed_; }

    // Outside a render pass. dt 0 (and no pending restart) leaves the particles as they are.
    void update(rhi::CommandList& commands, float dt, Vec2 wind, Vec2 canvasOrigin);
    // Inside the material pass (Lit) or the glow pass (Glow).
    void draw(rhi::CommandList& commands, const Mat4& viewProjection, GpuEmitter::Style style);

private:
    struct EmitterData {
        float area[4];
        float velocity[4];
        float motion[4];
        float life[4];
        float startColor[4];
        float endColor[4];
        float slots[4];
    };
    struct SimulationUniforms {
        float step[4];
        float canvas[4];
        float wind[4];
        EmitterData emitters[kMaxGpuEmitters];
    };
    struct DrawUniforms {
        Mat4 viewProjection;
        float params[4];
        EmitterData emitters[kMaxGpuEmitters];
    };

    rhi::Device* device_ = nullptr;
    rhi::Extent2D canvas_;
    rhi::ShaderHandle fullscreenVertex_, updateFragment_, drawVertex_, litFragment_, glowFragment_;
    rhi::PipelineHandle updatePipeline_, litPipeline_, glowPipeline_;
    rhi::BufferHandle triangle_, corners_, simulationUniforms_;
    std::array<rhi::BufferHandle, 2> drawUniforms_{}; // per style
    std::array<rhi::TextureHandle, 2> stateA_{}, stateB_{};
    rhi::SamplerHandle sampler_;
    rhi::BindGroupHandle simulationGroup_;
    std::array<rhi::BindGroupHandle, 2> updateInputs_{}; // reads side i
    std::array<rhi::BindGroupHandle, 2> drawSettings_{}; // per style
    std::array<rhi::BindGroupHandle, 2> drawInputs_{};   // reads side i

    std::array<EmitterData, kMaxGpuEmitters> emitters_{};
    std::array<GpuEmitter::Style, kMaxGpuEmitters> styles_{};
    size_t emitterCount_ = 0;
    uint32_t slotsUsed_ = 0;
    size_t current_ = 0; // side holding the latest state
    bool restart_ = false;
    uint32_t frame_ = 0;
};

} // namespace animal
