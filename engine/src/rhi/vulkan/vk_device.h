#pragma once

// Vulkan 1.2 backend (+ VK_KHR_dynamic_rendering, VK_KHR_synchronization2; portability subset on
// MoltenVK). Maps the RHI's Vulkan-shaped semantics directly:
//  - writeBuffer/writeTexture inside a frame are staged and recorded into an upload command
//    buffer that is submitted ahead of the frame's commands; outside a frame they execute
//    immediately. Either way they are ordered before every later submit.
//  - Image layouts are tracked per texture. Because a CommandList is complete before submit(),
//    each render pass is pre-scanned and all transitions it needs are issued before
//    vkCmdBeginRenderingKHR (barriers are not allowed inside dynamic rendering).
//  - The "swapchain texture" is an RGBA8 offscreen image, blitted to the real swapchain image
//    in endFrame(); this keeps swapchainFormat() identical across backends.
//  - Descriptor set layouts come from the shader packages' binding tables and are cached by
//    signature, so bind groups are compatible across pipelines with identical layouts.

#include <volk.h>

#include <vk_mem_alloc.h>

#include <animal/core/assert.h>
#include <animal/core/file.h>
#include <animal/core/log.h>
#include <animal/platform/platform.h>
#include <animal/platform/window.h>
#include <animal/rhi/command_list.h>
#include <animal/rhi/gpu_timers.h>
#include <animal/rhi/rhi.h>

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <bit>
#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <functional>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace animal::rhi::vulkan {

constexpr uint32_t kFramesInFlight = 2;
constexpr VkDeviceSize kStagingBytesPerFrame = 1ull * 1024 * 1024; // initial; grows when a frame needs more

const char* resultName(VkResult result);
bool check(VkResult result, const char* what);
const char* nameOr(const char* name, const char* fallback);
VkFormat toVk(Format format);
VkFormat toVk(VertexFormat format);
VkBlendFactor toVk(BlendFactor factor);
VkBlendOp toVk(BlendOp op);
VkPrimitiveTopology toVk(PrimitiveTopology topology);
VkCullModeFlags toVk(CullMode mode);
VkAttachmentLoadOp toVk(LoadOp op);
VkDescriptorType toVk(BindingKind kind);

// How an image is used next; the barrier between two uses is derived from them (ADR-0023).
enum class ImageUse : uint8_t {
    None,       // contents undefined (new image, or about to be fully overwritten)
    Attachment, // color attachment of a render pass (loads, blends and stores)
    Sampled,    // read by vertex or fragment shaders
    CopyDst,
    CopySrc,
    Present,
};

struct UseScope {
    VkPipelineStageFlags2KHR stages;
    VkAccessFlags2KHR access; // reads and writes made by this use
    VkAccessFlags2KHR writes; // the subset that must be made available to the next use
    VkImageLayout layout;
};

inline UseScope scopeOf(ImageUse use) {
    switch (use) {
    case ImageUse::None:
        return {VK_PIPELINE_STAGE_2_NONE_KHR, 0, 0, VK_IMAGE_LAYOUT_UNDEFINED};
    case ImageUse::Attachment:
        return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT_KHR | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT_KHR,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT_KHR, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    case ImageUse::Sampled:
        return {VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT_KHR, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case ImageUse::CopyDst:
        return {VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR, VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
                VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    case ImageUse::CopySrc:
        return {VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR, VK_ACCESS_2_TRANSFER_READ_BIT_KHR, 0,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    case ImageUse::Present:
        return {VK_PIPELINE_STAGE_2_NONE_KHR, 0, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    }
    return {};
}

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    uint64_t size = 0;
    BufferUsage usage = BufferUsage::Vertex;
};

struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    Format format = Format::RGBA8_UNorm;
    TextureUsage usage = TextureUsage::Sampled;
    ImageUse use = ImageUse::None; // tracked across command buffers in submission order
};

struct Sampler {
    VkSampler sampler = VK_NULL_HANDLE;
};

struct Shader {
    VkShaderModule module = VK_NULL_HANDLE;
    ShaderStage stage = ShaderStage::Vertex;
    std::vector<ShaderBinding> bindings;
    std::string name;
};

struct Pipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::array<VkDescriptorSetLayout, kMaxBindGroups> setLayouts{};
    std::vector<ShaderBinding> bindings;
    GraphicsPipelineDesc desc; // rebuilt from it when one of its shaders is reloaded (debugName cleared)
    std::string name;
};

struct BindGroup {
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE; // the pool it was allocated from
    uint32_t setIndex = 0;
    std::vector<TextureHandle> sampledTextures;
    struct ImageBinding {
        uint32_t binding;
        TextureHandle texture;
        SamplerHandle sampler;
    };
    std::vector<ImageBinding> images; // rewritten when a texture is recreated
};

struct HostBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr; // persistently mapped, host-coherent
    VkDeviceSize size = 0;
};

