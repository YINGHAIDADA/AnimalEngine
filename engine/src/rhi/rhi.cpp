#include <animal/core/log.h>
#include <animal/rhi/rhi.h>

namespace animal::rhi {

std::unique_ptr<Device> createOpenGLDevice(const DeviceDesc& desc);
#if defined(ANIMAL_RHI_VULKAN)
std::unique_ptr<Device> createVulkanDevice(const DeviceDesc& desc);
#endif

const char* backendName(Backend backend) {
    switch (backend) {
    case Backend::OpenGL:
        return "OpenGL";
    case Backend::Vulkan:
        return "Vulkan";
    }
    return "Unknown";
}

bool isBackendImplemented(Backend backend) {
    switch (backend) {
    case Backend::OpenGL:
        return true;
    case Backend::Vulkan:
#if defined(ANIMAL_RHI_VULKAN)
        return true;
#else
        return false;
#endif
    }
    return false;
}

uint32_t bytesPerPixel(Format format) {
    switch (format) {
    case Format::RGBA8_UNorm:
        return 4;
    case Format::R8_UNorm:
        return 1;
    case Format::RGBA16_Float:
        return 8;
    case Format::Undefined:
        break;
    }
    return 0;
}

std::unique_ptr<Device> createDevice(const DeviceDesc& desc) {
    if (!desc.window) {
        log::error("createDevice: a window is required");
        return nullptr;
    }
    if (!isBackendImplemented(desc.backend)) {
        log::error("The {} backend is not available in this build", backendName(desc.backend));
        return nullptr;
    }
#if defined(ANIMAL_RHI_VULKAN)
    if (desc.backend == Backend::Vulkan) {
        return createVulkanDevice(desc);
    }
#endif
    return createOpenGLDevice(desc);
}

} // namespace animal::rhi
