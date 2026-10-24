#include "lighting_pass.h"

#include <animal/core/assert.h>
#include <animal/core/log.h>
#include <animal/resources/loaders.h>
#include <animal/rhi/command_list.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace animal {

namespace {

constexpr rhi::BlendState kAdditive{true,
                                    rhi::BlendFactor::One,
                                    rhi::BlendFactor::One,
                                    rhi::BlendOp::Add,
                                    rhi::BlendFactor::One,
                                    rhi::BlendFactor::One,
                                    rhi::BlendOp::Add};

} // namespace

bool LightingPass::init(rhi::Device& device, const std::string& shaderDirectory, const Targets& targets,
                        rhi::Format outputFormat) {
    device_ = &device;
    targets_ = targets;
    sampler_ = device.createSampler({});
    linearSampler_ = device.createSampler({rhi::Filter::Linear, rhi::Filter::Linear});
    return sampler_ && linearSampler_ && initLightPipeline(shaderDirectory) && initIndirect(shaderDirectory) &&
           initCompositePipeline(shaderDirectory, outputFormat);
}

bool LightingPass::initLightPipeline(const std::string& shaderDirectory) {
    lightVertex_ = loadShader(*device_, shaderDirectory + "light.vert.ashd");
    lightFragment_ = loadShader(*device_, shaderDirectory + "light.frag.ashd");
    if (!lightVertex_ || !lightFragment_) {
        return false;
    }

    rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = lightVertex_;
    desc.fragmentShader = lightFragment_;
    desc.vertexBuffers = {{sizeof(LightVertex), rhi::VertexStepMode::Vertex}};
    desc.vertexAttributes = {
        {0, 0, rhi::VertexFormat::Float2, offsetof(LightVertex, x)},
        {1, 0, rhi::VertexFormat::Float2, offsetof(LightVertex, centerX)},
        {2, 0, rhi::VertexFormat::Float4, offsetof(LightVertex, r)},
        {3, 0, rhi::VertexFormat::Float4, offsetof(LightVertex, radius)},
        {4, 0, rhi::VertexFormat::Float2, offsetof(LightVertex, texelX)},
        {5, 0, rhi::VertexFormat::Float4, offsetof(LightVertex, spotX)},
    };
    desc.colorTargets = {{rhi::Format::RGBA16_Float, kAdditive}, {rhi::Format::RGBA16_Float, kAdditive}};
    desc.debugName = "light";
    lightPipeline_ = device_->createGraphicsPipeline(desc);
    if (!lightPipeline_) {
        return false;
    }

    lightVertices_ = device_->createBuffer(
        {uint64_t{kMaxLights} * 4 * sizeof(LightVertex), rhi::BufferUsage::Vertex, "light vertices"});
    std::vector<uint16_t> indices(size_t{kMaxLights} * 6);
    for (uint32_t i = 0; i < kMaxLights; ++i) {
        const auto base = static_cast<uint16_t>(i * 4);
        const uint16_t quad[6] = {base, static_cast<uint16_t>(base + 1), static_cast<uint16_t>(base + 2),
                                  base, static_cast<uint16_t>(base + 2), static_cast<uint16_t>(base + 3)};
        std::copy(std::begin(quad), std::end(quad), indices.begin() + static_cast<ptrdiff_t>(i * 6));
    }
    lightIndices_ = device_->createBuffer(
        {indices.size() * sizeof(uint16_t), rhi::BufferUsage::Index, "light indices"}, indices.data());
    viewUniforms_ = device_->createBuffer({sizeof(LightView), rhi::BufferUsage::Uniform, "light view"});

    rhi::TextureDesc occluderDesc;
    occluderDesc.width = targets_.extent.width;
    occluderDesc.height = targets_.extent.height;
    occluderDesc.format = rhi::Format::R8_UNorm;
    occluderDesc.debugName = "light occluders";
    occluders_ = device_->createTexture(occluderDesc);
    occluderDesc.debugName = "ambient occlusion";
    ambientOcclusion_ = device_->createTexture(occluderDesc);
    if (!occluders_ || !ambientOcclusion_) {
        return false;
    }
    setOccluders(std::vector<uint8_t>(size_t{targets_.extent.width} * targets_.extent.height, 0));

    rhi::BindGroupDesc viewGroup;
    viewGroup.pipeline = lightPipeline_;
    viewGroup.set = 0;
    viewGroup.entries = {rhi::BindGroupEntry::uniformBuffer(0, viewUniforms_)};
    viewGroup.debugName = "light view";
    lightViewGroup_ = device_->createBindGroup(viewGroup);

    rhi::BindGroupDesc inputGroup;
    inputGroup.pipeline = lightPipeline_;
    inputGroup.set = 1;
    inputGroup.entries = {rhi::BindGroupEntry::textureSampler(0, targets_.normal, sampler_),
                          rhi::BindGroupEntry::textureSampler(1, occluders_, sampler_),
                          rhi::BindGroupEntry::textureSampler(2, targets_.flags, sampler_)};
    inputGroup.debugName = "light inputs";
    lightInputGroup_ = device_->createBindGroup(inputGroup);

    vertices_.reserve(size_t{kMaxLights} * 4);
    return lightVertices_ && lightIndices_ && lightViewGroup_ && lightInputGroup_;
}

