#include "post_pass.h"

#include <animal/resources/loaders.h>
#include <animal/rhi/command_list.h>

#include <algorithm>

namespace animal {

namespace {

constexpr const char* kFragmentFiles[] = {"post_bright.frag.ashd", "post_blur.frag.ashd", "post_grade.frag.ashd"};

} // namespace

bool PostPass::init(rhi::Device& device, const std::string& shaderDirectory, rhi::TextureHandle canvas,
                    rhi::TextureHandle output, rhi::Extent2D canvasExtent) {
    device_ = &device;
    canvas_ = canvas;
    output_ = output;
    canvasExtent_ = canvasExtent;

    vertex_ = loadShader(device, shaderDirectory + "composite.vert.ashd");
    if (!vertex_) {
        return false;
    }
    for (size_t i = 0; i < ProgramCount; ++i) {
        fragments_[i] = loadShader(device, shaderDirectory + kFragmentFiles[i]);
        if (!fragments_[i]) {
            return false;
        }
        rhi::GraphicsPipelineDesc desc;
        desc.vertexShader = vertex_;
        desc.fragmentShader = fragments_[i];
        desc.vertexBuffers = {{2 * sizeof(float), rhi::VertexStepMode::Vertex}};
        desc.vertexAttributes = {{0, 0, rhi::VertexFormat::Float2, 0}};
        const rhi::Format format = i == Grade ? rhi::Format::RGBA8_UNorm : rhi::Format::RGBA16_Float;
        desc.colorTargets = {{format, rhi::BlendState::opaque()}};
        desc.debugName = kFragmentFiles[i];
        pipelines_[i] = device.createGraphicsPipeline(desc);
        if (!pipelines_[i]) {
            return false;
        }
    }

    const float triangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    triangle_ = device.createBuffer({sizeof(triangle), rhi::BufferUsage::Vertex, "post triangle"}, triangle);
    linear_ = device.createSampler({rhi::Filter::Linear, rhi::Filter::Linear});
    nearest_ = device.createSampler({});

    for (size_t i = 0; i < bloom_.size(); ++i) {
        rhi::TextureDesc desc;
        desc.width = canvasExtent.width / 2;
        desc.height = canvasExtent.height / 2;
        desc.format = rhi::Format::RGBA16_Float;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderTarget;
        desc.debugName = i == 0 ? "bloom 0" : "bloom 1";
        bloom_[i] = device.createTexture(desc);
    }

    brightUniforms_ = device.createBuffer({4 * sizeof(float), rhi::BufferUsage::Uniform, "bloom bright"});
    blurHorizontalUniforms_ = device.createBuffer({4 * sizeof(float), rhi::BufferUsage::Uniform, "bloom blur h"});
    blurVerticalUniforms_ = device.createBuffer({4 * sizeof(float), rhi::BufferUsage::Uniform, "bloom blur v"});
    gradeUniforms_ = device.createBuffer({sizeof(GradeUniforms), rhi::BufferUsage::Uniform, "post grade"});

    // The blur steps are fixed by the bloom resolution; written once.
    const float w = static_cast<float>(canvasExtent.width / 2);
    const float h = static_cast<float>(canvasExtent.height / 2);
    const float horizontal[4] = {1.0f / w, 0.0f, 0.0f, 0.0f};
    const float vertical[4] = {0.0f, 1.0f / h, 0.0f, 0.0f};
    device.writeBuffer(blurHorizontalUniforms_, 0, horizontal, sizeof(horizontal));
    device.writeBuffer(blurVerticalUniforms_, 0, vertical, sizeof(vertical));

    brightSettings_ = makeUniformGroup(Bright, brightUniforms_);
    brightInputs_ = makeGroup(Bright, 1, {canvas_, {}}, {linear_, {}}, 1);
    blurHorizontalSettings_ = makeUniformGroup(Blur, blurHorizontalUniforms_);
    blurVerticalSettings_ = makeUniformGroup(Blur, blurVerticalUniforms_);
    blurFromA_ = makeGroup(Blur, 1, {bloom_[0], {}}, {linear_, {}}, 1);
    blurFromB_ = makeGroup(Blur, 1, {bloom_[1], {}}, {linear_, {}}, 1);
    gradeSettings_ = makeUniformGroup(Grade, gradeUniforms_);
    gradeInputs_ = makeGroup(Grade, 1, {canvas_, bloom_[0]}, {nearest_, linear_}, 2);

    return triangle_ && bloom_[0] && bloom_[1] && brightInputs_ && blurFromA_ && blurFromB_ && gradeInputs_;
}

rhi::BindGroupHandle PostPass::makeUniformGroup(Program program, rhi::BufferHandle buffer) {
    rhi::BindGroupDesc desc;
    desc.pipeline = pipelines_[program];
    desc.set = 0;
    desc.entries = {rhi::BindGroupEntry::uniformBuffer(0, buffer)};
    desc.debugName = "post settings";
    return device_->createBindGroup(desc);
}

rhi::BindGroupHandle PostPass::makeGroup(Program program, uint32_t set, std::array<rhi::TextureHandle, 2> textures,
                                         std::array<rhi::SamplerHandle, 2> samplers, uint32_t textureCount) {
    rhi::BindGroupDesc desc;
    desc.pipeline = pipelines_[program];
    desc.set = set;
    for (uint32_t i = 0; i < textureCount; ++i) {
        desc.entries.push_back(rhi::BindGroupEntry::textureSampler(i, textures[i], samplers[i]));
    }
    desc.debugName = "post inputs";
    return device_->createBindGroup(desc);
}

void PostPass::shutdown() {
    if (!device_) {
        return;
    }
    for (rhi::BindGroupHandle g : {brightSettings_, brightInputs_, blurHorizontalSettings_, blurVerticalSettings_,
                                   blurFromA_, blurFromB_, gradeSettings_, gradeInputs_}) {
        device_->destroy(g);
    }
    for (rhi::BufferHandle b : {triangle_, brightUniforms_, blurHorizontalUniforms_, blurVerticalUniforms_,
                                gradeUniforms_}) {
        device_->destroy(b);
    }
    for (rhi::TextureHandle t : bloom_) {
        device_->destroy(t);
    }
    for (size_t i = 0; i < ProgramCount; ++i) {
        device_->destroy(pipelines_[i]);
        destroyShader(*device_, fragments_[i]);
    }
    destroyShader(*device_, vertex_);
    device_->destroy(linear_);
    device_->destroy(nearest_);
    device_ = nullptr;
}

void PostPass::pass(rhi::CommandList& commands, Program program, rhi::BindGroupHandle uniforms,
                    rhi::BindGroupHandle inputs, rhi::TextureHandle target) {
    commands.beginRenderPass(rhi::RenderPassDesc::single({target, rhi::LoadOp::DontCare, rhi::StoreOp::Store, {}}));
    commands.setPipeline(pipelines_[program]);
    commands.setVertexBuffer(0, triangle_);
    commands.setBindGroup(0, uniforms);
    commands.setBindGroup(1, inputs);
    commands.draw(3);
    commands.endRenderPass();
}

void PostPass::render(rhi::CommandList& commands, const PostSettings& settings) {
    const bool bloom = settings.enabled && settings.bloomIntensity > 0.0f;

    // Neutral grade when disabled: the canvas is copied unchanged.
    GradeUniforms grade{};
    grade.bloom[0] = bloom ? settings.bloomIntensity : 0.0f;
    grade.bloom[1] = static_cast<float>(std::max(0, settings.bloomBands));
    const bool on = settings.enabled;
    grade.grade[0] = on ? settings.exposure : 1.0f;
    grade.grade[1] = on ? settings.contrast : 1.0f;
    grade.grade[2] = on ? settings.saturation : 1.0f;
    grade.grade[3] = on ? settings.vignette : 0.0f;
    const Vec3 shadow = on ? settings.shadowTint : Vec3{1.0f, 1.0f, 1.0f};
    const Vec3 highlight = on ? settings.highlightTint : Vec3{1.0f, 1.0f, 1.0f};
    std::copy_n(&shadow.x, 3, grade.shadowTint);
    std::copy_n(&highlight.x, 3, grade.highlightTint);
    grade.canvasSize[0] = static_cast<float>(canvasExtent_.width);
    grade.canvasSize[1] = static_cast<float>(canvasExtent_.height);
    device_->writeBuffer(gradeUniforms_, 0, &grade, sizeof(grade));

    if (needsClear_) {
        // The grade pass always samples bloom_[0], even with bloom off (weight 0); fresh images
        // may hold NaNs, and 0 × NaN is still NaN.
        for (rhi::TextureHandle t : bloom_) {
            commands.beginRenderPass(
                rhi::RenderPassDesc::single({t, rhi::LoadOp::Clear, rhi::StoreOp::Store, {0.0f, 0.0f, 0.0f, 0.0f}}));
            commands.endRenderPass();
        }
        needsClear_ = false;
    }
    if (bloom) {
        const float bright[4] = {settings.bloomThreshold, std::max(settings.bloomKnee, 1e-3f), 0.0f, 0.0f};
        device_->writeBuffer(brightUniforms_, 0, bright, sizeof(bright));
        pass(commands, Bright, brightSettings_, brightInputs_, bloom_[0]);
        for (int iteration = 0; iteration < 2; ++iteration) {
            pass(commands, Blur, blurHorizontalSettings_, blurFromA_, bloom_[1]);
            pass(commands, Blur, blurVerticalSettings_, blurFromB_, bloom_[0]);
        }
    }
    pass(commands, Grade, gradeSettings_, gradeInputs_, output_);
}

} // namespace animal
