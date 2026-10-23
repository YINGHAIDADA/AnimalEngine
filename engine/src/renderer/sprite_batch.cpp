#include <animal/core/assert.h>
#include <animal/core/log.h>
#include <animal/renderer/sprite_batch.h>
#include <animal/resources/loaders.h>
#include <animal/rhi/command_list.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace animal {

bool SpriteBatch::init(rhi::Device& device, const std::string& shaderDirectory, const SpriteBatchDesc& desc) {
    device_ = &device;
    texturesPerSprite_ = desc.texturesPerSprite;
    vertexShader_ = loadShader(device, shaderDirectory + desc.vertexShader);
    fragmentShader_ = loadShader(device, shaderDirectory + desc.fragmentShader);
    if (!vertexShader_ || !fragmentShader_) {
        return false;
    }

    rhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader_;
    pipelineDesc.fragmentShader = fragmentShader_;
    pipelineDesc.vertexBuffers = {{sizeof(Vertex), rhi::VertexStepMode::Vertex}};
    pipelineDesc.vertexAttributes = {
        {0, 0, rhi::VertexFormat::Float2, offsetof(Vertex, x)},
        {1, 0, rhi::VertexFormat::Float2, offsetof(Vertex, u)},
        {2, 0, rhi::VertexFormat::UNorm8x4, offsetof(Vertex, color)},
        {3, 0, rhi::VertexFormat::UNorm8x4, offsetof(Vertex, params)},
    };
    pipelineDesc.colorTargets = desc.colorTargets;
    pipelineDesc.debugName = desc.debugName;
    pipeline_ = device.createGraphicsPipeline(pipelineDesc);
    if (!pipeline_) {
        return false;
    }

    vertexBuffer_ = device.createBuffer(
        {uint64_t{kMaxQuadsPerFrame} * 4 * sizeof(Vertex), rhi::BufferUsage::Vertex, "sprite vertices"});

    std::vector<uint16_t> indices(size_t{kMaxQuadsPerFrame} * 6);
    for (uint32_t quad = 0; quad < kMaxQuadsPerFrame; ++quad) {
        const auto base = static_cast<uint16_t>(quad * 4);
        uint16_t* out = &indices[size_t{quad} * 6];
        out[0] = base;
        out[1] = static_cast<uint16_t>(base + 1);
        out[2] = static_cast<uint16_t>(base + 2);
        out[3] = base;
        out[4] = static_cast<uint16_t>(base + 2);
        out[5] = static_cast<uint16_t>(base + 3);
    }
    indexBuffer_ = device.createBuffer(
        {indices.size() * sizeof(uint16_t), rhi::BufferUsage::Index, "sprite indices"}, indices.data());

    sampler_ = device.createSampler({});

    for (uint32_t i = 0; i < kMaxViewsPerFrame; ++i) {
        View view;
        view.uniforms = device.createBuffer({sizeof(Mat4), rhi::BufferUsage::Uniform, "sprite view"});
        rhi::BindGroupDesc groupDesc;
        groupDesc.pipeline = pipeline_;
        groupDesc.set = 0;
        groupDesc.entries = {rhi::BindGroupEntry::uniformBuffer(0, view.uniforms)};
        groupDesc.debugName = "sprite view";
        view.group = device.createBindGroup(groupDesc);
        if (!view.uniforms || !view.group) {
            return false;
        }
        views_.push_back(view);
    }

    vertices_.reserve(size_t{kMaxQuadsPerFrame} * 4);
    return vertexBuffer_ && indexBuffer_ && sampler_;
}

void SpriteBatch::shutdown() {
    if (!device_) {
        return;
    }
    for (auto& [key, group] : textureGroups_) {
        device_->destroy(group);
    }
    textureGroups_.clear();
    for (const View& view : views_) {
        device_->destroy(view.group);
        device_->destroy(view.uniforms);
    }
    views_.clear();
    device_->destroy(sampler_);
    device_->destroy(indexBuffer_);
    device_->destroy(vertexBuffer_);
    device_->destroy(pipeline_);
    destroyShader(*device_, fragmentShader_);
    destroyShader(*device_, vertexShader_);
    device_ = nullptr;
}

void SpriteBatch::beginFrame() {
    vertices_.clear();
    viewsUsed_ = 0;
    overflowReported_ = false;
    stats_ = {};
}

