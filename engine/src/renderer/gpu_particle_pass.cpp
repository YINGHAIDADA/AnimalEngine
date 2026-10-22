#include "gpu_particle_pass.h"

#include <animal/core/log.h>
#include <animal/resources/loaders.h>
#include <animal/rhi/command_list.h>

#include <algorithm>

namespace animal {

namespace {

constexpr rhi::BlendState kAdditive{true,
                                    rhi::BlendFactor::One,
                                    rhi::BlendFactor::One,
                                    rhi::BlendOp::Add,
                                    rhi::BlendFactor::One,
                                    rhi::BlendFactor::One,
                                    rhi::BlendOp::Add};

void store(float out[4], float a, float b, float c, float d) {
    out[0] = a;
    out[1] = b;
    out[2] = c;
    out[3] = d;
}

void store(float out[4], Color c) {
    store(out, c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

} // namespace

bool GpuParticlePass::init(rhi::Device& device, const std::string& shaderDirectory, rhi::TextureHandle occluders,
                           rhi::Extent2D canvas, const Formats& formats) {
    device_ = &device;
    canvas_ = canvas;
    fullscreenVertex_ = loadShader(device, shaderDirectory + "composite.vert.ashd");
    updateFragment_ = loadShader(device, shaderDirectory + "gpu_particles_update.frag.ashd");
    drawVertex_ = loadShader(device, shaderDirectory + "gpu_particles.vert.ashd");
    litFragment_ = loadShader(device, shaderDirectory + "gpu_particles_lit.frag.ashd");
    glowFragment_ = loadShader(device, shaderDirectory + "gpu_particles_glow.frag.ashd");
    if (!fullscreenVertex_ || !updateFragment_ || !drawVertex_ || !litFragment_ || !glowFragment_) {
        return false;
    }

    rhi::GraphicsPipelineDesc update;
    update.vertexShader = fullscreenVertex_;
    update.fragmentShader = updateFragment_;
    update.vertexBuffers = {{2 * sizeof(float), rhi::VertexStepMode::Vertex}};
    update.vertexAttributes = {{0, 0, rhi::VertexFormat::Float2, 0}};
    update.colorTargets = {{rhi::Format::RGBA32_Float, rhi::BlendState::opaque()},
                           {rhi::Format::RGBA32_Float, rhi::BlendState::opaque()}};
    update.debugName = "gpu particles update";
    updatePipeline_ = device.createGraphicsPipeline(update);

    rhi::GraphicsPipelineDesc lit;
    lit.vertexShader = drawVertex_;
    lit.fragmentShader = litFragment_;
    lit.vertexBuffers = {{2 * sizeof(float), rhi::VertexStepMode::Vertex}};
    lit.vertexAttributes = {{0, 0, rhi::VertexFormat::Float2, 0}};
    for (rhi::Format format : formats.material) {
        lit.colorTargets.push_back({format, rhi::BlendState::premultipliedAlpha()});
    }
    lit.debugName = "gpu particles lit";
    litPipeline_ = device.createGraphicsPipeline(lit);

    rhi::GraphicsPipelineDesc glow = lit;
    glow.fragmentShader = glowFragment_;
    glow.colorTargets = {{formats.glow, kAdditive}};
    glow.debugName = "gpu particles glow";
    glowPipeline_ = device.createGraphicsPipeline(glow);
    if (!updatePipeline_ || !litPipeline_ || !glowPipeline_) {
        return false;
    }

    const float triangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    triangle_ = device.createBuffer({sizeof(triangle), rhi::BufferUsage::Vertex, "gpu particles triangle"}, triangle);
    const float corners[12] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
    corners_ = device.createBuffer({sizeof(corners), rhi::BufferUsage::Vertex, "gpu particle quad"}, corners);
    simulationUniforms_ =
        device.createBuffer({sizeof(SimulationUniforms), rhi::BufferUsage::Uniform, "gpu particles simulation"});
    for (rhi::BufferHandle& b : drawUniforms_) {
        b = device.createBuffer({sizeof(DrawUniforms), rhi::BufferUsage::Uniform, "gpu particles draw"});
    }
    sampler_ = device.createSampler({});
    for (size_t i = 0; i < 2; ++i) {
        rhi::TextureDesc desc;
        desc.width = kStateWidth;
        desc.height = kGpuParticleCapacity / kStateWidth;
        desc.format = rhi::Format::RGBA32_Float;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderTarget;
        desc.debugName = i == 0 ? "gpu particles A0" : "gpu particles A1";
        stateA_[i] = device.createTexture(desc);
        desc.debugName = i == 0 ? "gpu particles B0" : "gpu particles B1";
        stateB_[i] = device.createTexture(desc);
    }

    rhi::BindGroupDesc simulation;
    simulation.pipeline = updatePipeline_;
    simulation.set = 0;
    simulation.entries = {rhi::BindGroupEntry::uniformBuffer(0, simulationUniforms_)};
    simulation.debugName = "gpu particles simulation";
    simulationGroup_ = device.createBindGroup(simulation);
    for (size_t i = 0; i < 2; ++i) {
        rhi::BindGroupDesc inputs;
        inputs.pipeline = updatePipeline_;
        inputs.set = 1;
        inputs.entries = {rhi::BindGroupEntry::textureSampler(0, stateA_[i], sampler_),
                          rhi::BindGroupEntry::textureSampler(1, stateB_[i], sampler_),
                          rhi::BindGroupEntry::textureSampler(2, occluders, sampler_)};
        inputs.debugName = "gpu particles update inputs";
        updateInputs_[i] = device.createBindGroup(inputs);

        rhi::BindGroupDesc settings;
        settings.pipeline = i == 0 ? litPipeline_ : glowPipeline_;
        settings.set = 0;
        settings.entries = {rhi::BindGroupEntry::uniformBuffer(0, drawUniforms_[i])};
        settings.debugName = "gpu particles draw settings";
        drawSettings_[i] = device.createBindGroup(settings);

        rhi::BindGroupDesc drawInputs;
        drawInputs.pipeline = litPipeline_;
        drawInputs.set = 1;
        drawInputs.entries = {rhi::BindGroupEntry::textureSampler(0, stateA_[i], sampler_),
                              rhi::BindGroupEntry::textureSampler(1, stateB_[i], sampler_)};
        drawInputs.debugName = "gpu particles draw inputs";
        drawInputs_[i] = device.createBindGroup(drawInputs);
    }
    return triangle_ && corners_ && simulationUniforms_ && simulationGroup_ && updateInputs_[1] && drawInputs_[1];
}

void GpuParticlePass::shutdown() {
    if (!device_) {
        return;
    }
    for (rhi::BindGroupHandle g : {simulationGroup_, updateInputs_[0], updateInputs_[1], drawSettings_[0],
                                   drawSettings_[1], drawInputs_[0], drawInputs_[1]}) {
        device_->destroy(g);
    }
    for (rhi::BufferHandle b : {triangle_, corners_, simulationUniforms_, drawUniforms_[0], drawUniforms_[1]}) {
        device_->destroy(b);
    }
    for (rhi::TextureHandle t : {stateA_[0], stateA_[1], stateB_[0], stateB_[1]}) {
        device_->destroy(t);
    }
    for (rhi::PipelineHandle p : {updatePipeline_, litPipeline_, glowPipeline_}) {
        device_->destroy(p);
    }
    for (rhi::ShaderHandle s : {fullscreenVertex_, updateFragment_, drawVertex_, litFragment_, glowFragment_}) {
        device_->destroy(s);
    }
    device_->destroy(sampler_);
    device_ = nullptr;
}

void GpuParticlePass::setEmitters(std::span<const GpuEmitter> emitters) {
    if (emitters.size() > kMaxGpuEmitters) {
        log::warn("GpuParticlePass: {} emitters; only the first {} are used", emitters.size(), kMaxGpuEmitters);
    }
    emitters_ = {};
    emitterCount_ = std::min(emitters.size(), kMaxGpuEmitters);
    slotsUsed_ = 0;
    for (size_t i = 0; i < emitterCount_; ++i) {
        const GpuEmitter& e = emitters[i];
        const uint32_t count = std::min(e.count, kGpuParticleCapacity - slotsUsed_);
        if (count < e.count) {
            log::warn("GpuParticlePass: capacity {} reached; emitter {} gets {} of {} particles", kGpuParticleCapacity,
                      i, count, e.count);
        }
        EmitterData& d = emitters_[i];
        store(d.area, e.area.x, e.area.y, e.area.width, e.area.height);
        store(d.velocity, e.velocityX.x, e.velocityX.y, e.velocityY.x, e.velocityY.y);
        store(d.motion, e.acceleration.x, e.acceleration.y, e.drag, e.windResponse);
        store(d.life, e.lifetime.x, std::max(e.lifetime.y, e.lifetime.x), e.fade, e.floorY);
        store(d.startColor, e.startColor);
        store(d.endColor, e.endColor);
        store(d.slots, static_cast<float>(slotsUsed_), static_cast<float>(count), e.dieOnSolid ? 1.0f : 0.0f,
              e.style == GpuEmitter::Style::Glow ? 1.0f : 0.0f);
        styles_[i] = e.style;
        slotsUsed_ += count;
    }
    restart_ = true;
}

bool GpuParticlePass::hasStyle(GpuEmitter::Style style) const {
    for (size_t i = 0; i < emitterCount_; ++i) {
        if (styles_[i] == style && emitters_[i].slots[1] > 0.0f) {
            return true;
        }
    }
    return false;
}

void GpuParticlePass::update(rhi::CommandList& commands, float dt, Vec2 wind, Vec2 canvasOrigin) {
    if (!active() || (dt <= 0.0f && !restart_)) {
        return;
    }
    SimulationUniforms u{};
    store(u.step, dt, restart_ ? 1.0f : 0.0f, static_cast<float>(frame_ & 0xFFFFFFu), static_cast<float>(kStateWidth));
    store(u.canvas, canvasOrigin.x, canvasOrigin.y, static_cast<float>(canvas_.width), static_cast<float>(canvas_.height));
    store(u.wind, wind.x, wind.y, 0.0f, 0.0f);
    std::copy(emitters_.begin(), emitters_.end(), u.emitters);
    device_->writeBuffer(simulationUniforms_, 0, &u, sizeof(u));

    const size_t next = 1 - current_;
    rhi::RenderPassDesc pass;
    pass.colorAttachments[0] = {stateA_[next], rhi::LoadOp::DontCare, rhi::StoreOp::Store, {}};
    pass.colorAttachments[1] = {stateB_[next], rhi::LoadOp::DontCare, rhi::StoreOp::Store, {}};
    pass.colorAttachmentCount = 2;
    commands.beginRenderPass(pass);
    commands.setPipeline(updatePipeline_);
    commands.setVertexBuffer(0, triangle_);
    commands.setBindGroup(0, simulationGroup_);
    commands.setBindGroup(1, updateInputs_[current_]);
    commands.draw(3);
    commands.endRenderPass();
    current_ = next;
    restart_ = false;
    lastStep_ = dt;
    ++frame_;
}

void GpuParticlePass::draw(rhi::CommandList& commands, const Mat4& viewProjection, GpuEmitter::Style style) {
    if (!hasStyle(style)) {
        return;
    }
    const size_t s = style == GpuEmitter::Style::Glow ? 1 : 0;
    DrawUniforms u{};
    u.viewProjection = viewProjection;
    store(u.params, static_cast<float>(kStateWidth), static_cast<float>(s), lastStep_, 0.0f);
    std::copy(emitters_.begin(), emitters_.end(), u.emitters);
    device_->writeBuffer(drawUniforms_[s], 0, &u, sizeof(u));

    commands.setPipeline(s == 1 ? glowPipeline_ : litPipeline_);
    commands.setVertexBuffer(0, corners_);
    commands.setBindGroup(0, drawSettings_[s]);
    commands.setBindGroup(1, drawInputs_[current_]);
    commands.draw(6, slotsUsed_);
}

} // namespace animal