bool LightingPass::initIndirect(const std::string& shaderDirectory) {
    indirectVertex_ = loadShader(*device_, shaderDirectory + "composite.vert.ashd");
    downsampleFragment_ = loadShader(*device_, shaderDirectory + "light_downsample.frag.ashd");
    blurFragment_ = loadShader(*device_, shaderDirectory + "post_blur.frag.ashd");
    if (!indirectVertex_ || !downsampleFragment_ || !blurFragment_) {
        return false;
    }
    const auto pipeline = [&](rhi::ShaderHandle fragment, const char* name) {
        rhi::GraphicsPipelineDesc desc;
        desc.vertexShader = indirectVertex_;
        desc.fragmentShader = fragment;
        desc.vertexBuffers = {{2 * sizeof(float), rhi::VertexStepMode::Vertex}};
        desc.vertexAttributes = {{0, 0, rhi::VertexFormat::Float2, 0}};
        desc.colorTargets = {{rhi::Format::RGBA16_Float, rhi::BlendState::opaque()}};
        desc.debugName = name;
        return device_->createGraphicsPipeline(desc);
    };
    downsamplePipeline_ = pipeline(downsampleFragment_, "indirect downsample");
    blurPipeline_ = pipeline(blurFragment_, "indirect blur");
    if (!downsamplePipeline_ || !blurPipeline_) {
        return false;
    }

    const uint32_t width = (targets_.extent.width + 3) / 4;
    const uint32_t height = (targets_.extent.height + 3) / 4;
    for (size_t i = 0; i < indirect_.size(); ++i) {
        rhi::TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.format = rhi::Format::RGBA16_Float;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderTarget;
        desc.debugName = i == 0 ? "indirect 0" : "indirect 1";
        indirect_[i] = device_->createTexture(desc);
    }
    downsampleUniforms_ = device_->createBuffer({4 * sizeof(float), rhi::BufferUsage::Uniform, "indirect downsample"});
    const float size[4] = {static_cast<float>(targets_.extent.width), static_cast<float>(targets_.extent.height), 0.0f,
                           0.0f};
    device_->writeBuffer(downsampleUniforms_, 0, size, sizeof(size));
    // Two blurs, a wide one (every second texel) and a narrow one: the light spreads some 40 pixels.
    const float steps[4][4] = {{2.0f / static_cast<float>(width), 0.0f, 0.0f, 0.0f},
                               {0.0f, 2.0f / static_cast<float>(height), 0.0f, 0.0f},
                               {1.0f / static_cast<float>(width), 0.0f, 0.0f, 0.0f},
                               {0.0f, 1.0f / static_cast<float>(height), 0.0f, 0.0f}};
    for (size_t i = 0; i < blurUniforms_.size(); ++i) {
        blurUniforms_[i] = device_->createBuffer({4 * sizeof(float), rhi::BufferUsage::Uniform, "indirect blur"});
        device_->writeBuffer(blurUniforms_[i], 0, steps[i], sizeof(steps[i]));
    }

    const auto group = [&](rhi::PipelineHandle pipe, uint32_t set, rhi::BindGroupEntry entry) {
        rhi::BindGroupDesc desc;
        desc.pipeline = pipe;
        desc.set = set;
        desc.entries = {entry};
        desc.debugName = "indirect";
        return device_->createBindGroup(desc);
    };
    downsampleSettings_ = group(downsamplePipeline_, 0, rhi::BindGroupEntry::uniformBuffer(0, downsampleUniforms_));
    downsampleInputs_ = group(downsamplePipeline_, 1, rhi::BindGroupEntry::textureSampler(0, targets_.light, sampler_));
    for (size_t i = 0; i < blurSettings_.size(); ++i) {
        blurSettings_[i] = group(blurPipeline_, 0, rhi::BindGroupEntry::uniformBuffer(0, blurUniforms_[i]));
    }
    for (size_t i = 0; i < blurInputs_.size(); ++i) {
        blurInputs_[i] = group(blurPipeline_, 1, rhi::BindGroupEntry::textureSampler(0, indirect_[i], linearSampler_));
    }
    return indirect_[0] && indirect_[1] && downsampleSettings_ && downsampleInputs_ && blurInputs_[0] && blurInputs_[1];
}

