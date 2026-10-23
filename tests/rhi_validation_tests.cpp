// Backend-independent RHI validation (ADR-0021), run against a device that does nothing.

#include <animal/core/file.h>
#include <animal/core/handle.h>
#include <animal/resources/loaders.h>
#include <animal/rhi/validation.h>

#include "test_harness.h"

#include <filesystem>
#include <memory>
#include <string>

namespace {

// Cleanup must not throw (Windows refuses to delete files that are still open).
std::error_code cleanupError;

using namespace animal;
using namespace animal::rhi;

// Hands out handles and otherwise does nothing.
class NullDevice final : public Device {
public:
    NullDevice() {
        caps_.maxTextureSize = 4096;
        caps_.uniformBufferAlignment = 256;
        swapchain_ = textures_.insert(0);
    }
    Backend backend() const override { return Backend::OpenGL; }
    const DeviceCaps& caps() const override { return caps_; }
    BufferHandle createBuffer(const BufferDesc&, const void*) override { return buffers_.insert(0); }
    TextureHandle createTexture(const TextureDesc&) override { return textures_.insert(0); }
    SamplerHandle createSampler(const SamplerDesc&) override { return samplers_.insert(0); }
    ShaderHandle createShader(const ShaderDesc&) override { return shaders_.insert(0); }
    PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc&) override { return pipelines_.insert(0); }
    BindGroupHandle createBindGroup(const BindGroupDesc&) override { return groups_.insert(0); }
    bool reloadShader(ShaderHandle h, const ShaderDesc&) override { return shaders_.get(h) != nullptr; }
    bool recreateTexture(TextureHandle h, const TextureDesc&) override { return textures_.get(h) != nullptr; }
    Extent2D textureExtent(TextureHandle) const override { return {}; }
    void destroy(BufferHandle h) override { buffers_.take(h); }
    void destroy(TextureHandle h) override { textures_.take(h); }
    void destroy(SamplerHandle h) override { samplers_.take(h); }
    void destroy(ShaderHandle h) override { shaders_.take(h); }
    void destroy(PipelineHandle h) override { pipelines_.take(h); }
    void destroy(BindGroupHandle h) override { groups_.take(h); }
    void writeBuffer(BufferHandle, uint64_t, const void*, uint64_t) override {}
    void writeTexture(TextureHandle, const TextureRegion&, const void*, uint32_t) override {}
    std::vector<uint8_t> readTexture(TextureHandle) override { return {}; }
    bool beginFrame() override { return true; }
    TextureHandle swapchainTexture() const override { return swapchain_; }
    Extent2D swapchainExtent() const override { return {64, 64}; }
    Format swapchainFormat() const override { return Format::RGBA8_UNorm; }
    void submit(const CommandList&) override {}
    void endFrame() override {}
    void waitIdle() override {}
    std::vector<GpuTiming> gpuTimings() const override { return {}; }

private:
    DeviceCaps caps_;
    TextureHandle swapchain_;
    HandlePool<BufferTag, int> buffers_;
    HandlePool<TextureTag, int> textures_;
    HandlePool<SamplerTag, int> samplers_;
    HandlePool<ShaderTag, int> shaders_;
    HandlePool<PipelineTag, int> pipelines_;
    HandlePool<BindGroupTag, int> groups_;
};

// A textured quad setup: VS uses a uniform buffer (set 0), FS a texture (set 1).
struct Rig {
    ValidatingDevice device{std::make_unique<NullDevice>()};
    ShaderPackage vsPackage{ShaderStage::Vertex, {{0, 0, BindingKind::UniformBuffer}}, {}, {}};
    ShaderPackage fsPackage{ShaderStage::Fragment, {{1, 0, BindingKind::CombinedImageSampler}}, {}, {}};
    ShaderHandle vs = device.createShader({&vsPackage, "vs"});
    ShaderHandle fs = device.createShader({&fsPackage, "fs"});
    TextureHandle target = device.createTexture({32, 32, Format::RGBA8_UNorm, TextureUsage::RenderTarget, "target"});
    TextureHandle image = device.createTexture(
        {16, 16, Format::RGBA8_UNorm, TextureUsage::Sampled | TextureUsage::RenderTarget, "image"});
    SamplerHandle sampler = device.createSampler({});
    BufferHandle vertices = device.createBuffer({64, BufferUsage::Vertex, "vertices"});
    BufferHandle indices = device.createBuffer({12, BufferUsage::Index, "indices"});
    BufferHandle uniforms = device.createBuffer({256, BufferUsage::Uniform, "uniforms"});
    PipelineHandle pipeline;
    BindGroupHandle view;
    BindGroupHandle material;

