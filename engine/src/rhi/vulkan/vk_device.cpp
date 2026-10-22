#include "vk_device.h"

namespace animal::rhi::vulkan {

namespace {

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

} // namespace

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
    if (!pickPhysicalDevice() || !createLogicalDevice() || !createAllocator() || !createFrames()) {
        return false;
    }
    createPipelineCache();

    if (!createDescriptorPool()) {
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
                    timestampBits_ = families[i].timestampValidBits;
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

    const VkPhysicalDeviceProperties& props = props2.properties;
    caps_.deviceName = props.deviceName;
    vendorId_ = props.vendorID;
    deviceId_ = props.deviceID;
    std::copy(std::begin(props.pipelineCacheUUID), std::end(props.pipelineCacheUUID), pipelineCacheUuid_.begin());
    caps_.apiVersion = std::format("Vulkan {}.{}.{} ({} {})", VK_API_VERSION_MAJOR(props.apiVersion),
                                   VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion),
                                   driver.driverName, driver.driverInfo);
    caps_.maxTextureSize = props.limits.maxImageDimension2D;
    caps_.uniformBufferAlignment = static_cast<uint32_t>(props.limits.minUniformBufferOffsetAlignment);
    caps_.framesInFlight = kFramesInFlight;
    caps_.computeShaders = true;
    caps_.firstInstance = true;
    timestampPeriod_ = props.limits.timestampPeriod;
    caps_.gpuTimers = timestampBits_ > 0 && timestampPeriod_ > 0.0;
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

bool VulkanDevice::createAllocator() {
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr; // loaded by volk
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo info{};
    info.physicalDevice = physical_;
    info.device = device_;
    info.instance = instance_;
    info.vulkanApiVersion = VK_API_VERSION_1_2;
    info.pVulkanFunctions = &functions;
    return check(vmaCreateAllocator(&info, &allocator_), "vmaCreateAllocator");
}

bool VulkanDevice::createDescriptorPool() {
    constexpr uint32_t kSets = 128;
    const std::array<VkDescriptorPoolSize, 2> sizes = {{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSets * 2},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSets * 4},
    }};
    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets = kSets;
    info.poolSizeCount = static_cast<uint32_t>(sizes.size());
    info.pPoolSizes = sizes.data();
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (!check(vkCreateDescriptorPool(device_, &info, nullptr, &pool), "vkCreateDescriptorPool")) {
        return false;
    }
    descriptorPools_.push_back(pool);
    log::debug("Vulkan: descriptor pool {} created", descriptorPools_.size());
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
        if (caps_.gpuTimers) {
            VkQueryPoolCreateInfo queryInfo{};
            queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
            queryInfo.queryCount = TimerScopes::kMaxTimestamps;
            if (!check(vkCreateQueryPool(device_, &queryInfo, nullptr, &frame.timerPool), "vkCreateQueryPool")) {
                return false;
            }
        }
    }
    return true;
}

VulkanDevice::~VulkanDevice() {
    if (allocator_) {
        VmaTotalStatistics stats;
        vmaCalculateStatistics(allocator_, &stats);
        log::info("Vulkan memory: {} allocation(s) in {} device memory block(s), {:.1f} MiB",
                  stats.total.statistics.allocationCount, stats.total.statistics.blockCount,
                  static_cast<double>(stats.total.statistics.blockBytes) / (1024.0 * 1024.0));
    }
    if (device_) {
        vkDeviceWaitIdle(device_);
        for (Frame& frame : frames_) {
            for (auto& fn : frame.deletions) {
                fn();
            }
            frame.deletions.clear();
        }
        bindGroups_.forEach([this](BindGroup& g) { vkFreeDescriptorSets(device_, g.pool, 1, &g.set); });
        pipelines_.forEach([this](Pipeline& p) {
            vkDestroyPipeline(device_, p.pipeline, nullptr);
            vkDestroyPipelineLayout(device_, p.layout, nullptr);
        });
        shaders_.forEach([this](Shader& s) { vkDestroyShaderModule(device_, s.module, nullptr); });
        samplers_.forEach([this](Sampler& s) { vkDestroySampler(device_, s.sampler, nullptr); });
        textures_.forEach([this](Texture& t) { destroyImageResources(t); });
        buffers_.forEach([this](Buffer& b) { vmaDestroyBuffer(allocator_, b.buffer, b.allocation); });
        for (auto& [signature, layout] : setLayoutCache_) {
            vkDestroyDescriptorSetLayout(device_, layout, nullptr);
        }
        for (Frame& frame : frames_) {
            destroyHostBuffer(frame.staging);
            vkDestroyQueryPool(device_, frame.timerPool, nullptr);
            vkDestroySemaphore(device_, frame.imageAvailable, nullptr);
            vkDestroyFence(device_, frame.fence, nullptr);
            vkDestroyCommandPool(device_, frame.pool, nullptr);
        }
        for (VkSemaphore semaphore : renderFinished_) {
            vkDestroySemaphore(device_, semaphore, nullptr);
        }
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        vkDestroyCommandPool(device_, immediatePool_, nullptr);
        for (VkDescriptorPool pool : descriptorPools_) {
            vkDestroyDescriptorPool(device_, pool, nullptr);
        }
        if (pipelineCache_) {
            log::info("Vulkan: {} pipeline(s) created in {:.1f} ms ({})", pipelinesCreated_, pipelineMilliseconds_,
                      pipelineCacheLoaded_ ? std::format("cache loaded, {} KiB", pipelineCacheLoaded_ / 1024)
                                           : std::string("cache started empty"));
            savePipelineCache();
            vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
        }
        if (allocator_) {
            vmaDestroyAllocator(allocator_);
        }
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


} // namespace animal::rhi::vulkan

namespace animal::rhi {

std::unique_ptr<Device> createVulkanDevice(const DeviceDesc& desc) {
    auto device = std::make_unique<vulkan::VulkanDevice>();
    if (!device->initialize(desc)) {
        return nullptr;
    }
    return device;
}

} // namespace animal::rhi