struct Frame {
    VkCommandPool pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;
    uint32_t commandBuffersUsed = 0;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    HostBuffer staging;
    VkDeviceSize stagingOffset = 0;
    VkDeviceSize stagingWanted = 0; // bytes staged in this slot's last frame (it grows to fit)
    std::vector<std::function<void()>> deletions;
    VkQueryPool timerPool = VK_NULL_HANDLE; // timestamps; read back once the fence has signalled
    TimerScopes timers;
    bool timerPoolReset = false;
};


// Collects image barriers and issues them in one vkCmdPipelineBarrier2.
class ImageBarriers {
public:
    // Moves a tracked texture to its next use. Consecutive reads need nothing; anything after a
    // write (and any layout change) waits for the earlier use. `discard`: old contents are not needed.
    void use(Texture& texture, ImageUse next, bool discard = false) {
        const ImageUse previous = texture.use;
        if (previous == next && scopeOf(next).writes == 0) {
            return;
        }
        add(texture.image, previous, next, discard);
        texture.use = next;
    }

    // An untracked image (the swapchain's). `srcStages` overrides the previous use's stages, e.g.
    // with the stage a semaphore wait unblocks.
    void add(VkImage image, ImageUse previous, ImageUse next, bool discard = false,
             VkPipelineStageFlags2KHR srcStages = 0) {
        const UseScope from = scopeOf(previous);
        const UseScope to = scopeOf(next);
        VkImageMemoryBarrier2KHR barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR;
        barrier.srcStageMask = srcStages ? srcStages : from.stages;
        barrier.srcAccessMask = from.writes;
        barrier.dstStageMask = to.stages;
        barrier.dstAccessMask = to.access;
        barrier.oldLayout = discard ? VK_IMAGE_LAYOUT_UNDEFINED : from.layout;
        barrier.newLayout = to.layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barriers_.push_back(barrier);
    }

    void flush(VkCommandBuffer cmd) {
        if (barriers_.empty()) {
            return;
        }
        VkDependencyInfoKHR dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
        dependency.imageMemoryBarrierCount = static_cast<uint32_t>(barriers_.size());
        dependency.pImageMemoryBarriers = barriers_.data();
        vkCmdPipelineBarrier2KHR(cmd, &dependency);
        barriers_.clear();
    }

private:
    std::vector<VkImageMemoryBarrier2KHR> barriers_;
};

// Buffer uploads (copies into vertex, index and uniform buffers) are recorded into their own
// command buffer, submitted before the frame. Before the copies: earlier reads of those buffers
// must have finished; after them: the copied data must be visible to vertex input and shaders.
inline void bufferBarrier(VkCommandBuffer cmd, bool beforeCopies) {
    constexpr VkPipelineStageFlags2KHR kReaders = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT_KHR |
                                                  VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR |
                                                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR;
    VkMemoryBarrier2KHR barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR;
    if (beforeCopies) {
        barrier.srcStageMask = kReaders;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
    } else {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
        barrier.dstStageMask = kReaders;
        barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT_KHR | VK_ACCESS_2_INDEX_READ_BIT_KHR |
                                VK_ACCESS_2_UNIFORM_READ_BIT_KHR | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT_KHR;
    }
    VkDependencyInfoKHR dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2KHR(cmd, &dependency);
}

class VulkanDevice final : public Device {
public:
    VulkanDevice() = default;
    ~VulkanDevice() override;

    bool initialize(const DeviceDesc& desc);

    Backend backend() const override { return Backend::Vulkan; }
    const DeviceCaps& caps() const override { return caps_; }

    BufferHandle createBuffer(const BufferDesc& desc, const void* initialData) override;
    TextureHandle createTexture(const TextureDesc& desc) override;
    SamplerHandle createSampler(const SamplerDesc& desc) override;
    ShaderHandle createShader(const ShaderDesc& desc) override;
    PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    bool reloadShader(ShaderHandle shader, const ShaderDesc& desc) override;
    bool recreateTexture(TextureHandle texture, const TextureDesc& desc) override;
    Extent2D textureExtent(TextureHandle texture) const override;
    BindGroupHandle createBindGroup(const BindGroupDesc& desc) override;

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

