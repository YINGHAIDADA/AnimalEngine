#pragma once

// Backend-independent RHI validation (ADR-0021): a Device decorator that checks resource
// descriptions and recorded command lists with the same rules and messages for every backend.
// createDevice() wraps the backend in it when DeviceDesc::debug is set.

#include <animal/rhi/command_list.h>
#include <animal/rhi/rhi.h>
#include <animal/rhi/shader_package.h>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace animal::rhi {

class ValidatingDevice final : public Device {
public:
    explicit ValidatingDevice(std::unique_ptr<Device> inner);
    ~ValidatingDevice() override;

    Backend backend() const override { return inner_->backend(); }
    const DeviceCaps& caps() const override { return inner_->caps(); }

    BufferHandle createBuffer(const BufferDesc& desc, const void* initialData = nullptr) override;
    TextureHandle createTexture(const TextureDesc& desc) override;
    SamplerHandle createSampler(const SamplerDesc& desc) override;
    ShaderHandle createShader(const ShaderDesc& desc) override;
    PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    BindGroupHandle createBindGroup(const BindGroupDesc& desc) override;
    bool reloadShader(ShaderHandle shader, const ShaderDesc& desc) override;
    bool recreateTexture(TextureHandle texture, const TextureDesc& desc) override;
    Extent2D textureExtent(TextureHandle texture) const override { return inner_->textureExtent(texture); }

    void destroy(BufferHandle handle) override;
    void destroy(TextureHandle handle) override;
    void destroy(SamplerHandle handle) override;
    void destroy(ShaderHandle handle) override;
    void destroy(PipelineHandle handle) override;
    void destroy(BindGroupHandle handle) override;

    void writeBuffer(BufferHandle buffer, uint64_t offset, const void* data, uint64_t size) override;
    void writeTexture(TextureHandle texture, const TextureRegion& region, const void* pixels,
                      uint32_t bytesPerRow) override;
    std::vector<uint8_t> readTexture(TextureHandle texture) override;

    bool beginFrame() override { return inner_->beginFrame(); }
    TextureHandle swapchainTexture() const override { return inner_->swapchainTexture(); }
    Extent2D swapchainExtent() const override { return inner_->swapchainExtent(); }
    Format swapchainFormat() const override { return inner_->swapchainFormat(); }
    void submit(const CommandList& commands) override;
    void endFrame() override { inner_->endFrame(); }
    void waitIdle() override { inner_->waitIdle(); }
    void setVsync(bool vsync) override { inner_->setVsync(vsync); }
    std::vector<GpuTiming> gpuTimings() const override { return inner_->gpuTimings(); }

    // Every problem found so far (the first 100 messages are kept).
    uint32_t errorCount() const { return errorCount_; }
    const std::vector<std::string>& messages() const { return messages_; }
    // Checks a command list without submitting it; returns the number of new problems.
    uint32_t validate(const CommandList& commands);

private:
    // Binding signature of one set: (binding, kind) pairs sorted by binding.
    using SetLayout = std::vector<std::pair<uint32_t, BindingKind>>;

    struct BufferInfo {
        uint64_t size;
        BufferUsage usage;
    };
    struct TextureInfo {
        Extent2D extent;
        Format format;
        TextureUsage usage;
    };
    struct ShaderInfo {
        ShaderStage stage;
        std::vector<ShaderBinding> bindings;
    };
    struct PipelineInfo {
        std::string name;
        std::vector<Format> colorFormats;
        uint32_t vertexBuffers = 0;
        std::array<SetLayout, kMaxBindGroups> sets; // empty: the set is not used
    };
    struct BindGroupInfo {
        uint32_t set;
        SetLayout layout;
        std::vector<TextureHandle> textures;
        std::vector<BufferHandle> buffers;
    };

    void report(std::string message);
    bool textureInfo(TextureHandle handle, TextureInfo& out) const;

    std::unique_ptr<Device> inner_;
    std::map<uint64_t, BufferInfo> buffers_;
    std::map<uint64_t, TextureInfo> textures_;
    std::map<uint64_t, bool> samplers_;
    std::map<uint64_t, ShaderInfo> shaders_;
    std::map<uint64_t, PipelineInfo> pipelines_;
    std::map<uint64_t, BindGroupInfo> bindGroups_;
    uint32_t errorCount_ = 0;
    std::vector<std::string> messages_;
};

} // namespace animal::rhi