bool LightingPass::initCompositePipeline(const std::string& shaderDirectory, rhi::Format outputFormat) {
    compositeVertex_ = loadShader(*device_, shaderDirectory + "composite.vert.ashd");
    compositeFragment_ = loadShader(*device_, shaderDirectory + "composite.frag.ashd");
    if (!compositeVertex_ || !compositeFragment_) {
        return false;
    }

    rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = compositeVertex_;
    desc.fragmentShader = compositeFragment_;
    desc.vertexBuffers = {{2 * sizeof(float), rhi::VertexStepMode::Vertex}};
    desc.vertexAttributes = {{0, 0, rhi::VertexFormat::Float2, 0}};
    desc.colorTargets = {{outputFormat, rhi::BlendState::opaque()}};
    desc.debugName = "composite";
    compositePipeline_ = device_->createGraphicsPipeline(desc);
    if (!compositePipeline_) {
        return false;
    }

    const float triangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    triangle_ = device_->createBuffer({sizeof(triangle), rhi::BufferUsage::Vertex, "fullscreen triangle"}, triangle);
    compositeUniforms_ =
        device_->createBuffer({sizeof(CompositeUniforms), rhi::BufferUsage::Uniform, "composite settings"});

    rhi::BindGroupDesc settingsGroup;
    settingsGroup.pipeline = compositePipeline_;
    settingsGroup.set = 0;
    settingsGroup.entries = {rhi::BindGroupEntry::uniformBuffer(0, compositeUniforms_)};
    settingsGroup.debugName = "composite settings";
    compositeSettingsGroup_ = device_->createBindGroup(settingsGroup);

    rhi::BindGroupDesc inputGroup;
    inputGroup.pipeline = compositePipeline_;
    inputGroup.set = 1;
    inputGroup.entries = {
        rhi::BindGroupEntry::textureSampler(0, targets_.albedo, sampler_),
        rhi::BindGroupEntry::textureSampler(1, targets_.light, sampler_),
        rhi::BindGroupEntry::textureSampler(2, targets_.emissive, sampler_),
        rhi::BindGroupEntry::textureSampler(3, targets_.smoke, linearSampler_),
        rhi::BindGroupEntry::textureSampler(4, targets_.rim, sampler_),
        rhi::BindGroupEntry::textureSampler(5, targets_.flags, sampler_),
        rhi::BindGroupEntry::textureSampler(6, indirect_[0], sampler_),
        rhi::BindGroupEntry::textureSampler(7, ambientOcclusion_, sampler_),
    };
    inputGroup.debugName = "composite inputs";
    compositeInputGroup_ = device_->createBindGroup(inputGroup);
    return triangle_ && compositeSettingsGroup_ && compositeInputGroup_;
}

