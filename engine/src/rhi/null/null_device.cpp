// Null backend (headless runs, ADR-0032): creates handles and remembers texture sizes, draws nothing.
// The game simulates exactly as with a GPU, so input-script playthroughs run on build machines.
#include <animal/core/handle.h>
#include <animal/core/log.h>
#include <animal/rhi/command_list.h>
#include <animal/rhi/rhi.h>

namespace animal::rhi {

namespace {

class NullDevice final : public Device {
public:
    NullDevice() {
        caps_.apiVersion = "none";
        caps_.deviceName = "Null (headless)";
        caps_.maxTextureSize = 16384;
        caps_.uniformBufferAlignment = 256;
        caps_.framesInFlight = 1;
        swapchain_ = textures_.insert({1280, 720});
    }

    Backend backend() const override { return Backend::Null; }
    const DeviceCaps& caps() const override { return caps_; }

    BufferHandle createBuffer(const BufferDesc&, const void*) override { return buffers_.insert(0); }
    TextureHandle createTexture(const TextureDesc& desc) override { return textures_.insert({desc.width, desc.height}); }
    SamplerHandle createSampler(const SamplerDesc&) override { return samplers_.insert(0); }
    ShaderHandle createShader(const ShaderDesc& desc) override {
        return desc.package ? shaders_.insert(0) : ShaderHandle{};
    }
    PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc&) override { return pipelines_.insert(0); }
    BindGroupHandle createBindGroup(const BindGroupDesc&) override { return groups_.insert(0); }
    bool reloadShader(ShaderHandle shader, const ShaderDesc&) override { return shaders_.get(shader) != nullptr; }
    bool recreateTexture(TextureHandle texture, const TextureDesc& desc) override {
        Extent2D* extent = textures_.get(texture);
        if (!extent) {
            return false;
        }
        *extent = {desc.width, desc.height};
        return true;
    }
    Extent2D textureExtent(TextureHandle texture) const override {
        const Extent2D* extent = textures_.get(texture);
        return extent ? *extent : Extent2D{};
    }

    void destroy(BufferHandle h) override { buffers_.take(h); }
    void destroy(TextureHandle h) override {
        if (h != swapchain_) {
            textures_.take(h);
        }
    }
    void destroy(SamplerHandle h) override { samplers_.take(h); }
    void destroy(ShaderHandle h) override { shaders_.take(h); }
    void destroy(PipelineHandle h) override { pipelines_.take(h); }
    void destroy(BindGroupHandle h) override { groups_.take(h); }

    void writeBuffer(BufferHandle, uint64_t, const void*, uint64_t) override {}
    void writeTexture(TextureHandle, const TextureRegion&, const void*, uint32_t) override {}
    std::vector<uint8_t> readTexture(TextureHandle) override { return {}; }

    bool beginFrame() override { return true; }
    TextureHandle swapchainTexture() const override { return swapchain_; }
    Extent2D swapchainExtent() const override { return {1280, 720}; }
    Format swapchainFormat() const override { return Format::RGBA8_UNorm; }
    void submit(const CommandList&) override {}
    void endFrame() override {}
    void waitIdle() override {}
    std::vector<GpuTiming> gpuTimings() const override { return {}; }

private:
    DeviceCaps caps_;
    TextureHandle swapchain_;
    HandlePool<BufferTag, int> buffers_;
    HandlePool<TextureTag, Extent2D> textures_;
    HandlePool<SamplerTag, int> samplers_;
    HandlePool<ShaderTag, int> shaders_;
    HandlePool<PipelineTag, int> pipelines_;
    HandlePool<BindGroupTag, int> groups_;
};

} // namespace

std::unique_ptr<Device> createNullDevice() {
    log::info("Null device: running headless (nothing is drawn)");
    return std::make_unique<NullDevice>();
}

} // namespace animal::rhi
