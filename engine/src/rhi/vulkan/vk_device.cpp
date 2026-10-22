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

namespace animal::rhi {

namespace vulkan {
namespace {

constexpr uint32_t kFramesInFlight = 2;
constexpr VkDeviceSize kStagingBytesPerFrame = 8ull * 1024 * 1024;

const char* resultName(VkResult result) {
    switch (result) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    default:
        return "VkResult error";
    }
}

bool check(VkResult result, const char* what) {
    if (result == VK_SUCCESS) {
        return true;
    }
    log::error("Vulkan: {} failed ({}, {})", what, resultName(result), static_cast<int>(result));
    return false;
}

const char* nameOr(const char* name, const char* fallback) {
    return name ? name : fallback;
}

VkFormat toVk(Format format) {
    switch (format) {
    case Format::RGBA8_UNorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::R8_UNorm:
        return VK_FORMAT_R8_UNORM;
    case Format::RGBA16_Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::Undefined:
        break;
    }
    return VK_FORMAT_UNDEFINED;
}

VkFormat toVk(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float2:
        return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::UNorm8x4:
        return VK_FORMAT_R8G8B8A8_UNORM;
    }
    return VK_FORMAT_UNDEFINED;
}

VkBlendFactor toVk(BlendFactor factor) {
    switch (factor) {
    case BlendFactor::Zero:
        return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One:
        return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SrcColor:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::OneMinusSrcColor:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::SrcAlpha:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstColor:
        return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::OneMinusDstColor:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case BlendFactor::DstAlpha:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::OneMinusDstAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    return VK_BLEND_FACTOR_ONE;
}

VkBlendOp toVk(BlendOp op) {
    switch (op) {
    case BlendOp::Add:
        return VK_BLEND_OP_ADD;
    case BlendOp::Subtract:
        return VK_BLEND_OP_SUBTRACT;
    case BlendOp::ReverseSubtract:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendOp::Min:
        return VK_BLEND_OP_MIN;
    case BlendOp::Max:
        return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

VkPrimitiveTopology toVk(PrimitiveTopology topology) {
    switch (topology) {
    case PrimitiveTopology::TriangleList:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveTopology::TriangleStrip:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveTopology::LineList:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkCullModeFlags toVk(CullMode mode) {
    switch (mode) {
    case CullMode::None:
        return VK_CULL_MODE_NONE;
    case CullMode::Front:
        return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back:
        return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

VkAttachmentLoadOp toVk(LoadOp op) {
    switch (op) {
    case LoadOp::Load:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case LoadOp::Clear:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadOp::DontCare:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_LOAD;
}

VkDescriptorType toVk(BindingKind kind) {
    return kind == BindingKind::UniformBuffer ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                              : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}

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
};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ++*static_cast<std::atomic<uint32_t>*>(userData);
        log::error("Vulkan validation: {}", data->pMessage);
    } else {
        log::warn("Vulkan validation: {}", data->pMessage);
    }
    return VK_FALSE;
}

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
void memoryBarrier(VkCommandBuffer cmd) {
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

// --- Initialization ------------------------------------------------------------

bool VulkanDevice::initialize(const DeviceDesc& desc) {
    window_ = desc.window;
    vsync_ = desc.vsync;

    auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (!getInstanceProcAddr) {
        log::error("Vulkan: no vkGetInstanceProcAddr ({})", SDL_GetError());
        return false;
    }
    volkInitializeCustom(getInstanceProcAddr);

    if (!createInstance(desc.debug)) {
        return false;
    }
    if (!SDL_Vulkan_CreateSurface(window_->sdlWindow(), instance_, nullptr, &surface_)) {
        log::error("Vulkan: SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        return false;
    }
    if (!pickPhysicalDevice() || !createLogicalDevice() || !createFrames()) {
        return false;
    }

    std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096},
    }};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 4096;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (!check(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_), "vkCreateDescriptorPool")) {
        return false;
    }

    VkCommandPoolCreateInfo immediateInfo{};
    immediateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    immediateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    immediateInfo.queueFamilyIndex = queueFamily_;
    if (!check(vkCreateCommandPool(device_, &immediateInfo, nullptr, &immediatePool_), "vkCreateCommandPool")) {
        return false;
    }

    lastWindowSize_ = window_->pixelSize();
    if (!createSwapchain({lastWindowSize_.width, lastWindowSize_.height})) {
        return false;
    }

    TextureDesc swapDesc;
    swapDesc.width = swapchainExtent_.width;
    swapDesc.height = swapchainExtent_.height;
    swapDesc.format = Format::RGBA8_UNorm;
    swapDesc.usage = TextureUsage::RenderTarget;
    swapDesc.debugName = "swapchain";
    virtualSwapchain_ = createTexture(swapDesc);
    if (!virtualSwapchain_) {
        return false;
    }

    log::info("Vulkan device: {} | {} | max texture {} | UBO alignment {} | frames in flight {}", caps_.deviceName,
              caps_.apiVersion, caps_.maxTextureSize, caps_.uniformBufferAlignment, caps_.framesInFlight);
    return true;
}

bool VulkanDevice::createInstance(bool debug) {
    uint32_t sdlCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlCount);
    std::vector<const char*> extensions(sdlExtensions, sdlExtensions + sdlCount);

    uint32_t availableCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr);
    std::vector<VkExtensionProperties> available(availableCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, available.data());
    auto hasExtension = [&available](const char* name) {
        return std::any_of(available.begin(), available.end(),
                           [name](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
    };
    auto addExtension = [&extensions](const char* name) {
        if (std::none_of(extensions.begin(), extensions.end(),
                         [name](const char* e) { return std::strcmp(e, name) == 0; })) {
            extensions.push_back(name);
        }
    };

    VkInstanceCreateFlags flags = 0;
    if (hasExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        addExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    const bool debugUtils = debug && hasExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (debugUtils) {
        addExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char*> layers;
    const char* validation = "VK_LAYER_KHRONOS_validation";
    bool layerSettings = false;
    if (debug) {
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        if (std::any_of(availableLayers.begin(), availableLayers.end(), [validation](const VkLayerProperties& l) {
                return std::strcmp(l.layerName, validation) == 0;
            })) {
            layers.push_back(validation);
            uint32_t layerExtCount = 0;
            vkEnumerateInstanceExtensionProperties(validation, &layerExtCount, nullptr);
            std::vector<VkExtensionProperties> layerExts(layerExtCount);
            vkEnumerateInstanceExtensionProperties(validation, &layerExtCount, layerExts.data());
            layerSettings = std::any_of(layerExts.begin(), layerExts.end(), [](const VkExtensionProperties& e) {
                return std::strcmp(e.extensionName, VK_EXT_LAYER_SETTINGS_EXTENSION_NAME) == 0;
            });
            if (layerSettings) {
                addExtension(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
            }
        } else {
            log::warn("Vulkan: validation layer not found; running without validation");
        }
    }

    // Synchronization validation catches missing barriers between passes and uploads.
    const VkBool32 enabled = VK_TRUE;
    const VkLayerSettingEXT settings[] = {{validation, "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &enabled}};
    VkLayerSettingsCreateInfoEXT settingsInfo{};
    settingsInfo.sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT;
    settingsInfo.settingCount = 1;
    settingsInfo.pSettings = settings;

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Animal Engine";
    app.pEngineName = "Animal Engine";
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pNext = layerSettings ? &settingsInfo : nullptr;
    info.flags = flags;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    if (!check(vkCreateInstance(&info, nullptr, &instance_), "vkCreateInstance")) {
        return false;
    }
    volkLoadInstance(instance_);

    if (debugUtils) {
        VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
        messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messengerInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messengerInfo.pfnUserCallback = debugCallback;
        messengerInfo.pUserData = &validationErrors_;
        check(vkCreateDebugUtilsMessengerEXT(instance_, &messengerInfo, nullptr, &messenger_),
              "vkCreateDebugUtilsMessengerEXT");
    }
    log::info("Vulkan instance: {} extension(s), validation {}", extensions.size(),
              layers.empty() ? "off" : (layerSettings ? "on (+sync)" : "on"));
    return true;
}

bool VulkanDevice::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    const char* required[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
                              VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};

    int bestScore = -1;
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(candidate, &props);
        if (props.apiVersion < VK_API_VERSION_1_2) {
            continue;
        }

        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extCount, exts.data());
        auto has = [&exts](const char* name) {
            return std::any_of(exts.begin(), exts.end(),
                               [name](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
        };
        if (!std::all_of(std::begin(required), std::end(required), has)) {
            continue;
        }

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        for (uint32_t i = 0; i < familyCount; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface_, &present);
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
                if (score > bestScore) {
                    bestScore = score;
                    physical_ = candidate;
                    queueFamily_ = i;
                    portabilitySubset_ = has("VK_KHR_portability_subset");
                }
                break;
            }
        }
    }

    if (!physical_) {
        log::error("Vulkan: no GPU supports Vulkan 1.2 with swapchain, dynamic rendering and synchronization2");
        return false;
    }

    VkPhysicalDeviceDriverProperties driver{};
    driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &driver;
    vkGetPhysicalDeviceProperties2(physical_, &props2);
    vkGetPhysicalDeviceMemoryProperties(physical_, &memoryProperties_);

    const VkPhysicalDeviceProperties& props = props2.properties;
    caps_.deviceName = props.deviceName;
    caps_.apiVersion = std::format("Vulkan {}.{}.{} ({} {})", VK_API_VERSION_MAJOR(props.apiVersion),
                                   VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion),
                                   driver.driverName, driver.driverInfo);
    caps_.maxTextureSize = props.limits.maxImageDimension2D;
    caps_.uniformBufferAlignment = static_cast<uint32_t>(props.limits.minUniformBufferOffsetAlignment);
    caps_.framesInFlight = kFramesInFlight;
    caps_.computeShaders = true;
    caps_.firstInstance = true;
    return true;
}

bool VulkanDevice::createLogicalDevice() {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    std::vector<const char*> extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
                                           VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};
    if (portabilitySubset_) {
        extensions.push_back("VK_KHR_portability_subset");
    }

    VkPhysicalDeviceSynchronization2FeaturesKHR sync2{};
    sync2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
    sync2.synchronization2 = VK_TRUE;
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRendering{};
    dynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    dynamicRendering.dynamicRendering = VK_TRUE;
    dynamicRendering.pNext = &sync2;

    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.pNext = &dynamicRendering;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    if (!check(vkCreateDevice(physical_, &info, nullptr, &device_), "vkCreateDevice")) {
        return false;
    }
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    return true;
}

bool VulkanDevice::createFrames() {
    for (Frame& frame : frames_) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamily_;
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (!check(vkCreateCommandPool(device_, &poolInfo, nullptr, &frame.pool), "vkCreateCommandPool") ||
            !check(vkCreateFence(device_, &fenceInfo, nullptr, &frame.fence), "vkCreateFence") ||
            !check(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &frame.imageAvailable), "vkCreateSemaphore") ||
            !createHostBuffer(kStagingBytesPerFrame, frame.staging)) {
            return false;
        }
    }
    return true;
}

bool VulkanDevice::createSwapchain(Extent2D desired) {
    VkSurfaceCapabilitiesKHR surfaceCaps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &surfaceCaps);

    VkExtent2D extent = surfaceCaps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = std::clamp(desired.width, surfaceCaps.minImageExtent.width, surfaceCaps.maxImageExtent.width);
        extent.height =
            std::clamp(desired.height, surfaceCaps.minImageExtent.height, surfaceCaps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) {
        return false;
    }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &formatCount, formats.data());
    VkSurfaceFormatKHR surfaceFormat = formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
    for (const VkSurfaceFormatKHR& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) {
            surfaceFormat = f;
            break;
        }
    }

    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &modeCount, modes.data());
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync_) {
        for (VkPresentModeKHR preferred : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
            if (std::find(modes.begin(), modes.end(), preferred) != modes.end()) {
                presentMode = preferred;
                break;
            }
        }
    }

    uint32_t imageCount = std::max(surfaceCaps.minImageCount + 1, 2u);
    if (surfaceCaps.maxImageCount > 0) {
        imageCount = std::min(imageCount, surfaceCaps.maxImageCount);
    }
    if (!(surfaceCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        log::error("Vulkan: swapchain images cannot be a transfer destination");
        return false;
    }
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(surfaceCaps.supportedCompositeAlpha & compositeAlpha)) {
        for (uint32_t bit = 1; bit != 0; bit <<= 1) {
            if (surfaceCaps.supportedCompositeAlpha & bit) {
                compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(bit);
                break;
            }
        }
    }

    VkSwapchainKHR oldSwapchain = swapchain_;
    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface_;
    info.minImageCount = imageCount;
    info.imageFormat = surfaceFormat.format;
    info.imageColorSpace = surfaceFormat.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = surfaceCaps.currentTransform;
    info.compositeAlpha = compositeAlpha;
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = oldSwapchain;
    if (!check(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "vkCreateSwapchainKHR")) {
        swapchain_ = oldSwapchain;
        return false;
    }
    if (oldSwapchain) {
        vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
    }

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    swapchainImages_.resize(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, swapchainImages_.data());

    for (VkSemaphore semaphore : renderFinished_) {
        vkDestroySemaphore(device_, semaphore, nullptr);
    }
    renderFinished_.assign(count, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (VkSemaphore& semaphore : renderFinished_) {
        vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &semaphore);
    }

    swapchainExtent_ = {extent.width, extent.height};
    return true;
}