void LightingPass::shutdown() {
    if (!device_) {
        return;
    }
    for (rhi::BindGroupHandle group : {lightViewGroup_, lightInputGroup_, compositeSettingsGroup_, compositeInputGroup_,
                                       downsampleSettings_, downsampleInputs_, blurInputs_[0], blurInputs_[1]}) {
        device_->destroy(group);
    }
    for (rhi::BindGroupHandle group : blurSettings_) {
        device_->destroy(group);
    }
    for (rhi::BufferHandle buffer : blurUniforms_) {
        device_->destroy(buffer);
    }
    device_->destroy(downsampleUniforms_);
    for (rhi::TextureHandle texture : indirect_) {
        device_->destroy(texture);
    }
    device_->destroy(ambientOcclusion_);
    device_->destroy(downsamplePipeline_);
    device_->destroy(blurPipeline_);
    for (rhi::ShaderHandle shader : {indirectVertex_, downsampleFragment_, blurFragment_}) {
        destroyShader(*device_, shader);
    }
    for (rhi::BufferHandle buffer : {lightVertices_, lightIndices_, viewUniforms_, triangle_, compositeUniforms_}) {
        device_->destroy(buffer);
    }
    device_->destroy(occluders_);
    device_->destroy(lightPipeline_);
    device_->destroy(compositePipeline_);
    for (rhi::ShaderHandle shader : {lightVertex_, lightFragment_, compositeVertex_, compositeFragment_}) {
        destroyShader(*device_, shader);
    }
    device_->destroy(sampler_);
    device_->destroy(linearSampler_);
    device_ = nullptr;
}

void LightingPass::setOccluders(std::span<const uint8_t> mask) {
    const size_t expected = size_t{targets_.extent.width} * targets_.extent.height;
    if (mask.size() != expected) {
        log::error("LightingPass::setOccluders: expected {}x{} bytes, got {}", targets_.extent.width,
                   targets_.extent.height, mask.size());
        return;
    }
    std::vector<uint8_t> bytes(mask.size());
    std::transform(mask.begin(), mask.end(), bytes.begin(), [](uint8_t v) { return v ? uint8_t{255} : uint8_t{0}; });
    device_->writeTexture(occluders_, {0, 0, targets_.extent.width, targets_.extent.height}, bytes.data(),
                          targets_.extent.width);

    // Ambient occlusion of every open pixel: the share of occluder pixels in the box around it (an
    // integral image), doubled and capped, so a pixel against a flat wall reads about 0.9 and open
    // space 0. Occluder pixels keep their ambient light.
    const int w = static_cast<int>(targets_.extent.width);
    const int h = static_cast<int>(targets_.extent.height);
    constexpr int kRadius = 6;
    std::vector<int> sum(static_cast<size_t>((w + 1) * (h + 1)), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int solid = mask[static_cast<size_t>(y * w + x)] ? 1 : 0;
            sum[static_cast<size_t>((y + 1) * (w + 1) + x + 1)] = solid + sum[static_cast<size_t>(y * (w + 1) + x + 1)] +
                                                                  sum[static_cast<size_t>((y + 1) * (w + 1) + x)] -
                                                                  sum[static_cast<size_t>(y * (w + 1) + x)];
        }
    }
    const auto boxSum = [&](int x0, int y0, int x1, int y1) { // inclusive-exclusive, clamped
        x0 = std::clamp(x0, 0, w);
        x1 = std::clamp(x1, 0, w);
        y0 = std::clamp(y0, 0, h);
        y1 = std::clamp(y1, 0, h);
        return sum[static_cast<size_t>(y1 * (w + 1) + x1)] - sum[static_cast<size_t>(y0 * (w + 1) + x1)] -
               sum[static_cast<size_t>(y1 * (w + 1) + x0)] + sum[static_cast<size_t>(y0 * (w + 1) + x0)];
    };
    constexpr int kArea = (2 * kRadius + 1) * (2 * kRadius + 1);
    std::vector<uint8_t> occlusion(bytes.size(), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (mask[static_cast<size_t>(y * w + x)]) {
                continue;
            }
            const int solid = boxSum(x - kRadius, y - kRadius, x + kRadius + 1, y + kRadius + 1);
            occlusion[static_cast<size_t>(y * w + x)] = static_cast<uint8_t>(std::min(255, solid * 2 * 255 / kArea));
        }
    }
    device_->writeTexture(ambientOcclusion_, {0, 0, targets_.extent.width, targets_.extent.height}, occlusion.data(),
                          targets_.extent.width);
}