    bool beginFrame() override;
    TextureHandle swapchainTexture() const override { return virtualSwapchain_; }
    Extent2D swapchainExtent() const override { return swapchainExtent_; }
    Format swapchainFormat() const override { return Format::RGBA8_UNorm; }
    void submit(const CommandList& commands) override;
    void endFrame() override;
    void waitIdle() override;
    std::vector<GpuTiming> gpuTimings() const override { return gpuTimings_; }

private:
    bool createInstance(bool debug);
    bool pickPhysicalDevice();
    bool createLogicalDevice();
    bool createFrames();
    bool createSwapchain(Extent2D desired);
    bool recreateSwapchain();
    bool createImageResources(Texture& texture);
    void destroyImageResources(Texture& texture);

    bool createAllocator();
    void createPipelineCache(); // from disk when it matches this device; an empty one otherwise
    void savePipelineCache();
    bool createDescriptorPool();
    // Device-local, or (host) persistently mapped host-coherent memory, from the allocator.
    bool allocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host, VkBuffer& buffer,
                        VmaAllocation& allocation, void** mapped = nullptr);
    bool createHostBuffer(VkDeviceSize size, HostBuffer& out);
    void destroyHostBuffer(HostBuffer& buffer);

    VkDescriptorSetLayout setLayout(const std::vector<ShaderBinding>& bindings, uint32_t set);
    VkCommandBuffer acquireCommandBuffer();
    void defer(std::function<void()> fn);
    VkPipeline buildPipeline(const GraphicsPipelineDesc& desc, VkShaderModule vertexModule,
                             VkShaderModule fragmentModule, VkPipelineLayout layout);

    // Uploads: in-frame writes go into the pending upload command buffer.
    VkCommandBuffer uploadCommands();
    VkCommandBuffer finishPendingUpload();
    void stage(const void* data, VkDeviceSize size, VkBuffer& buffer, VkDeviceSize& offset);
    void immediate(const std::function<void(VkCommandBuffer)>& record);
    void recordTextureCopy(VkCommandBuffer cmd, Texture& texture, VkBuffer source, VkDeviceSize sourceOffset,
                           const TextureRegion& region, uint32_t rowLength);

    void translate(const CommandList& commands, VkCommandBuffer cmd);

    platform::Window* window_ = nullptr;
    bool vsync_ = true;
    DeviceCaps caps_;
    std::atomic<uint32_t> validationErrors_{0};

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    bool portabilitySubset_ = false;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    uint32_t timestampBits_ = 0;
    double timestampPeriod_ = 0.0; // nanoseconds per tick
    std::vector<GpuTiming> gpuTimings_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    std::string pipelineCachePath_;
    size_t pipelineCacheLoaded_ = 0; // bytes read at startup (0: started empty)
    uint32_t pipelinesCreated_ = 0;
    double pipelineMilliseconds_ = 0.0; // time spent in vkCreateGraphicsPipelines
    uint32_t vendorId_ = 0;
    uint32_t deviceId_ = 0;
    std::array<uint8_t, VK_UUID_SIZE> pipelineCacheUuid_{};
    std::vector<VkDescriptorPool> descriptorPools_; // bind groups come from the last; a new one when it is full
    VkCommandPool immediatePool_ = VK_NULL_HANDLE;
    std::map<std::string, VkDescriptorSetLayout> setLayoutCache_;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages_;
    std::vector<VkSemaphore> renderFinished_;
    Extent2D swapchainExtent_;
    platform::PixelSize lastWindowSize_;
    TextureHandle virtualSwapchain_;
    bool swapchainDirty_ = false;

    std::array<Frame, kFramesInFlight> frames_{};
    uint32_t frameIndex_ = 0;
    uint32_t imageIndex_ = 0;
    bool frameActive_ = false;
    VkCommandBuffer pendingUpload_ = VK_NULL_HANDLE;

    HandlePool<BufferTag, Buffer> buffers_;
    HandlePool<TextureTag, Texture> textures_;
    HandlePool<SamplerTag, Sampler> samplers_;
    HandlePool<ShaderTag, Shader> shaders_;
    HandlePool<PipelineTag, Pipeline> pipelines_;
    HandlePool<BindGroupTag, BindGroup> bindGroups_;
};

} // namespace animal::rhi::vulkan