bool VulkanDevice::recreateSwapchain() {
    vkDeviceWaitIdle(device_);
    const platform::PixelSize size = window_->pixelSize();
    if (!createSwapchain({size.width, size.height})) {
        return false;
    }
    lastWindowSize_ = size;
    swapchainDirty_ = false;

    Texture* texture = textures_.get(virtualSwapchain_);
    ANIMAL_ASSERT(texture != nullptr);
    destroyImageResources(*texture);
    texture->width = swapchainExtent_.width;
    texture->height = swapchainExtent_.height;
    texture->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    log::debug("Vulkan swapchain recreated: {}x{}", swapchainExtent_.width, swapchainExtent_.height);
    return createImageResources(*texture);
}

VulkanDevice::~VulkanDevice() {
    if (device_) {
        vkDeviceWaitIdle(device_);
        for (Frame& frame : frames_) {
            for (auto& fn : frame.deletions) {
                fn();
            }
            frame.deletions.clear();
        }
        bindGroups_.forEach([this](BindGroup& g) { vkFreeDescriptorSets(device_, descriptorPool_, 1, &g.set); });
        pipelines_.forEach([this](Pipeline& p) {
            vkDestroyPipeline(device_, p.pipeline, nullptr);
            vkDestroyPipelineLayout(device_, p.layout, nullptr);
        });
        shaders_.forEach([this](Shader& s) { vkDestroyShaderModule(device_, s.module, nullptr); });
        samplers_.forEach([this](Sampler& s) { vkDestroySampler(device_, s.sampler, nullptr); });
        textures_.forEach([this](Texture& t) { destroyImageResources(t); });
        buffers_.forEach([this](Buffer& b) {
            vkDestroyBuffer(device_, b.buffer, nullptr);
            vkFreeMemory(device_, b.memory, nullptr);
        });
        for (auto& [signature, layout] : setLayoutCache_) {
            vkDestroyDescriptorSetLayout(device_, layout, nullptr);
        }
        for (Frame& frame : frames_) {
            destroyHostBuffer(frame.staging);
            vkDestroySemaphore(device_, frame.imageAvailable, nullptr);
            vkDestroyFence(device_, frame.fence, nullptr);
            vkDestroyCommandPool(device_, frame.pool, nullptr);
        }
        for (VkSemaphore semaphore : renderFinished_) {
            vkDestroySemaphore(device_, semaphore, nullptr);
        }
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        vkDestroyCommandPool(device_, immediatePool_, nullptr);
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    if (messenger_) {
        vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
    }
    if (instance_) {
        vkDestroyInstance(instance_, nullptr);
        log::info("Vulkan shutdown: {} validation error(s)", validationErrors_.load());
    }
}

// --- Memory helpers --------------------------------------------------------------

uint32_t VulkanDevice::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const {
    for (uint32_t i = 0; i < memoryProperties_.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (memoryProperties_.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool VulkanDevice::allocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                                  VkBuffer& buffer, VkDeviceMemory& memory) {
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!check(vkCreateBuffer(device_, &info, nullptr, &buffer), "vkCreateBuffer")) {
        return false;
    }
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device_, buffer, &requirements);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties);
    if (alloc.memoryTypeIndex == UINT32_MAX ||
        !check(vkAllocateMemory(device_, &alloc, nullptr, &memory), "vkAllocateMemory (buffer)")) {
        vkDestroyBuffer(device_, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(device_, buffer, memory, 0);
    return true;
}

bool VulkanDevice::createHostBuffer(VkDeviceSize size, HostBuffer& out) {
    out.size = size;
    if (!allocateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, out.buffer,
                        out.memory)) {
        return false;
    }
    return check(vkMapMemory(device_, out.memory, 0, size, 0, &out.mapped), "vkMapMemory");
}

void VulkanDevice::destroyHostBuffer(HostBuffer& buffer) {
    if (buffer.memory) {
        vkUnmapMemory(device_, buffer.memory);
        vkFreeMemory(device_, buffer.memory, nullptr);
    }
    if (buffer.buffer) {
        vkDestroyBuffer(device_, buffer.buffer, nullptr);
    }
    buffer = {};
}

void VulkanDevice::defer(std::function<void()> fn) {
    frames_[frameIndex_].deletions.push_back(std::move(fn));
}

VkCommandBuffer VulkanDevice::acquireCommandBuffer() {
    Frame& frame = frames_[frameIndex_];
    if (frame.commandBuffersUsed == frame.commandBuffers.size()) {
        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = frame.pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        check(vkAllocateCommandBuffers(device_, &alloc, &cmd), "vkAllocateCommandBuffers");
        frame.commandBuffers.push_back(cmd);
    }
    VkCommandBuffer cmd = frame.commandBuffers[frame.commandBuffersUsed++];
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    return cmd;
}

// --- Uploads ------------------------------------------------------------------

VkCommandBuffer VulkanDevice::uploadCommands() {
    if (!pendingUpload_) {
        pendingUpload_ = acquireCommandBuffer();
        memoryBarrier(pendingUpload_);
    }
    return pendingUpload_;
}

VkCommandBuffer VulkanDevice::finishPendingUpload() {
    VkCommandBuffer cmd = pendingUpload_;
    if (cmd) {
        memoryBarrier(cmd);
        vkEndCommandBuffer(cmd);
        pendingUpload_ = VK_NULL_HANDLE;
    }
    return cmd;
}

void VulkanDevice::stage(const void* data, VkDeviceSize size, VkBuffer& buffer, VkDeviceSize& offset) {
    Frame& frame = frames_[frameIndex_];
    const VkDeviceSize aligned = (frame.stagingOffset + 15) & ~VkDeviceSize{15};
    if (aligned + size <= frame.staging.size) {
        std::memcpy(static_cast<uint8_t*>(frame.staging.mapped) + aligned, data, size);
        frame.stagingOffset = aligned + size;
        buffer = frame.staging.buffer;
        offset = aligned;
        return;
    }
    // Frame staging exhausted: a dedicated buffer, released once this frame retires.
    HostBuffer overflow;
    createHostBuffer(size, overflow);
    std::memcpy(overflow.mapped, data, size);
    buffer = overflow.buffer;
    offset = 0;
    defer([this, overflow]() mutable { destroyHostBuffer(overflow); });
}

void VulkanDevice::immediate(const std::function<void(VkCommandBuffer)>& record) {
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = immediatePool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device_, &alloc, &cmd);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    memoryBarrier(cmd);
    record(cmd);
    memoryBarrier(cmd);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    check(vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit (immediate)");
    vkQueueWaitIdle(queue_);
    vkFreeCommandBuffers(device_, immediatePool_, 1, &cmd);
}

void VulkanDevice::recordTextureCopy(VkCommandBuffer cmd, Texture& texture, VkBuffer source,
                                     VkDeviceSize sourceOffset, const TextureRegion& region, uint32_t rowLength) {
    ImageBarriers barriers;
    const bool whole = region.x == 0 && region.y == 0 && region.width == texture.width &&
                       region.height == texture.height;
    barriers.add(texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, whole);
    barriers.flush(cmd);

    VkBufferImageCopy copy{};
    copy.bufferOffset = sourceOffset;
    copy.bufferRowLength = rowLength;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageOffset = {static_cast<int32_t>(region.x), static_cast<int32_t>(region.y), 0};
    copy.imageExtent = {region.width, region.height, 1};
    vkCmdCopyBufferToImage(cmd, source, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    if (hasFlag(texture.usage, TextureUsage::Sampled)) {
        barriers.add(texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        barriers.flush(cmd);
    }
}

void VulkanDevice::writeBuffer(BufferHandle handle, uint64_t offset, const void* data, uint64_t size) {
    const Buffer* buffer = buffers_.get(handle);
    if (!buffer || offset + size > buffer->size) {
        log::error("writeBuffer: invalid buffer or range [{}, {})", offset, offset + size);
        return;
    }
    if (size == 0) {
        return;
    }
    const VkBuffer target = buffer->buffer;
    if (frameActive_) {
        VkBuffer source = VK_NULL_HANDLE;
        VkDeviceSize sourceOffset = 0;
        stage(data, size, source, sourceOffset);
        const VkBufferCopy copy{sourceOffset, offset, size};
        vkCmdCopyBuffer(uploadCommands(), source, target, 1, &copy);
        return;
    }
    HostBuffer temp;
    if (!createHostBuffer(size, temp)) {
        return;
    }
    std::memcpy(temp.mapped, data, size);
    immediate([&](VkCommandBuffer cmd) {
        const VkBufferCopy copy{0, offset, size};
        vkCmdCopyBuffer(cmd, temp.buffer, target, 1, &copy);
    });
    destroyHostBuffer(temp);
}

void VulkanDevice::writeTexture(TextureHandle handle, const TextureRegion& region, const void* pixels,
                                uint32_t bytesPerRow) {
    Texture* texture = textures_.get(handle);
    if (!texture) {
        log::error("writeTexture: invalid texture");
        return;
    }
    const uint32_t bpp = bytesPerPixel(texture->format);
    if (region.x + region.width > texture->width || region.y + region.height > texture->height ||
        bytesPerRow % bpp != 0 || bytesPerRow / bpp < region.width) {
        log::error("writeTexture: region or row pitch out of range");
        return;
    }
    const VkDeviceSize size = VkDeviceSize{bytesPerRow} * (region.height - 1) + VkDeviceSize{region.width} * bpp;
    const uint32_t rowLength = bytesPerRow / bpp;

    if (frameActive_) {
        VkBuffer source = VK_NULL_HANDLE;
        VkDeviceSize sourceOffset = 0;
        stage(pixels, size, source, sourceOffset);
        recordTextureCopy(uploadCommands(), *texture, source, sourceOffset, region, rowLength);
        return;
    }
    HostBuffer temp;
    if (!createHostBuffer(size, temp)) {
        return;
    }
    std::memcpy(temp.mapped, pixels, size);
    immediate([&](VkCommandBuffer cmd) { recordTextureCopy(cmd, *texture, temp.buffer, 0, region, rowLength); });
    destroyHostBuffer(temp);
}

std::vector<uint8_t> VulkanDevice::readTexture(TextureHandle handle) {
    Texture* texture = textures_.get(handle);
    if (!texture) {
        log::error("readTexture: invalid texture");
        return {};
    }
    // Recorded-but-unsubmitted uploads already changed the tracked layouts; submit them first.
    if (VkCommandBuffer upload = finishPendingUpload()) {
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &upload;
        check(vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit (upload)");
    }
    vkQueueWaitIdle(queue_);

    const VkDeviceSize size = VkDeviceSize{texture->width} * texture->height * bytesPerPixel(texture->format);
    HostBuffer temp;
    if (!createHostBuffer(size, temp)) {
        return {};
    }
    immediate([&](VkCommandBuffer cmd) {
        const VkImageLayout previous = texture->layout;
        ImageBarriers barriers;
        barriers.add(*texture, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        barriers.flush(cmd);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {texture->width, texture->height, 1};
        vkCmdCopyImageToBuffer(cmd, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, temp.buffer, 1, &copy);
        if (previous != VK_IMAGE_LAYOUT_UNDEFINED) {
            barriers.add(*texture, previous);
            barriers.flush(cmd);
        }
    });
    std::vector<uint8_t> pixels(size);
    std::memcpy(pixels.data(), temp.mapped, size);
    destroyHostBuffer(temp);
    return pixels;
}

// --- Resource creation -------------------------------------------------------------

BufferHandle VulkanDevice::createBuffer(const BufferDesc& desc, const void* initialData) {
    if (desc.size == 0) {
        log::error("createBuffer '{}': size must be non-zero", nameOr(desc.debugName, "unnamed"));
        return {};
    }
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (hasFlag(desc.usage, BufferUsage::Vertex)) {
        usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (hasFlag(desc.usage, BufferUsage::Index)) {
        usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (hasFlag(desc.usage, BufferUsage::Uniform)) {
        usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    Buffer buffer;
    buffer.size = desc.size;
    buffer.usage = desc.usage;
    if (!allocateBuffer(desc.size, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer.buffer, buffer.memory)) {
        return {};
    }
    const BufferHandle handle = buffers_.insert(buffer);
    if (initialData) {
        writeBuffer(handle, 0, initialData, desc.size);
    }
    return handle;
}

bool VulkanDevice::createImageResources(Texture& texture) {
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (hasFlag(texture.usage, TextureUsage::Sampled)) {
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (hasFlag(texture.usage, TextureUsage::RenderTarget)) {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = toVk(texture.format);
    info.extent = {texture.width, texture.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!check(vkCreateImage(device_, &info, nullptr, &texture.image), "vkCreateImage")) {
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device_, texture.image, &requirements);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (alloc.memoryTypeIndex == UINT32_MAX ||
        !check(vkAllocateMemory(device_, &alloc, nullptr, &texture.memory), "vkAllocateMemory (image)")) {
        destroyImageResources(texture);
        return false;
    }
    vkBindImageMemory(device_, texture.image, texture.memory, 0);

    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = texture.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = info.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!check(vkCreateImageView(device_, &view, nullptr, &texture.view), "vkCreateImageView")) {
        destroyImageResources(texture);
        return false;
    }
    texture.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

void VulkanDevice::destroyImageResources(Texture& texture) {
    if (texture.view) {
        vkDestroyImageView(device_, texture.view, nullptr);
    }
    if (texture.image) {
        vkDestroyImage(device_, texture.image, nullptr);
    }
    if (texture.memory) {
        vkFreeMemory(device_, texture.memory, nullptr);
    }
    texture.view = VK_NULL_HANDLE;
    texture.image = VK_NULL_HANDLE;
    texture.memory = VK_NULL_HANDLE;
}

TextureHandle VulkanDevice::createTexture(const TextureDesc& desc) {
    const char* name = nameOr(desc.debugName, "unnamed");
    if (desc.width == 0 || desc.height == 0 || desc.width > caps_.maxTextureSize ||
        desc.height > caps_.maxTextureSize) {
        log::error("createTexture '{}': invalid size {}x{}", name, desc.width, desc.height);
        return {};
    }
    if (toVk(desc.format) == VK_FORMAT_UNDEFINED) {
        log::error("createTexture '{}': unsupported format", name);
        return {};
    }
    Texture texture;
    texture.width = desc.width;
    texture.height = desc.height;
    texture.format = desc.format;
    texture.usage = desc.usage;
    if (!createImageResources(texture)) {
        log::error("createTexture '{}' failed", name);
        return {};
    }
    return textures_.insert(texture);
}

SamplerHandle VulkanDevice::createSampler(const SamplerDesc& desc) {
    auto filter = [](Filter f) { return f == Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR; };
    auto address = [](AddressMode m) {
        return m == AddressMode::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    };
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = filter(desc.magFilter);
    info.minFilter = filter(desc.minFilter);
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = address(desc.addressU);
    info.addressModeV = address(desc.addressV);
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = 0.0f;
    Sampler sampler;
    if (!check(vkCreateSampler(device_, &info, nullptr, &sampler.sampler), "vkCreateSampler")) {
        return {};
    }
    return samplers_.insert(sampler);
}

ShaderHandle VulkanDevice::createShader(const ShaderDesc& desc) {
    const char* name = nameOr(desc.debugName, "unnamed");
    if (!desc.package || desc.package->spirv.empty()) {
        log::error("createShader '{}': package has no SPIR-V code", name);
        return {};
    }
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = desc.package->spirv.size() * sizeof(uint32_t);
    info.pCode = desc.package->spirv.data();
    Shader shader;
    if (!check(vkCreateShaderModule(device_, &info, nullptr, &shader.module), "vkCreateShaderModule")) {
        return {};
    }
    shader.stage = desc.package->stage;
    shader.bindings = desc.package->bindings;
    shader.name = name;
    return shaders_.insert(std::move(shader));
}

VkDescriptorSetLayout VulkanDevice::setLayout(const std::vector<ShaderBinding>& bindings, uint32_t set) {
    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    std::string signature;
    for (const ShaderBinding& b : bindings) {
        if (b.set != set) {
            continue;
        }
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = b.binding;
        binding.descriptorType = toVk(b.kind);
        binding.descriptorCount = 1;
        // Same stage flags everywhere so identical sets are compatible across pipelines.
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        vkBindings.push_back(binding);
    }
    std::sort(vkBindings.begin(), vkBindings.end(),
              [](const auto& a, const auto& b) { return a.binding < b.binding; });
    for (const auto& b : vkBindings) {
        signature += std::format("{}:{};", b.binding, static_cast<int>(b.descriptorType));
    }

    if (auto it = setLayoutCache_.find(signature); it != setLayoutCache_.end()) {
        return it->second;
    }
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(vkBindings.size());
    info.pBindings = vkBindings.data();
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (!check(vkCreateDescriptorSetLayout(device_, &info, nullptr, &layout), "vkCreateDescriptorSetLayout")) {
        return VK_NULL_HANDLE;
    }
    setLayoutCache_.emplace(signature, layout);
    return layout;
}

PipelineHandle VulkanDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    const char* name = nameOr(desc.debugName, "unnamed");
    const Shader* vs = shaders_.get(desc.vertexShader);
    const Shader* fs = shaders_.get(desc.fragmentShader);
    if (!vs || !fs || vs->stage != ShaderStage::Vertex || fs->stage != ShaderStage::Fragment) {
        log::error("createGraphicsPipeline '{}': needs a valid vertex and fragment shader", name);
        return {};
    }
    if (desc.colorTargets.empty() || desc.colorTargets.size() > kMaxColorAttachments) {
        log::error("createGraphicsPipeline '{}': needs 1..{} color targets", name, kMaxColorAttachments);
        return {};
    }

    std::vector<ShaderBinding> bindings = vs->bindings;
    for (const ShaderBinding& b : fs->bindings) {
        auto existing = std::find_if(bindings.begin(), bindings.end(), [&b](const ShaderBinding& e) {
            return e.set == b.set && e.binding == b.binding;
        });
        if (existing == bindings.end()) {
            bindings.push_back(b);
        } else if (existing->kind != b.kind) {
            log::error("createGraphicsPipeline '{}': set {} binding {} has different types per stage", name, b.set,
                       b.binding);
            return {};
        }
    }

    Pipeline pipeline;
    pipeline.bindings = bindings;
    uint32_t setCount = 0;
    for (const ShaderBinding& b : bindings) {
        setCount = std::max(setCount, b.set + 1);
    }
    for (uint32_t s = 0; s < setCount; ++s) {
        pipeline.setLayouts[s] = setLayout(bindings, s);
        if (!pipeline.setLayouts[s]) {
            return {};
        }
    }

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = setCount;
    layoutInfo.pSetLayouts = pipeline.setLayouts.data();
    if (!check(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipeline.layout), "vkCreatePipelineLayout")) {
        return {};
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs->module;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs->module;
    stages[1].pName = "main";

    std::vector<VkVertexInputBindingDescription> vertexBindings;
    for (uint32_t i = 0; i < desc.vertexBuffers.size(); ++i) {
        vertexBindings.push_back({i, desc.vertexBuffers[i].stride,
                                  desc.vertexBuffers[i].stepMode == VertexStepMode::Instance
                                      ? VK_VERTEX_INPUT_RATE_INSTANCE
                                      : VK_VERTEX_INPUT_RATE_VERTEX});
    }
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    for (const VertexAttribute& a : desc.vertexAttributes) {
        vertexAttributes.push_back({a.location, a.bufferSlot, toVk(a.format), a.offset});
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindings.size());
    vertexInput.pVertexBindingDescriptions = vertexBindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
    vertexInput.pVertexAttributeDescriptions = vertexAttributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = toVk(desc.topology);

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = toVk(desc.cullMode);
    raster.frontFace = desc.frontFace == FrontFace::CounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                                                      : VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments;
    std::vector<VkFormat> colorFormats;
    for (const ColorTargetDesc& target : desc.colorTargets) {
        VkPipelineColorBlendAttachmentState blend{};
        blend.blendEnable = target.blend.enabled ? VK_TRUE : VK_FALSE;
        blend.srcColorBlendFactor = toVk(target.blend.srcColor);
        blend.dstColorBlendFactor = toVk(target.blend.dstColor);
        blend.colorBlendOp = toVk(target.blend.colorOp);
        blend.srcAlphaBlendFactor = toVk(target.blend.srcAlpha);
        blend.dstAlphaBlendFactor = toVk(target.blend.dstAlpha);
        blend.alphaBlendOp = toVk(target.blend.alphaOp);
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                               VK_COLOR_COMPONENT_A_BIT;
        blendAttachments.push_back(blend);
        colorFormats.push_back(toVk(target.format));
    }
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    colorBlend.pAttachments = blendAttachments.data();

    const std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    VkPipelineRenderingCreateInfoKHR rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    rendering.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
    rendering.pColorAttachmentFormats = colorFormats.data();

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = static_cast<uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewportState;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &colorBlend;
    info.pDynamicState = &dynamic;
    info.layout = pipeline.layout;
    if (!check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline.pipeline),
               "vkCreateGraphicsPipelines")) {
        vkDestroyPipelineLayout(device_, pipeline.layout, nullptr);
        return {};
    }
    return pipelines_.insert(std::move(pipeline));
}

BindGroupHandle VulkanDevice::createBindGroup(const BindGroupDesc& desc) {
    const char* name = nameOr(desc.debugName, "unnamed");
    const Pipeline* pipeline = pipelines_.get(desc.pipeline);
    if (!pipeline || desc.set >= kMaxBindGroups || !pipeline->setLayouts[desc.set]) {
        log::error("createBindGroup '{}': pipeline has no layout for set {}", name, desc.set);
        return {};
    }

    BindGroup group;
    group.setIndex = desc.set;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    bufferInfos.reserve(desc.entries.size());
    imageInfos.reserve(desc.entries.size());
    std::vector<VkWriteDescriptorSet> writes;
    uint32_t provided = 0;

    for (const BindGroupEntry& entry : desc.entries) {
        auto layout = std::find_if(pipeline->bindings.begin(), pipeline->bindings.end(),
                                   [&](const ShaderBinding& b) { return b.set == desc.set && b.binding == entry.binding; });
        if (layout == pipeline->bindings.end()) {
            log::error("createBindGroup '{}': pipeline has no resource at set {} binding {}", name, desc.set,
                       entry.binding);
            return {};
        }
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = entry.binding;
        write.descriptorCount = 1;
        write.descriptorType = toVk(layout->kind);
        if (layout->kind == BindingKind::UniformBuffer) {
            const Buffer* buffer = buffers_.get(entry.buffer);
            if (!buffer || !hasFlag(buffer->usage, BufferUsage::Uniform) ||
                entry.offset % caps_.uniformBufferAlignment != 0 || entry.offset + entry.size > buffer->size) {
                log::error("createBindGroup '{}': binding {} needs an aligned uniform buffer range", name,
                           entry.binding);
                return {};
            }
            bufferInfos.push_back({buffer->buffer, entry.offset, entry.size ? entry.size : VK_WHOLE_SIZE});
            write.pBufferInfo = &bufferInfos.back();
        } else {
            const Texture* texture = textures_.get(entry.texture);
            const Sampler* sampler = samplers_.get(entry.sampler);
            if (!texture || !hasFlag(texture->usage, TextureUsage::Sampled) || !sampler) {
                log::error("createBindGroup '{}': binding {} needs a sampled texture and a sampler", name,
                           entry.binding);
                return {};
            }
            imageInfos.push_back({sampler->sampler, texture->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            write.pImageInfo = &imageInfos.back();
            group.sampledTextures.push_back(entry.texture);
        }
        writes.push_back(write);
        provided |= 1u << entry.binding;
    }
    for (const ShaderBinding& b : pipeline->bindings) {
        if (b.set == desc.set && !(provided & (1u << b.binding))) {
            log::error("createBindGroup '{}': missing resource for set {} binding {}", name, b.set, b.binding);
            return {};
        }
    }

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = descriptorPool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &pipeline->setLayouts[desc.set];
    if (!check(vkAllocateDescriptorSets(device_, &alloc, &group.set), "vkAllocateDescriptorSets")) {
        return {};
    }
    for (VkWriteDescriptorSet& write : writes) {
        write.dstSet = group.set;
    }
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return bindGroups_.insert(std::move(group));
}

// --- Destruction (deferred until the GPU is done with the current frame slot) ---------------

void VulkanDevice::destroy(BufferHandle handle) {
    if (auto buffer = buffers_.take(handle)) {
        defer([this, b = *buffer] {
            vkDestroyBuffer(device_, b.buffer, nullptr);
            vkFreeMemory(device_, b.memory, nullptr);
        });
    }
}

void VulkanDevice::destroy(TextureHandle handle) {
    if (handle == virtualSwapchain_) {
        log::error("destroy: the swapchain texture is owned by the device");
        return;
    }
    if (auto texture = textures_.take(handle)) {
        defer([this, t = *texture]() mutable { destroyImageResources(t); });
    }
}

void VulkanDevice::destroy(SamplerHandle handle) {
    if (auto sampler = samplers_.take(handle)) {
        defer([this, s = sampler->sampler] { vkDestroySampler(device_, s, nullptr); });
    }
}

void VulkanDevice::destroy(ShaderHandle handle) {
    if (auto shader = shaders_.take(handle)) {
        defer([this, m = shader->module] { vkDestroyShaderModule(device_, m, nullptr); });
    }
}

void VulkanDevice::destroy(PipelineHandle handle) {
    if (auto pipeline = pipelines_.take(handle)) {
        defer([this, p = pipeline->pipeline, l = pipeline->layout] {
            vkDestroyPipeline(device_, p, nullptr);
            vkDestroyPipelineLayout(device_, l, nullptr);
        });
    }
}

void VulkanDevice::destroy(BindGroupHandle handle) {
    if (auto group = bindGroups_.take(handle)) {
        defer([this, s = group->set] { vkFreeDescriptorSets(device_, descriptorPool_, 1, &s); });
    }
}

// --- Frames ---------------------------------------------------------------------

bool VulkanDevice::beginFrame() {
    ANIMAL_ASSERT(!frameActive_);
    Frame& frame = frames_[frameIndex_];
    vkWaitForFences(device_, 1, &frame.fence, VK_TRUE, UINT64_MAX);
    for (auto& fn : frame.deletions) {
        fn();
    }
    frame.deletions.clear();
    vkResetCommandPool(device_, frame.pool, 0);
    frame.commandBuffersUsed = 0;
    frame.stagingOffset = 0;

    const platform::PixelSize size = window_->pixelSize();
    if (size.width == 0 || size.height == 0) {
        return false;
    }
    if (swapchainDirty_ || size.width != lastWindowSize_.width || size.height != lastWindowSize_.height) {
        if (!recreateSwapchain()) {
            return false;
        }
    }

    VkResult result =
        vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex_);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        if (!recreateSwapchain()) {
            return false;
        }
        result =
            vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex_);
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        check(result, "vkAcquireNextImageKHR");
        return false;
    }
    swapchainDirty_ = result == VK_SUBOPTIMAL_KHR;
    vkResetFences(device_, 1, &frame.fence);
    frameActive_ = true;
    return true;
}

void VulkanDevice::submit(const CommandList& commands) {
    ANIMAL_ASSERT(frameActive_);
    ANIMAL_ASSERT(!commands.insideRenderPass());
    std::vector<VkCommandBuffer> buffers;
    if (VkCommandBuffer upload = finishPendingUpload()) {
        buffers.push_back(upload);
    }
    VkCommandBuffer cmd = acquireCommandBuffer();
    translate(commands, cmd);
    vkEndCommandBuffer(cmd);
    buffers.push_back(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = static_cast<uint32_t>(buffers.size());
    submitInfo.pCommandBuffers = buffers.data();
    check(vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit");
}

void VulkanDevice::endFrame() {
    ANIMAL_ASSERT(frameActive_);
    Frame& frame = frames_[frameIndex_];
    std::vector<VkCommandBuffer> buffers;
    if (VkCommandBuffer upload = finishPendingUpload()) {
        buffers.push_back(upload);
    }

    VkCommandBuffer cmd = acquireCommandBuffer();
    Texture* source = textures_.get(virtualSwapchain_);
    const VkImage target = swapchainImages_[imageIndex_];
    ImageBarriers barriers;
    barriers.add(*source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    barriers.add(target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    barriers.flush(cmd);

    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {static_cast<int32_t>(source->width), static_cast<int32_t>(source->height), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[1] = {static_cast<int32_t>(swapchainExtent_.width),
                          static_cast<int32_t>(swapchainExtent_.height), 1};
    vkCmdBlitImage(cmd, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

    barriers.add(target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    barriers.flush(cmd);
    vkEndCommandBuffer(cmd);
    buffers.push_back(cmd);

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = static_cast<uint32_t>(buffers.size());
    submitInfo.pCommandBuffers = buffers.data();
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinished_[imageIndex_];
    check(vkQueueSubmit(queue_, 1, &submitInfo, frame.fence), "vkQueueSubmit (present)");

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished_[imageIndex_];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &imageIndex_;
    const VkResult result = vkQueuePresentKHR(queue_, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        swapchainDirty_ = true;
    } else {
        check(result, "vkQueuePresentKHR");
    }

    frameActive_ = false;
    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
}

void VulkanDevice::waitIdle() {
    if (device_) {
        vkDeviceWaitIdle(device_);
    }
}

// --- Command translation -------------------------------------------------------------

void VulkanDevice::translate(const CommandList& commandList, VkCommandBuffer cmd) {
    const std::vector<Command>& commands = commandList.commands();
    const Pipeline* pipeline = nullptr;
    std::array<const BindGroup*, kMaxBindGroups> boundGroups{};
    uint32_t dirtyGroups = 0;
    bool skipPass = false;

    auto bindDirtyGroups = [&] {
        for (uint32_t set = 0; set < kMaxBindGroups; ++set) {
            if ((dirtyGroups & (1u << set)) && boundGroups[set] && pipeline->setLayouts[set]) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout, set, 1,
                                        &boundGroups[set]->set, 0, nullptr);
            }
        }
        dirtyGroups = 0;
    };

    for (size_t i = 0; i < commands.size(); ++i) {
        const Command& command = commands[i];
        if (const auto* begin = std::get_if<cmd::BeginRenderPass>(&command)) {
            const RenderPassDesc& desc = begin->desc;
            ImageBarriers barriers;

            // Everything sampled inside this pass must be readable before rendering starts.
            for (size_t j = i + 1; j < commands.size() && !std::holds_alternative<cmd::EndRenderPass>(commands[j]);
                 ++j) {
                if (const auto* setGroup = std::get_if<cmd::SetBindGroup>(&commands[j])) {
                    if (const BindGroup* group = bindGroups_.get(setGroup->group)) {
                        for (TextureHandle sampled : group->sampledTextures) {
                            if (Texture* texture = textures_.get(sampled)) {
                                barriers.add(*texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                            }
                        }
                    }
                }
            }

            std::array<VkRenderingAttachmentInfoKHR, kMaxColorAttachments> attachments{};
            Extent2D extent{UINT32_MAX, UINT32_MAX};
            skipPass = false;
            for (uint32_t a = 0; a < desc.colorAttachmentCount; ++a) {
                const ColorAttachment& color = desc.colorAttachments[a];
                Texture* texture = textures_.get(color.texture);
                if (!texture || !hasFlag(texture->usage, TextureUsage::RenderTarget)) {
                    log::error("Vulkan backend: render pass attachment is not a valid render target");
                    skipPass = true;
                    break;
                }
                barriers.add(*texture, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, color.loadOp != LoadOp::Load);
                attachments[a].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
                attachments[a].imageView = texture->view;
                attachments[a].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                attachments[a].loadOp = toVk(color.loadOp);
                attachments[a].storeOp =
                    color.storeOp == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachments[a].clearValue.color = {
                    {color.clearColor.r, color.clearColor.g, color.clearColor.b, color.clearColor.a}};
                extent.width = std::min(extent.width, texture->width);
                extent.height = std::min(extent.height, texture->height);
            }
            barriers.flush(cmd);
            if (skipPass) {
                continue;
            }

            VkRenderingInfoKHR rendering{};
            rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
            rendering.renderArea = {{0, 0}, {extent.width, extent.height}};
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = desc.colorAttachmentCount;
            rendering.pColorAttachments = attachments.data();
            vkCmdBeginRenderingKHR(cmd, &rendering);

            const VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height),
                                      0.0f, 1.0f};
            const VkRect2D scissor{{0, 0}, {extent.width, extent.height}};
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            pipeline = nullptr;
            boundGroups = {};
            dirtyGroups = 0;
            continue;
        }

        if (std::holds_alternative<cmd::EndRenderPass>(command)) {
            if (!skipPass) {
                vkCmdEndRenderingKHR(cmd);
            }
            skipPass = false;
            continue;
        }
        if (skipPass) {
            continue;
        }

        if (const auto* c = std::get_if<cmd::SetPipeline>(&command)) {
            pipeline = pipelines_.get(c->pipeline);
            if (pipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
                for (uint32_t set = 0; set < kMaxBindGroups; ++set) {
                    if (boundGroups[set]) {
                        dirtyGroups |= 1u << set;
                    }
                }
            }
        } else if (const auto* viewport = std::get_if<cmd::SetViewport>(&command)) {
            const Viewport& v = viewport->viewport;
            const VkViewport vk{v.x, v.y, v.width, v.height, v.minDepth, v.maxDepth};
            vkCmdSetViewport(cmd, 0, 1, &vk);
        } else if (const auto* scissor = std::get_if<cmd::SetScissor>(&command)) {
            const VkRect2D rect{{scissor->scissor.x, scissor->scissor.y},
                                {scissor->scissor.width, scissor->scissor.height}};
            vkCmdSetScissor(cmd, 0, 1, &rect);
        } else if (const auto* vb = std::get_if<cmd::SetVertexBuffer>(&command)) {
            if (const Buffer* buffer = buffers_.get(vb->buffer)) {
                const VkDeviceSize offset = vb->offset;
                vkCmdBindVertexBuffers(cmd, vb->slot, 1, &buffer->buffer, &offset);
            }
        } else if (const auto* ib = std::get_if<cmd::SetIndexBuffer>(&command)) {
            if (const Buffer* buffer = buffers_.get(ib->buffer)) {
                vkCmdBindIndexBuffer(cmd, buffer->buffer, ib->offset,
                                     ib->type == IndexType::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
            }
        } else if (const auto* group = std::get_if<cmd::SetBindGroup>(&command)) {
            const BindGroup* bindGroup = bindGroups_.get(group->group);
            if (!bindGroup || bindGroup->setIndex != group->set) {
                log::error("Vulkan backend: setBindGroup({}) with an invalid or mismatched bind group", group->set);
                continue;
            }
            boundGroups[group->set] = bindGroup;
            dirtyGroups |= 1u << group->set;
        } else if (const auto* draw = std::get_if<cmd::Draw>(&command)) {
            if (pipeline) {
                bindDirtyGroups();
                vkCmdDraw(cmd, draw->vertexCount, draw->instanceCount, draw->firstVertex, draw->firstInstance);
            }
        } else if (const auto* drawIndexed = std::get_if<cmd::DrawIndexed>(&command)) {
            if (pipeline) {
                bindDirtyGroups();
                vkCmdDrawIndexed(cmd, drawIndexed->indexCount, drawIndexed->instanceCount, drawIndexed->firstIndex,
                                 drawIndexed->vertexOffset, drawIndexed->firstInstance);
            }
        }
    }
}

} // namespace
} // namespace vulkan

std::unique_ptr<Device> createVulkanDevice(const DeviceDesc& desc) {
    auto device = std::make_unique<vulkan::VulkanDevice>();
    if (!device->initialize(desc)) {
        return nullptr;
    }
    return device;
}

} // namespace animal::rhi
