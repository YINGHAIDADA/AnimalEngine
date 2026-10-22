#pragma once

#include <animal/core/math.h>
#include <animal/rhi/rhi.h>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace animal::rhi {
class CommandList;
}

namespace animal {

struct SpriteBatchStats {
    uint32_t quads = 0;
    uint32_t drawCalls = 0;
};

inline constexpr uint32_t kMaxSpriteTextures = 3;
using SpriteTextures = std::array<rhi::TextureHandle, kMaxSpriteTextures>;

// Shaders and targets of one batch. Textures are bound at set 1, bindings 0..texturesPerSprite-1.
struct SpriteBatchDesc {
    std::string vertexShader = "sprite.vert.ashd";
    std::string fragmentShader = "sprite.frag.ashd";
    std::vector<rhi::ColorTargetDesc> colorTargets;
    uint32_t texturesPerSprite = 1;
    const char* debugName = "sprite";
};

// Collects textured quads for a whole frame. Draws are recorded into the command list as
// they are batched; vertex data is uploaded once in endFrame(), which must run before
// Device::submit(). Consecutive quads sharing the same textures become one draw call.
class SpriteBatch {
public:
    static constexpr uint32_t kMaxQuadsPerFrame = 16384; // 65536 vertices, addressable with uint16 indices
    static constexpr uint32_t kMaxViewsPerFrame = 8;

    bool init(rhi::Device& device, const std::string& shaderDirectory, const SpriteBatchDesc& desc);
    void shutdown();

    void beginFrame();
    void begin(rhi::CommandList& commands, const Mat4& viewProjection);
    // `color` is straight alpha. `uv` is in normalized texture coordinates, v = 0 at the top;
    // a negative width mirrors horizontally. `rim` (0..1) reaches the shader as vParams.x.
    void draw(const SpriteTextures& textures, const RectF& dst, const RectF& uv, Color color = Color::white(),
              float rim = 0.0f);
    void draw(rhi::TextureHandle texture, const RectF& dst, const RectF& uv, Color color = Color::white()) {
        draw(SpriteTextures{texture}, dst, uv, color);
    }
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
        uint8_t params[4]; // x: rim strength
    };

    struct View {
        rhi::BufferHandle uniforms;
        rhi::BindGroupHandle group;
    };

    using TextureKey = std::array<uint64_t, kMaxSpriteTextures>;

    void flush();
    uint32_t quadCount() const { return static_cast<uint32_t>(vertices_.size() / 4); }
    rhi::BindGroupHandle textureGroup(const SpriteTextures& textures);
    static TextureKey keyOf(const SpriteTextures& textures);

    rhi::Device* device_ = nullptr;
    uint32_t texturesPerSprite_ = 1;
    rhi::ShaderHandle vertexShader_;
    rhi::ShaderHandle fragmentShader_;
    rhi::PipelineHandle pipeline_;
    rhi::BufferHandle vertexBuffer_;
    rhi::BufferHandle indexBuffer_;
    rhi::SamplerHandle sampler_;
    std::vector<View> views_;
    std::map<TextureKey, rhi::BindGroupHandle> textureGroups_;

    std::vector<Vertex> vertices_;
    rhi::CommandList* commands_ = nullptr;
    SpriteTextures runTextures_{};
    uint32_t runStart_ = 0;
    uint32_t viewsUsed_ = 0;
    bool overflowReported_ = false;
    SpriteBatchStats stats_;
    SpriteBatchStats lastFrameStats_;
};

} // namespace animal
