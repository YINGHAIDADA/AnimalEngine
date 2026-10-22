#pragma once

#include <animal/core/math.h>
#include <animal/renderer/water.h>
#include <animal/rhi/rhi.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace animal::rhi {
class CommandList;
}

namespace animal {

// Draws water over the lit scene: reads `scene` (the composite) and `light`, writes `output`
// (the canvas). One canvas row of column data says where each column's water is. ADR-0018.
class WaterPass {
public:
    struct Targets {
        rhi::TextureHandle scene;
        rhi::TextureHandle light;
        rhi::TextureHandle output;
        rhi::Extent2D extent;
    };

    struct Frame {
        std::vector<uint8_t> columns; // RGBA8 per canvas column: surface row, bottom row, style, present
        std::array<WaterSurface, kMaxWaterStyles> styles{};
        Vec3 ambient;
        int bands = 0;
        bool dither = true;
        int phase = 0;
    };

    bool init(rhi::Device& device, const std::string& shaderDirectory, const Targets& targets);
    void shutdown();
    void render(rhi::CommandList& commands, const Frame& frame);

private:
    struct Uniforms {
        float canvas[4];
        float ambient[4];
        float color[kMaxWaterStyles][4];
        float params[kMaxWaterStyles][4];
    };

    rhi::Device* device_ = nullptr;
    Targets targets_;
    rhi::ShaderHandle vertex_;
    rhi::ShaderHandle fragment_;
    rhi::PipelineHandle pipeline_;
    rhi::BufferHandle triangle_;
    rhi::BufferHandle uniforms_;
    rhi::TextureHandle columns_;
    rhi::SamplerHandle sampler_;
    rhi::BindGroupHandle settingsGroup_;
    rhi::BindGroupHandle inputGroup_;
};

} // namespace animal