void LightingPass::fullscreen(rhi::CommandList& commands, rhi::PipelineHandle pipeline, rhi::BindGroupHandle settings,
                              rhi::BindGroupHandle inputs, rhi::TextureHandle target) {
    commands.beginRenderPass(rhi::RenderPassDesc::single({target, rhi::LoadOp::DontCare, rhi::StoreOp::Store, {}}));
    commands.setPipeline(pipeline);
    commands.setVertexBuffer(0, triangle_);
    commands.setBindGroup(0, settings);
    commands.setBindGroup(1, inputs);
    commands.draw(3);
    commands.endRenderPass();
}

void LightingPass::beginFrame() {
    vertices_.clear();
    renderedThisFrame_ = false;
}

void LightingPass::render(rhi::CommandList& commands, const Mat4& viewProjection, Vec2 canvasOrigin,
                          std::span<const PointLight> lights, const LightingSettings& settings, const SmokeLook& smoke,
                          rhi::TextureHandle output) {
    ANIMAL_ASSERT(!renderedThisFrame_);
    renderedThisFrame_ = true;

    const size_t count = std::min<size_t>(lights.size(), kMaxLights);
    if (lights.size() > kMaxLights) {
        log::warn("LightingPass: {} lights this frame; only the first {} are drawn", lights.size(), kMaxLights);
    }
    for (size_t i = 0; i < count; ++i) {
        const PointLight& light = lights[i];
        const float r = light.radius;
        const Vec3 c = light.color * light.intensity;
        const float x0 = light.position.x - r;
        const float y0 = light.position.y - r;
        const float x1 = light.position.x + r;
        const float y1 = light.position.y + r;
        const float texelX = std::floor(light.position.x - canvasOrigin.x);
        const float texelY = std::floor(light.position.y - canvasOrigin.y);
        // The cone fades over its outer fifth (banded into steps by the composite).
        const bool spot = light.coneAngle > 0.0f && light.coneAngle < 2.0f * kPi;
        const float half = 0.5f * light.coneAngle;
        const float spotOuter = spot ? std::cos(half) : -2.0f;
        const float spotInner = spot ? std::cos(half * 0.8f) : -1.0f;
        const Vec2 dir = spot ? normalize(light.direction) : Vec2{0.0f, 1.0f};
        for (const auto& [x, y] : {std::pair{x0, y0}, std::pair{x1, y0}, std::pair{x1, y1}, std::pair{x0, y1}}) {
            vertices_.push_back({x, y, light.position.x, light.position.y, c.x, c.y, c.z, 1.0f, r, light.height,
                                 light.castsShadows ? 1.0f : 0.0f, 0.0f, texelX, texelY, dir.x, dir.y, spotOuter,
                                 spotInner});
        }
    }
    lastFrameLights_ = static_cast<uint32_t>(count);

    const LightView view = {viewProjection,
                            {static_cast<float>(targets_.extent.width), static_cast<float>(targets_.extent.height),
                             settings.shadows ? 1.0f : 0.0f, std::max(0.0f, settings.shadowPenetration)},
                            {std::max(0.0f, settings.rimIntensity), std::clamp(settings.backFlatten, 0.0f, 1.0f), 0.0f,
                             0.0f}};
    device_->writeBuffer(viewUniforms_, 0, &view, sizeof(view));
    const CompositeUniforms uniforms = {
        {settings.ambient.x, settings.ambient.y, settings.ambient.z, 0.0f},
        {settings.enabled ? 1.0f : 0.0f, static_cast<float>(std::max(0, settings.bands)), settings.dither ? 1.0f : 0.0f,
         0.0f},
        {static_cast<float>(targets_.extent.width), static_cast<float>(targets_.extent.height), 0.0f, 0.0f},
        {smoke.tint.x, smoke.tint.y, smoke.tint.z, smoke.opacity},
        {settings.rimAmbient.x, settings.rimAmbient.y, settings.rimAmbient.z, 0.0f},
        {std::max(0.0f, settings.backLight), std::clamp(settings.depthFog, 0.0f, 1.0f),
         std::clamp(settings.ambientOcclusion, 0.0f, 1.0f), std::clamp(settings.ambientGradient, -1.0f, 1.0f)},
        {settings.depthFogColor.x, settings.depthFogColor.y, settings.depthFogColor.z, std::max(0.0f, settings.indirect)},
    };
    device_->writeBuffer(compositeUniforms_, 0, &uniforms, sizeof(uniforms));

    rhi::RenderPassDesc lightPass;
    lightPass.colorAttachments[0] = {targets_.light, rhi::LoadOp::Clear, rhi::StoreOp::Store, {0.0f, 0.0f, 0.0f, 0.0f}};
    lightPass.colorAttachments[1] = {targets_.rim, rhi::LoadOp::Clear, rhi::StoreOp::Store, {0.0f, 0.0f, 0.0f, 0.0f}};
    lightPass.colorAttachmentCount = 2;
    commands.beginRenderPass(lightPass);
    if (count > 0) {
        commands.setPipeline(lightPipeline_);
        commands.setVertexBuffer(0, lightVertices_);
        commands.setIndexBuffer(lightIndices_, rhi::IndexType::UInt16);
        commands.setBindGroup(0, lightViewGroup_);
        commands.setBindGroup(1, lightInputGroup_);
        commands.drawIndexed(static_cast<uint32_t>(count * 6));
    }
    commands.endRenderPass();

    // Indirect light: always computed (the composite samples it even at strength 0), tiny at a
    // quarter of the resolution.
    if (settings.indirect > 0.0f) {
        fullscreen(commands, downsamplePipeline_, downsampleSettings_, downsampleInputs_, indirect_[0]);
        fullscreen(commands, blurPipeline_, blurSettings_[0], blurInputs_[0], indirect_[1]);
        fullscreen(commands, blurPipeline_, blurSettings_[1], blurInputs_[1], indirect_[0]);
        fullscreen(commands, blurPipeline_, blurSettings_[2], blurInputs_[0], indirect_[1]);
        fullscreen(commands, blurPipeline_, blurSettings_[3], blurInputs_[1], indirect_[0]);
        indirectCleared_ = false;
    } else if (!indirectCleared_) {
        // The composite still reads it (times 0): it must hold numbers, not stale or undefined texels.
        commands.beginRenderPass(rhi::RenderPassDesc::single(
            {indirect_[0], rhi::LoadOp::Clear, rhi::StoreOp::Store, {0.0f, 0.0f, 0.0f, 0.0f}}));
        commands.endRenderPass();
        indirectCleared_ = true;
    }

    commands.beginRenderPass(rhi::RenderPassDesc::single({output, rhi::LoadOp::DontCare, rhi::StoreOp::Store, {}}));
    commands.setPipeline(compositePipeline_);
    commands.setVertexBuffer(0, triangle_);
    commands.setBindGroup(0, compositeSettingsGroup_);
    commands.setBindGroup(1, compositeInputGroup_);
    commands.draw(3);
    commands.endRenderPass();
}

void LightingPass::endFrame() {
    if (!vertices_.empty()) {
        device_->writeBuffer(lightVertices_, 0, vertices_.data(), vertices_.size() * sizeof(LightVertex));
    }
}

} // namespace animal
