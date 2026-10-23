#include "fluid_pass.h"

#include <animal/core/assert.h>
#include <animal/core/log.h>
#include <animal/resources/loaders.h>
#include <animal/rhi/command_list.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace animal {

namespace {

constexpr const char* kFragmentFiles[] = {
    "fluid_advect.frag.ashd", "fluid_splat.frag.ashd", "fluid_divergence.frag.ashd",
    "fluid_pressure.frag.ashd", "fluid_gradient.frag.ashd", "fluid_curl.frag.ashd", "fluid_vorticity.frag.ashd",
};

// Which inputs (set 1, in binding order) use linear filtering, per program.
constexpr bool kLinearInputs[][4] = {
    {true, true, false, true}, // advect: velocity, source, obstacles, density
    {false, false},            // splat: target, obstacles
    {false, false},            // divergence: velocity, obstacles
    {false, false, false},     // pressure: pressure, divergence, obstacles
    {false, false, false},     // gradient: pressure, velocity, obstacles
    {false},                   // curl: velocity
    {false, false, false},     // vorticity: velocity, curl, obstacles
};

} // namespace

bool FluidPass::init(rhi::Device& device, const std::string& shaderDirectory) {
    device_ = &device;
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
        desc.colorTargets = {{rhi::Format::RGBA16_Float, rhi::BlendState::opaque()}};
        desc.debugName = kFragmentFiles[i];
        pipelines_[i] = device.createGraphicsPipeline(desc);
        if (!pipelines_[i]) {
            return false;
        }
    }

    const float triangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    triangle_ = device.createBuffer({sizeof(triangle), rhi::BufferUsage::Vertex, "fluid triangle"}, triangle);
    linear_ = device.createSampler({rhi::Filter::Linear, rhi::Filter::Linear});
    nearest_ = device.createSampler({});

    gridUniforms_ = device.createBuffer({sizeof(AdvectUniforms), rhi::BufferUsage::Uniform, "fluid grid"});
    advectVelocityUniforms_ =
        device.createBuffer({sizeof(AdvectUniforms), rhi::BufferUsage::Uniform, "fluid advect velocity"});
    advectSmokeUniforms_ =
        device.createBuffer({sizeof(AdvectUniforms), rhi::BufferUsage::Uniform, "fluid advect smoke"});
    vorticityUniforms_ = device.createBuffer({sizeof(AdvectUniforms), rhi::BufferUsage::Uniform, "fluid vorticity"});
    forceSplatUniforms_ = device.createBuffer({sizeof(SplatUniforms), rhi::BufferUsage::Uniform, "fluid forces"});
    smokeSplatUniforms_ = device.createBuffer({sizeof(SplatUniforms), rhi::BufferUsage::Uniform, "fluid smoke"});

    return triangle_ && linear_ && nearest_ && createTargets();
}

bool FluidPass::createTargets() {
    auto field = [this](const char* name) {
        rhi::TextureDesc desc;
        desc.width = kFluidGrid.width;
        desc.height = kFluidGrid.height;
        desc.format = rhi::Format::RGBA16_Float;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderTarget;
        desc.debugName = name;
        return device_->createTexture(desc);
    };
    velocity_ = {field("fluid velocity 0"), field("fluid velocity 1")};
    smoke_ = {field("fluid smoke 0"), field("fluid smoke 1")};
    pressure_ = {field("fluid pressure 0"), field("fluid pressure 1")};
    divergence_ = field("fluid divergence");
    curl_ = field("fluid curl");

    rhi::TextureDesc maskDesc;
    maskDesc.width = kFluidGrid.width;
    maskDesc.height = kFluidGrid.height;
    maskDesc.format = rhi::Format::R8_UNorm;
    maskDesc.debugName = "fluid obstacles";
    obstacles_ = device_->createTexture(maskDesc);
    if (obstacles_) {
        setObstacles(std::vector<uint8_t>(size_t{kFluidGrid.width} * kFluidGrid.height, 0));
    }
    for (rhi::TextureHandle t : {velocity_[0], velocity_[1], smoke_[0], smoke_[1], pressure_[0], pressure_[1],
                                 divergence_, curl_, obstacles_}) {
        if (!t) {
            return false;
        }
    }
    return true;
}

