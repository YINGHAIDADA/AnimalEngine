#include <animal/core/assert.h>
#include <animal/core/log.h>
#include <animal/renderer/sprite_batch.h>
#include <animal/resources/loaders.h>
#include <animal/rhi/command_list.h>

#include <cstddef>

namespace animal {

bool SpriteBatch::init(rhi::Device& device, const std::string& shaderDirectory, rhi::Format targetFormat) {
    device_ = &device;
    vertexShader_ = loadShader(device, shaderDirectory + "sprite.vert.ashd");
    fragmentShader_ = loadShader(device, shaderDirectory + "sprite.frag.ashd");
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
    };
    pipelineDesc.colorTargets = {{targetFormat, rhi::BlendState::premultipliedAlpha()}};
    pipelineDesc.debugName = "sprite";
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
    device_->destroy(fragmentShader_);
    device_->destroy(vertexShader_);
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
    runTexture_ = {};
    runStart_ = quadCount();
}

void SpriteBatch::draw(rhi::TextureHandle texture, const RectF& dst, const RectF& uv, Color color) {
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
    if (texture != runTexture_) {
        flush();
        runTexture_ = texture;
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
    vertices_.push_back({x0, y0, u0, v0, c});
    vertices_.push_back({x1, y0, u1, v0, c});
    vertices_.push_back({x1, y1, u1, v1, c});
    vertices_.push_back({x0, y1, u0, v1, c});
}

void SpriteBatch::flush() {
    const uint32_t end = quadCount();
    const uint32_t count = end - runStart_;
    if (count > 0 && runTexture_) {
        const rhi::BindGroupHandle group = textureGroup(runTexture_);
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
    runTexture_ = {};
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
    const auto it = textureGroups_.find(texture.key());
    if (it != textureGroups_.end()) {
        device_->destroy(it->second);
        textureGroups_.erase(it);
    }
}

rhi::BindGroupHandle SpriteBatch::textureGroup(rhi::TextureHandle texture) {
    const auto it = textureGroups_.find(texture.key());
    if (it != textureGroups_.end()) {
        return it->second;
    }
    rhi::BindGroupDesc desc;
    desc.pipeline = pipeline_;
    desc.set = 1;
    desc.entries = {rhi::BindGroupEntry::textureSampler(0, texture, sampler_)};
    desc.debugName = "sprite texture";
    const rhi::BindGroupHandle group = device_->createBindGroup(desc);
    if (group) {
        textureGroups_.emplace(texture.key(), group);
    }
    return group;
}

} // namespace animal
