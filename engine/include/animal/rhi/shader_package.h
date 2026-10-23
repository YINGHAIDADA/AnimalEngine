#pragma once

// Compiled shader package (.ashd), produced by tools/shaderc at build time.
// Header-only so the shader compiler can use it without linking the engine.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace animal::rhi {

static_assert(std::endian::native == std::endian::little, "Shader packages are little-endian");

enum class ShaderStage : uint32_t {
    Vertex = 0,
    Fragment = 1,
};

enum class BindingKind : uint32_t {
    UniformBuffer = 0,
    CombinedImageSampler = 1,
};

// The OpenGL backend flattens (set, binding) into slot = set * kMaxBindingsPerSet + binding,
// so both limits are part of the shader contract.
inline constexpr uint32_t kMaxBindGroups = 4;
inline constexpr uint32_t kMaxBindingsPerSet = 8;

struct ShaderBinding {
    uint32_t set = 0;
    uint32_t binding = 0;
    BindingKind kind = BindingKind::UniformBuffer;
};

struct ShaderPackage {
    ShaderStage stage = ShaderStage::Vertex;
    std::vector<ShaderBinding> bindings;
    std::vector<uint32_t> spirv;
    std::string glsl;
};

// Same resources regardless of declaration order (shader hot reload keeps bind groups valid only then).
inline bool sameBindings(const std::vector<ShaderBinding>& a, const std::vector<ShaderBinding>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (const ShaderBinding& x : a) {
        bool found = false;
        for (const ShaderBinding& y : b) {
            found = found || (x.set == y.set && x.binding == y.binding && x.kind == y.kind);
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

// GLSL 410 cannot declare explicit bindings, so resources are renamed to these
// names by the shader compiler and looked up by the OpenGL backend after linking.
inline std::string glslResourceName(BindingKind kind, uint32_t set, uint32_t binding) {
    const char* prefix = kind == BindingKind::UniformBuffer ? "ubo" : "tex";
    return std::string(prefix) + "_s" + std::to_string(set) + "_b" + std::to_string(binding);
}

namespace detail {

inline constexpr char kShaderPackageMagic[4] = {'A', 'S', 'H', 'D'};
inline constexpr uint32_t kShaderPackageVersion = 1;

struct ShaderPackageHeader {
    char magic[4];
    uint32_t version;
    uint32_t stage;
    uint32_t bindingCount;
    uint32_t spirvWordCount;
    uint32_t glslLength;
};

struct PackedBinding {
    uint32_t set;
    uint32_t binding;
    uint32_t kind;
};

} // namespace detail

inline std::vector<std::byte> serializeShaderPackage(const ShaderPackage& package) {
    detail::ShaderPackageHeader header{};
    std::memcpy(header.magic, detail::kShaderPackageMagic, sizeof(header.magic));
    header.version = detail::kShaderPackageVersion;
    header.stage = static_cast<uint32_t>(package.stage);
    header.bindingCount = static_cast<uint32_t>(package.bindings.size());
    header.spirvWordCount = static_cast<uint32_t>(package.spirv.size());
    header.glslLength = static_cast<uint32_t>(package.glsl.size());

    std::vector<std::byte> out;
    auto append = [&out](const void* data, size_t size) {
        const auto* bytes = static_cast<const std::byte*>(data);
        out.insert(out.end(), bytes, bytes + size);
    };
    append(&header, sizeof(header));
    for (const ShaderBinding& b : package.bindings) {
        const detail::PackedBinding packed{b.set, b.binding, static_cast<uint32_t>(b.kind)};
        append(&packed, sizeof(packed));
    }
    append(package.spirv.data(), package.spirv.size() * sizeof(uint32_t));
    append(package.glsl.data(), package.glsl.size());
    return out;
}

inline std::optional<ShaderPackage> parseShaderPackage(std::span<const std::byte> data,
                                                       std::string* error = nullptr) {
    auto fail = [error](const char* message) -> std::optional<ShaderPackage> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };

    detail::ShaderPackageHeader header{};
    if (data.size() < sizeof(header)) {
        return fail("file too small for shader package header");
    }
    std::memcpy(&header, data.data(), sizeof(header));
    if (std::memcmp(header.magic, detail::kShaderPackageMagic, sizeof(header.magic)) != 0) {
        return fail("not a shader package (bad magic)");
    }
    if (header.version != detail::kShaderPackageVersion) {
        return fail("unsupported shader package version; rebuild shaders");
    }
    if (header.stage > static_cast<uint32_t>(ShaderStage::Fragment)) {
        return fail("unknown shader stage");
    }

    const size_t expected = sizeof(header) + size_t{header.bindingCount} * sizeof(detail::PackedBinding) +
                            size_t{header.spirvWordCount} * sizeof(uint32_t) + header.glslLength;
    if (data.size() != expected) {
        return fail("shader package size does not match its header (truncated or corrupt)");
    }

    ShaderPackage package;
    package.stage = static_cast<ShaderStage>(header.stage);
    size_t offset = sizeof(header);

    package.bindings.reserve(header.bindingCount);
    for (uint32_t i = 0; i < header.bindingCount; ++i) {
        detail::PackedBinding packed{};
        std::memcpy(&packed, data.data() + offset, sizeof(packed));
        offset += sizeof(packed);
        if (packed.kind > static_cast<uint32_t>(BindingKind::CombinedImageSampler) ||
            packed.set >= kMaxBindGroups || packed.binding >= kMaxBindingsPerSet) {
            return fail("invalid binding entry in shader package");
        }
        package.bindings.push_back({packed.set, packed.binding, static_cast<BindingKind>(packed.kind)});
    }

    package.spirv.resize(header.spirvWordCount);
    std::memcpy(package.spirv.data(), data.data() + offset, package.spirv.size() * sizeof(uint32_t));
    offset += package.spirv.size() * sizeof(uint32_t);

    package.glsl.assign(reinterpret_cast<const char*>(data.data() + offset), header.glslLength);
    return package;
}

} // namespace animal::rhi