    Rig() {
        GraphicsPipelineDesc desc;
        desc.vertexShader = vs;
        desc.fragmentShader = fs;
        desc.vertexBuffers = {{8, VertexStepMode::Vertex}};
        desc.vertexAttributes = {{0, 0, VertexFormat::Float2, 0}};
        desc.colorTargets = {{Format::RGBA8_UNorm, BlendState::opaque()}};
        desc.debugName = "quad";
        pipeline = device.createGraphicsPipeline(desc);
        view = device.createBindGroup({pipeline, 0, {BindGroupEntry::uniformBuffer(0, uniforms)}, "view"});
        material = device.createBindGroup({pipeline, 1, {BindGroupEntry::textureSampler(0, image, sampler)}, "mat"});
    }

    // A complete, valid draw into `into`; `skip` leaves out one step (a missing pipeline is
    // already asserted by CommandList while recording).
    CommandList draw(TextureHandle into, const std::string& skip = "") {
        CommandList list;
        list.beginRenderPass(RenderPassDesc::single({into, LoadOp::Clear, StoreOp::Store, {}}));
        list.setPipeline(pipeline);
        if (skip != "vertices") {
            list.setVertexBuffer(0, vertices);
        }
        list.setIndexBuffer(indices, IndexType::UInt16);
        if (skip != "view") {
            list.setBindGroup(0, view);
        }
        list.setBindGroup(1, material);
        list.drawIndexed(6);
        list.endRenderPass();
        return list;
    }
};

