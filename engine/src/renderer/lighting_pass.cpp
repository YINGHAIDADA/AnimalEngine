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
    return sampler_ && linearSampler_ && initLightPipeline(shaderDirectory) && initCompositePipeline(shaderDirectory, outputFormat);
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
    desc.colorTargets = {{rhi::Format::RGBA16_Float, kAdditive}};
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
    if (!occluders_) {
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
                          rhi::BindGroupEntry::textureSampler(1, occluders_, sampler_)};
    inputGroup.debugName = "light inputs";
    lightInputGroup_ = device_->createBindGroup(inputGroup);

    vertices_.reserve(size_t{kMaxLights} * 4);
    return lightVertices_ && lightIndices_ && lightViewGroup_ && lightInputGroup_;
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
    };
    inputGroup.debugName = "composite inputs";
    compositeInputGroup_ = device_->createBindGroup(inputGroup);
    return triangle_ && compositeSettingsGroup_ && compositeInputGroup_;
}

void LightingPass::shutdown() {
    if (!device_) {
        return;
    }
    for (rhi::BindGroupHandle group :
         {lightViewGroup_, lightInputGroup_, compositeSettingsGroup_, compositeInputGroup_}) {
        device_->destroy(group);
    }
    for (rhi::BufferHandle buffer : {lightVertices_, lightIndices_, viewUniforms_, triangle_, compositeUniforms_}) {
        device_->destroy(buffer);
    }
    device_->destroy(occluders_);
    device_->destroy(lightPipeline_);
    device_->destroy(compositePipeline_);
    for (rhi::ShaderHandle shader : {lightVertex_, lightFragment_, compositeVertex_, compositeFragment_}) {
        device_->destroy(shader);
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
                             settings.shadows ? 1.0f : 0.0f, std::max(0.0f, settings.shadowPenetration)}};
    device_->writeBuffer(viewUniforms_, 0, &view, sizeof(view));
    const CompositeUniforms uniforms = {
        {settings.ambient.x, settings.ambient.y, settings.ambient.z, 0.0f},
        {settings.enabled ? 1.0f : 0.0f, static_cast<float>(std::max(0, settings.bands)), settings.dither ? 1.0f : 0.0f,
         0.0f},
        {static_cast<float>(targets_.extent.width), static_cast<float>(targets_.extent.height), 0.0f, 0.0f},
        {smoke.tint.x, smoke.tint.y, smoke.tint.z, smoke.opacity},
    };
    device_->writeBuffer(compositeUniforms_, 0, &uniforms, sizeof(uniforms));

    commands.beginRenderPass(rhi::RenderPassDesc::single({targets_.light, rhi::LoadOp::Clear, rhi::StoreOp::Store,
                                                          {0.0f, 0.0f, 0.0f, 0.0f}}));
    if (count > 0) {
        commands.setPipeline(lightPipeline_);
        commands.setVertexBuffer(0, lightVertices_);
        commands.setIndexBuffer(lightIndices_, rhi::IndexType::UInt16);
        commands.setBindGroup(0, lightViewGroup_);
        commands.setBindGroup(1, lightInputGroup_);
        commands.drawIndexed(static_cast<uint32_t>(count * 6));
    }
    commands.endRenderPass();

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
