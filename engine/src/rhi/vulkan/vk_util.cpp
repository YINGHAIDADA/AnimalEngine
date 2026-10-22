#include "vk_device.h"

namespace animal::rhi::vulkan {

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
    case Format::RGBA32_Float:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
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


} // namespace animal::rhi::vulkan