void SpriteBatch::begin(rhi::CommandList& commands, const Mat4& viewProjection) {
    ANIMAL_ASSERT(commands_ == nullptr);
    if (viewsUsed_ >= views_.size()) {
        log::error("SpriteBatch: more than {} views in one frame; batch skipped", kMaxViewsPerFrame);
        return;
    }
    const View& view = views_[viewsUsed_++];
    device_->writeBuffer(view.uniforms, 0, &viewProjection, sizeof(Mat4));

    commands_ = &commands;
    commands.setPipeline(pipeline_);
    commands.setVertexBuffer(0, vertexBuffer_);
    commands.setIndexBuffer(indexBuffer_, rhi::IndexType::UInt16);
    commands.setBindGroup(0, view.group);
    runTextures_ = {};
    runStart_ = quadCount();
}

void SpriteBatch::draw(const SpriteTextures& textures, const RectF& dst, const RectF& uv, Color color, float rim) {
    if (!commands_) {
        return;
    }
    if (quadCount() >= kMaxQuadsPerFrame) {
        if (!overflowReported_) {
            log::warn("SpriteBatch: more than {} quads this frame; extra quads dropped", kMaxQuadsPerFrame);
            overflowReported_ = true;
        }
        return;
    }
    if (textures != runTextures_) {
        flush();
        runTextures_ = textures;
    }

    const Color c = color.premultiplied();
    const float x0 = dst.x;
    const float y0 = dst.y;
    const float x1 = dst.x + dst.width;
    const float y1 = dst.y + dst.height;
    const float u0 = uv.x;
    const float v0 = uv.y;
    const float u1 = uv.x + uv.width;
    const float v1 = uv.y + uv.height;
    const auto r = static_cast<uint8_t>(std::lround(std::clamp(rim, 0.0f, 1.0f) * 255.0f));
    vertices_.push_back({x0, y0, u0, v0, c, {r, 0, 0, 0}});
    vertices_.push_back({x1, y0, u1, v0, c, {r, 0, 0, 0}});
    vertices_.push_back({x1, y1, u1, v1, c, {r, 0, 0, 0}});
    vertices_.push_back({x0, y1, u0, v1, c, {r, 0, 0, 0}});
}

void SpriteBatch::flush() {
    const uint32_t end = quadCount();
    const uint32_t count = end - runStart_;
    if (count > 0 && runTextures_[0]) {
        const rhi::BindGroupHandle group = textureGroup(runTextures_);
        if (group) {
            commands_->setBindGroup(1, group);
            commands_->drawIndexed(count * 6, 1, runStart_ * 6, 0);
            ++stats_.drawCalls;
        }
    }
    runStart_ = end;
}

void SpriteBatch::end() {
    if (!commands_) {
        return;
    }
    flush();
    commands_ = nullptr;
    runTextures_ = {};
}

void SpriteBatch::endFrame() {
    ANIMAL_ASSERT(commands_ == nullptr);
    if (!vertices_.empty()) {
        device_->writeBuffer(vertexBuffer_, 0, vertices_.data(), vertices_.size() * sizeof(Vertex));
    }
    stats_.quads = quadCount();
    lastFrameStats_ = stats_;
}

void SpriteBatch::forgetTexture(rhi::TextureHandle texture) {
    for (auto it = textureGroups_.begin(); it != textureGroups_.end();) {
        if (std::find(it->first.begin(), it->first.end(), texture.key()) != it->first.end()) {
            device_->destroy(it->second);
            it = textureGroups_.erase(it);
        } else {
            ++it;
        }
    }
}

SpriteBatch::TextureKey SpriteBatch::keyOf(const SpriteTextures& textures) {
    TextureKey key{};
    for (size_t i = 0; i < textures.size(); ++i) {
        key[i] = textures[i].key();
    }
    return key;
}

rhi::BindGroupHandle SpriteBatch::textureGroup(const SpriteTextures& textures) {
    const TextureKey key = keyOf(textures);
    if (const auto it = textureGroups_.find(key); it != textureGroups_.end()) {
        return it->second;
    }
    rhi::BindGroupDesc desc;
    desc.pipeline = pipeline_;
    desc.set = 1;
    for (uint32_t i = 0; i < texturesPerSprite_; ++i) {
        desc.entries.push_back(rhi::BindGroupEntry::textureSampler(i, textures[i], sampler_));
    }
    desc.debugName = "sprite textures";
    const rhi::BindGroupHandle group = device_->createBindGroup(desc);
    if (group) {
        textureGroups_.emplace(key, group);
    }
    return group;
}

} // namespace animal
