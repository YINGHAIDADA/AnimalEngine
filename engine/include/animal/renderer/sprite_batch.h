#pragma once

#include <animal/core/math.h>
#include <animal/rhi/rhi.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace animal::rhi {
class CommandList;
}

namespace animal {

struct SpriteBatchStats {
    uint32_t quads = 0;
    uint32_t drawCalls = 0;
};

// Collects textured quads for a whole frame. Draws are recorded into the command list as
// they are batched; vertex data is uploaded once in endFrame(), which must run before
// Device::submit(). Consecutive quads sharing a texture become one draw call.
class SpriteBatch {
public:
    static constexpr uint32_t kMaxQuadsPerFrame = 16384; // 65536 vertices, addressable with uint16 indices
    static constexpr uint32_t kMaxViewsPerFrame = 8;

    bool init(rhi::Device& device, const std::string& shaderDirectory, rhi::Format targetFormat);
    void shutdown();

    void beginFrame();
    void begin(rhi::CommandList& commands, const Mat4& viewProjection);
    // `color` is straight alpha. `uv` is in normalized texture coordinates, v = 0 at the top.
    void draw(rhi::TextureHandle texture, const RectF& dst, const RectF& uv, Color color = Color::white());
    void end();
    void endFrame();

    void forgetTexture(rhi::TextureHandle texture);
    // Totals of the last completed frame.
    const SpriteBatchStats& stats() const { return lastFrameStats_; }

private:
    struct Vertex {
        float x, y;
        float u, v;
        Color color;
    };

    struct View {
        rhi::BufferHandle uniforms;
        rhi::BindGroupHandle group;
    };

    void flush();
    uint32_t quadCount() const { return static_cast<uint32_t>(vertices_.size() / 4); }
    rhi::BindGroupHandle textureGroup(rhi::TextureHandle texture);

    rhi::Device* device_ = nullptr;
    rhi::ShaderHandle vertexShader_;
    rhi::ShaderHandle fragmentShader_;
    rhi::PipelineHandle pipeline_;
    rhi::BufferHandle vertexBuffer_;
    rhi::BufferHandle indexBuffer_;
    rhi::SamplerHandle sampler_;
    std::vector<View> views_;
    std::unordered_map<uint64_t, rhi::BindGroupHandle> textureGroups_;

    std::vector<Vertex> vertices_;
    rhi::CommandList* commands_ = nullptr;
    rhi::TextureHandle runTexture_;
    uint32_t runStart_ = 0;
    uint32_t viewsUsed_ = 0;
    bool overflowReported_ = false;
    SpriteBatchStats stats_;
    SpriteBatchStats lastFrameStats_;
};

} // namespace animal
