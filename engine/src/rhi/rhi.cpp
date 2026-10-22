#include <animal/core/log.h>
#include <animal/rhi/rhi.h>

namespace animal::rhi {

std::unique_ptr<Device> createOpenGLDevice(const DeviceDesc& desc);

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
    return backend == Backend::OpenGL;
}

uint32_t bytesPerPixel(Format format) {
    switch (format) {
    case Format::RGBA8_UNorm:
        return 4;
    case Format::R8_UNorm:
        return 1;
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
        log::error("The {} backend is not implemented yet (planned for milestone M2.5)", backendName(desc.backend));
        return nullptr;
    }
    return createOpenGLDevice(desc);
}

} // namespace animal::rhi