void FluidPass::shutdown() {
    if (!device_) {
        return;
    }
    for (auto& [key, group] : groups_) {
        device_->destroy(group);
    }
    groups_.clear();
    for (rhi::TextureHandle t : {velocity_[0], velocity_[1], smoke_[0], smoke_[1], pressure_[0], pressure_[1],
                                 divergence_, curl_, obstacles_}) {
        device_->destroy(t);
    }
    for (rhi::BufferHandle b : {triangle_, gridUniforms_, advectVelocityUniforms_, advectSmokeUniforms_, vorticityUniforms_,
                                forceSplatUniforms_, smokeSplatUniforms_}) {
        device_->destroy(b);
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

void FluidPass::setObstacles(std::span<const uint8_t> mask) {
    if (mask.size() != size_t{kFluidGrid.width} * kFluidGrid.height) {
        log::error("FluidPass::setObstacles: expected {}x{} bytes, got {}", kFluidGrid.width, kFluidGrid.height,
                   mask.size());
        return;
    }
    std::vector<uint8_t> bytes(mask.size());
    std::transform(mask.begin(), mask.end(), bytes.begin(), [](uint8_t v) { return v ? uint8_t{255} : uint8_t{0}; });
    device_->writeTexture(obstacles_, {0, 0, kFluidGrid.width, kFluidGrid.height}, bytes.data(), kFluidGrid.width);
}

rhi::BindGroupHandle FluidPass::uniformGroup(Program program, rhi::BufferHandle buffer) {
    const std::array<uint64_t, 6> key{program, 0xFFFF, buffer.key(), 0, 0, 0};
    if (const auto it = groups_.find(key); it != groups_.end()) {
        return it->second;
    }
    rhi::BindGroupDesc desc;
    desc.pipeline = pipelines_[program];
    desc.set = 0;
    desc.entries = {rhi::BindGroupEntry::uniformBuffer(0, buffer)};
    desc.debugName = "fluid uniforms";
    const rhi::BindGroupHandle group = device_->createBindGroup(desc);
    groups_.emplace(key, group);
    return group;
}

rhi::BindGroupHandle FluidPass::inputGroup(Program program, std::span<const rhi::TextureHandle> textures) {
    std::array<uint64_t, 6> key{program, 0, 0, 0, 0, 0};
    for (size_t i = 0; i < textures.size(); ++i) {
        key[i + 1] = textures[i].key();
    }
    if (const auto it = groups_.find(key); it != groups_.end()) {
        return it->second;
    }
    rhi::BindGroupDesc desc;
    desc.pipeline = pipelines_[program];
    desc.set = 1;
    for (uint32_t i = 0; i < textures.size(); ++i) {
        desc.entries.push_back(
            rhi::BindGroupEntry::textureSampler(i, textures[i], kLinearInputs[program][i] ? linear_ : nearest_));
    }
    desc.debugName = "fluid inputs";
    const rhi::BindGroupHandle group = device_->createBindGroup(desc);
    groups_.emplace(key, group);
    return group;
}

void FluidPass::pass(rhi::CommandList& commands, Program program, rhi::BindGroupHandle uniforms,
                     std::span<const rhi::TextureHandle> inputs, rhi::TextureHandle target) {
    commands.beginRenderPass(rhi::RenderPassDesc::single({target, rhi::LoadOp::DontCare, rhi::StoreOp::Store, {}}));
    commands.setPipeline(pipelines_[program]);
    commands.setVertexBuffer(0, triangle_);
    commands.setBindGroup(0, uniforms);
    commands.setBindGroup(1, inputGroup(program, inputs));
    commands.draw(3);
    commands.endRenderPass();
}

FluidPass::SplatUniforms FluidPass::packSplats(std::span<const GridSplat> splats) {
    SplatUniforms u{};
    const size_t count = std::min<size_t>(splats.size(), kMaxSplats);
    u.info[0] = static_cast<float>(count);
    u.info[1] = static_cast<float>(kFluidGrid.width) / static_cast<float>(kFluidGrid.height);
    for (size_t i = 0; i < count; ++i) {
        u.shape[i][0] = splats[i].uv.x;
        u.shape[i][1] = splats[i].uv.y;
        u.shape[i][2] = std::max(splats[i].radius, 1e-4f);
        std::copy(std::begin(splats[i].value), std::end(splats[i].value), u.value[i]);
    }
    return u;
}

void FluidPass::simulate(rhi::CommandList& commands, float dt, std::span<const GridSplat> forces,
                         std::span<const GridSplat> smoke, const FluidSettings& settings) {
    ANIMAL_ASSERT(!simulatedThisFrame_);
    simulatedThisFrame_ = true;

    if (needsClear_) {
        for (rhi::TextureHandle t : {velocity_[0], velocity_[1], smoke_[0], smoke_[1], pressure_[0], pressure_[1],
                                     divergence_, curl_}) {
            commands.beginRenderPass(
                rhi::RenderPassDesc::single({t, rhi::LoadOp::Clear, rhi::StoreOp::Store, {0.0f, 0.0f, 0.0f, 0.0f}}));
            commands.endRenderPass();
        }
        needsClear_ = false;
    }
    if (dt <= 0.0f) {
        return;
    }
    if (forces.size() > kMaxSplats || smoke.size() > kMaxSplats) {
        log::warn("FluidPass: more than {} splats of one kind this frame; extra splats ignored", kMaxSplats);
    }

    const float texelX = 1.0f / static_cast<float>(kFluidGrid.width);
    const float texelY = 1.0f / static_cast<float>(kFluidGrid.height);
    const AdvectUniforms grid{{texelX, texelY, 0.0f, 0.0f}, {}};
    const AdvectUniforms advectVelocity{{texelX, texelY, 0.0f, 0.0f},
                                        {dt, std::pow(settings.velocityKeepPerSecond, dt), settings.buoyancy, 0.0f}};
    const AdvectUniforms advectSmoke{{texelX, texelY, 0.0f, 0.0f},
                                     {dt, std::pow(settings.smokeKeepPerSecond, dt), 0.0f, 0.0f}};
    const AdvectUniforms vorticity{{texelX, texelY, 0.0f, 0.0f}, {dt, settings.vorticity, 0.0f, 0.0f}};
    const SplatUniforms forceSplats = packSplats(forces);
    const SplatUniforms smokeSplats = packSplats(smoke);
    device_->writeBuffer(gridUniforms_, 0, &grid, sizeof(grid));
    device_->writeBuffer(advectVelocityUniforms_, 0, &advectVelocity, sizeof(advectVelocity));
    device_->writeBuffer(advectSmokeUniforms_, 0, &advectSmoke, sizeof(advectSmoke));
    device_->writeBuffer(vorticityUniforms_, 0, &vorticity, sizeof(vorticity));
    device_->writeBuffer(forceSplatUniforms_, 0, &forceSplats, sizeof(forceSplats));
    device_->writeBuffer(smokeSplatUniforms_, 0, &smokeSplats, sizeof(smokeSplats));

    size_t v = velocityIndex_;
    auto swapVelocity = [&v] { v = 1 - v; };

    // 1. Advect velocity (with buoyancy from the current smoke).
    pass(commands, Advect, uniformGroup(Advect, advectVelocityUniforms_),
         std::array{velocity_[v], velocity_[v], obstacles_, smoke_[0]}, velocity_[1 - v]);
    swapVelocity();
    // 2. External forces.
    pass(commands, Splat, uniformGroup(Splat, forceSplatUniforms_), std::array{velocity_[v], obstacles_},
         velocity_[1 - v]);
    swapVelocity();
    // 3. Vorticity confinement.
    if (settings.vorticity > 0.0f) {
        pass(commands, Curl, uniformGroup(Curl, gridUniforms_), std::array{velocity_[v]}, curl_);
        pass(commands, Vorticity, uniformGroup(Vorticity, vorticityUniforms_),
             std::array{velocity_[v], curl_, obstacles_}, velocity_[1 - v]);
        swapVelocity();
    }
    // 4–6. Projection.
    pass(commands, Divergence, uniformGroup(Divergence, gridUniforms_), std::array{velocity_[v], obstacles_},
         divergence_);
    const int iterations = (std::max(2, settings.pressureIterations) + 1) / 2 * 2; // even: ends in pressure_[0]
    commands.beginTimer("pressure");
    for (int i = 0; i < iterations; ++i) {
        const size_t from = static_cast<size_t>(i % 2);
        pass(commands, Pressure, uniformGroup(Pressure, gridUniforms_),
             std::array{pressure_[from], divergence_, obstacles_}, pressure_[1 - from]);
    }
    commands.endTimer();
    pass(commands, Gradient, uniformGroup(Gradient, gridUniforms_), std::array{pressure_[0], velocity_[v], obstacles_},
         velocity_[1 - v]);
    swapVelocity();
    velocityIndex_ = v;
    // 7. Advect smoke and add sources; two passes so it always ends in smoke_[0].
    pass(commands, Advect, uniformGroup(Advect, advectSmokeUniforms_),
         std::array{velocity_[v], smoke_[0], obstacles_, smoke_[0]}, smoke_[1]);
    pass(commands, Splat, uniformGroup(Splat, smokeSplatUniforms_), std::array{smoke_[1], obstacles_}, smoke_[0]);
}

} // namespace animal
