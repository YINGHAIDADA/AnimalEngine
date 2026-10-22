#include "water_pass.h"

#include <animal/resources/loaders.h>
#include <animal/rhi/command_list.h>

namespace animal {

namespace {

constexpr float kOpacityPerDepthPixel = 0.015f;

} // namespace

bool WaterPass::init(rhi::Device& device, const std::string& shaderDirectory, const Targets& targets) {
    device_ = &device;
    targets_ = targets;
    vertex_ = loadShader(device, shaderDirectory + "composite.vert.ashd");
    fragment_ = loadShader(device, shaderDirectory + "water.frag.ashd");
    if (!vertex_ || !fragment_) {
        return false;
    }
    rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = vertex_;
    desc.fragmentShader = fragment_;
    desc.vertexBuffers = {{2 * sizeof(float), rhi::VertexStepMode::Vertex}};
    desc.vertexAttributes = {{0, 0, rhi::VertexFormat::Float2, 0}};
    desc.colorTargets = {{rhi::Format::RGBA8_UNorm, rhi::BlendState::opaque()}};
    desc.debugName = "water";
    pipeline_ = device.createGraphicsPipeline(desc);

    const float triangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    triangle_ = device.createBuffer({sizeof(triangle), rhi::BufferUsage::Vertex, "water triangle"}, triangle);
    uniforms_ = device.createBuffer({sizeof(Uniforms), rhi::BufferUsage::Uniform, "water settings"});
    sampler_ = device.createSampler({});

    rhi::TextureDesc columnDesc;
    columnDesc.width = targets.extent.width;
    columnDesc.height = 1;
    columnDesc.format = rhi::Format::RGBA8_UNorm;
    columnDesc.debugName = "water columns";
    columns_ = device.createTexture(columnDesc);
    if (!pipeline_ || !triangle_ || !uniforms_ || !sampler_ || !columns_) {
        return false;
    }

    rhi::BindGroupDesc settings;
    settings.pipeline = pipeline_;
    settings.set = 0;
    settings.entries = {rhi::BindGroupEntry::uniformBuffer(0, uniforms_)};
    settings.debugName = "water settings";
    settingsGroup_ = device.createBindGroup(settings);

    rhi::BindGroupDesc inputs;
    inputs.pipeline = pipeline_;
    inputs.set = 1;
    inputs.entries = {
        rhi::BindGroupEntry::textureSampler(0, targets.scene, sampler_),
        rhi::BindGroupEntry::textureSampler(1, targets.light, sampler_),
        rhi::BindGroupEntry::textureSampler(2, columns_, sampler_),
    };
    inputs.debugName = "water inputs";
    inputGroup_ = device.createBindGroup(inputs);
    return settingsGroup_ && inputGroup_;
}

void WaterPass::shutdown() {
    if (!device_) {
        return;
    }
    device_->destroy(settingsGroup_);
    device_->destroy(inputGroup_);
    device_->destroy(triangle_);
    device_->destroy(uniforms_);
    device_->destroy(columns_);
    device_->destroy(sampler_);
    device_->destroy(pipeline_);
    device_->destroy(vertex_);
    device_->destroy(fragment_);
    device_ = nullptr;
}

void WaterPass::render(rhi::CommandList& commands, const Frame& frame) {
    device_->writeTexture(columns_, {0, 0, targets_.extent.width, 1}, frame.columns.data(), targets_.extent.width * 4);
    Uniforms u{};
    u.canvas[0] = static_cast<float>(targets_.extent.width);
    u.canvas[1] = static_cast<float>(targets_.extent.height);
    u.canvas[2] = static_cast<float>(frame.phase);
    u.canvas[3] = static_cast<float>(frame.bands);
    u.ambient[0] = frame.ambient.x;
    u.ambient[1] = frame.ambient.y;
    u.ambient[2] = frame.ambient.z;
    u.ambient[3] = frame.dither ? 1.0f : 0.0f;
    for (size_t i = 0; i < kMaxWaterStyles; ++i) {
        const WaterSurface& s = frame.styles[i];
        u.color[i][0] = s.color.r / 255.0f;
        u.color[i][1] = s.color.g / 255.0f;
        u.color[i][2] = s.color.b / 255.0f;
        u.color[i][3] = s.opacity;
        u.params[i][0] = s.reflection;
        u.params[i][1] = static_cast<float>(s.refraction);
        u.params[i][2] = kOpacityPerDepthPixel;
    }
    device_->writeBuffer(uniforms_, 0, &u, sizeof(u));

    commands.beginRenderPass(
        rhi::RenderPassDesc::single({targets_.output, rhi::LoadOp::DontCare, rhi::StoreOp::Store, {}}));
    commands.setPipeline(pipeline_);
    commands.setVertexBuffer(0, triangle_);
    commands.setBindGroup(0, settingsGroup_);
    commands.setBindGroup(1, inputGroup_);
    commands.draw(3);
    commands.endRenderPass();
}

} // namespace animal