bool mentions(const ValidatingDevice& device, const std::string& text) {
    for (const std::string& m : device.messages()) {
        if (m.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

TEST(rhi_validation_accepts_correct_use) {
    Rig rig;
    CHECK(rig.pipeline && rig.view && rig.material);
    CHECK(rig.device.validate(rig.draw(rig.target)) == 0);
    CHECK(rig.device.validate(rig.draw(rig.device.swapchainTexture())) == 0);
    CHECK(rig.device.errorCount() == 0);
}

TEST(rhi_validation_reports_missing_draw_state) {
    Rig rig;
    CHECK(rig.device.validate(rig.draw(rig.target, "vertices")) == 1);
    CHECK(mentions(rig.device, "needs vertex buffer slot 0"));
    CHECK(rig.device.validate(rig.draw(rig.target, "view")) == 1);
    CHECK(mentions(rig.device, "needs a bind group for set 0"));
}

TEST(rhi_validation_reports_format_mismatch_and_feedback_loops) {
    Rig rig;
    const TextureHandle hdr =
        rig.device.createTexture({32, 32, Format::RGBA16_Float, TextureUsage::RenderTarget, "hdr"});
    CHECK(rig.device.validate(rig.draw(hdr)) == 1);
    CHECK(mentions(rig.device, "color targets do not match"));
    CHECK(rig.device.validate(rig.draw(rig.image)) >= 1); // samples the texture it renders into
    CHECK(mentions(rig.device, "sampled while it is an attachment"));
}

TEST(rhi_validation_checks_bind_groups_against_the_layout) {
    Rig rig;
    CHECK(!rig.device.createBindGroup({rig.pipeline, 1, {}, "empty"}));
    CHECK(mentions(rig.device, "set 1 has 1 bindings but 0 were given"));
    CHECK(!rig.device.createBindGroup({rig.pipeline, 0, {BindGroupEntry::textureSampler(0, rig.image, rig.sampler)}}));
    CHECK(mentions(rig.device, "needs a valid uniform buffer"));
    CHECK(!rig.device.createBindGroup({rig.pipeline, 0, {BindGroupEntry::uniformBuffer(0, rig.uniforms, 16, 64)}}));
    CHECK(mentions(rig.device, "misaligned or out of bounds"));
    CHECK(!rig.device.createBindGroup({rig.pipeline, 2, {}}));
    CHECK(mentions(rig.device, "does not use set 2"));
}

TEST(rhi_validation_catches_destroyed_resources) {
    Rig rig;
    rig.device.destroy(rig.image);
    CHECK(rig.device.validate(rig.draw(rig.target)) == 1);
    CHECK(mentions(rig.device, "samples a destroyed texture"));
    rig.device.destroy(rig.image);
    CHECK(mentions(rig.device, "already destroyed texture"));
    rig.device.destroy(rig.pipeline);
    CHECK(rig.device.validate(rig.draw(rig.target)) >= 1);
    CHECK(mentions(rig.device, "invalid or destroyed pipeline"));
}

TEST(rhi_validation_rejects_bad_descriptions) {
    Rig rig;
    CHECK(!rig.device.createTexture({0, 16, Format::RGBA8_UNorm, TextureUsage::Sampled, "zero"}));
    CHECK(!rig.device.createTexture({8192, 16, Format::RGBA8_UNorm, TextureUsage::Sampled, "huge"}));
    CHECK(!rig.device.createBuffer({0, BufferUsage::Vertex, "empty"}));
    GraphicsPipelineDesc blendFloat;
    blendFloat.vertexShader = rig.vs;
    blendFloat.fragmentShader = rig.fs;
    blendFloat.colorTargets = {{Format::RGBA32_Float, BlendState::premultipliedAlpha()}};
    CHECK(!rig.device.createGraphicsPipeline(blendFloat));
    CHECK(mentions(rig.device, "cannot blend"));
    GraphicsPipelineDesc swapped = blendFloat;
    swapped.vertexShader = rig.fs;
    swapped.colorTargets = {{Format::RGBA8_UNorm, BlendState::opaque()}};
    CHECK(!rig.device.createGraphicsPipeline(swapped));
    CHECK(mentions(rig.device, "needs a valid vertex shader"));
    std::vector<uint8_t> pixels(4 * 16 * 16);
    rig.device.writeTexture(rig.image, {8, 8, 16, 16}, pixels.data(), 64);
    CHECK(mentions(rig.device, "does not fit"));
}

// Hot reload through the loaders (ADR-0028): files are tracked by path; a reload that would change a
// handle's shape or cannot be read fails and leaves the handle alone.
TEST(loaders_reload_tracked_files_and_refuse_bad_ones) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "animal_reload_test";
    fs::remove_all(dir, cleanupError);
    fs::create_directories(dir);
    const auto pathOf = [&dir](const char* name) {
        const std::u8string u8 = (dir / name).generic_u8string();
        return std::string(u8.begin(), u8.end());
    };
    const std::vector<uint8_t> small(4 * 4 * 4, 200);
    const std::vector<uint8_t> large(8 * 4 * 4, 100);
    const std::string png = pathOf("sprite.png");
    CHECK(writePng(png, 4, 4, small.data()));

    ValidatingDevice device(std::make_unique<NullDevice>());
    const auto texture = loadTexture(device, png);
    CHECK(texture.has_value());
    CHECK(reloadTextureFile(device, png) == ReloadResult::Reloaded);
    CHECK(writePng(png, 8, 4, large.data()));
    CHECK(reloadTextureFile(device, png) == ReloadResult::Reloaded); // recreated at the new size
    CHECK(reloadTextureFile(device, pathOf("other.png")) == ReloadResult::NotLoaded);
    fs::remove(dir / "sprite.png");
    CHECK(reloadTextureFile(device, png) == ReloadResult::Failed);

    ShaderPackage package;
    package.stage = ShaderStage::Fragment;
    package.spirv = {0x07230203u, 0u};
    package.glsl = "void main() {}";
    const std::vector<std::byte> bytes = serializeShaderPackage(package);
    const std::string ashd = pathOf("test.frag.ashd");
    CHECK(writeFileAtomic(ashd, bytes.data(), bytes.size()));
    const ShaderHandle shader = loadShader(device, ashd);
    CHECK(shader.valid());
    CHECK(reloadShaderFile(device, dir.generic_string() + "/./test.frag.ashd") == ReloadResult::Reloaded);
    const char garbage[] = "not a shader package";
    CHECK(writeFileAtomic(ashd, garbage, sizeof(garbage)));
    CHECK(reloadShaderFile(device, ashd) == ReloadResult::Failed);
    CHECK(device.errorCount() == 0); // failed reloads are content problems, not API misuse

    // A material without companions gets neutral 1×1 stand-ins that a later file reloads into.
    CHECK(writePng(pathOf("hero.png"), 4, 4, small.data()));
    std::optional<SpriteMaterial> material = loadMaterial(device, pathOf("hero"));
    CHECK(material && material->normal.valid() && material->emissive.valid());
    CHECK(reloadTextureFile(device, pathOf("hero_e.png")) == ReloadResult::Failed); // not there yet
    CHECK(writePng(pathOf("hero_e.png"), 4, 4, small.data()));
    CHECK(reloadTextureFile(device, pathOf("hero_e.png")) == ReloadResult::Reloaded);
    if (material) {
        destroyMaterial(device, *material);
    }
    CHECK(reloadTextureFile(device, pathOf("hero_e.png")) == ReloadResult::NotLoaded);

    forgetLoaded(texture->handle);
    forgetLoaded(shader);
    CHECK(reloadShaderFile(device, ashd) == ReloadResult::NotLoaded);
    fs::remove_all(dir, cleanupError);
}

} // namespace
