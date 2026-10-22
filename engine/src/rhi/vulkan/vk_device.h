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

#include <animal/core/assert.h>
#include <animal/core/log.h>
#include <animal/platform/window.h>
#include <animal/rhi/command_list.h>
#include <animal/rhi/gpu_timers.h>
#include <animal/rhi/rhi.h>

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
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
constexpr VkDeviceSize kStagingBytesPerFrame = 8ull * 1024 * 1024;

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

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint64_t size = 0;
    BufferUsage usage = BufferUsage::Vertex;
};

struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    Format format = Format::RGBA8_UNorm;
    TextureUsage usage = TextureUsage::Sampled;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
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
};

struct BindGroup {
    VkDescriptorSet set = VK_NULL_HANDLE;
    uint32_t setIndex = 0;
    std::vector<TextureHandle> sampledTextures;
};

struct HostBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
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
    std::vector<std::function<void()>> deletions;
    VkQueryPool timerPool = VK_NULL_HANDLE; // timestamps; read back once the fence has signalled
    TimerScopes timers;
    bool timerPoolReset = false;
};


class ImageBarriers {
public:
    void add(Texture& texture, VkImageLayout newLayout, bool discardContents = false) {
        if (texture.layout == newLayout) {
            return;
        }
        add(texture.image, discardContents ? VK_IMAGE_LAYOUT_UNDEFINED : texture.layout, newLayout);
        texture.layout = newLayout;
    }

    void add(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
        VkImageMemoryBarrier2KHR barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;
        barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT_KHR | VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
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

// Coarse full memory barrier; enough for upload ordering against earlier submissions.
inline void memoryBarrier(VkCommandBuffer cmd) {
    VkMemoryBarrier2KHR barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT_KHR | VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
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

    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const;
    bool allocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                        VkBuffer& buffer, VkDeviceMemory& memory);
    bool createHostBuffer(VkDeviceSize size, HostBuffer& out);
    void destroyHostBuffer(HostBuffer& buffer);

    VkDescriptorSetLayout setLayout(const std::vector<ShaderBinding>& bindings, uint32_t set);
    VkCommandBuffer acquireCommandBuffer();
    void defer(std::function<void()> fn);

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
    VkPhysicalDeviceMemoryProperties memoryProperties_{};
    bool portabilitySubset_ = false;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    uint32_t timestampBits_ = 0;
    double timestampPeriod_ = 0.0; // nanoseconds per tick
    std::vector<GpuTiming> gpuTimings_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
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
