#include <animal/core/log.h>
#include <animal/rhi/validation.h>

#include <algorithm>
#include <format>
#include <set>
#include <variant>

namespace animal::rhi {

namespace {

constexpr size_t kKeptMessages = 100;

std::string label(const char* name) {
    return name ? std::format(" '{}'", name) : std::string();
}

const char* formatName(Format format) {
    switch (format) {
    case Format::Undefined:
        return "Undefined";
    case Format::RGBA8_UNorm:
        return "RGBA8_UNorm";
    case Format::R8_UNorm:
        return "R8_UNorm";
    case Format::RGBA16_Float:
        return "RGBA16_Float";
    case Format::RGBA32_Float:
        return "RGBA32_Float";
    }
    return "?";
}

} // namespace

ValidatingDevice::ValidatingDevice(std::unique_ptr<Device> inner) : inner_(std::move(inner)) {}

ValidatingDevice::~ValidatingDevice() {
    log::info("RHI validation: {} error(s)", errorCount_);
}

void ValidatingDevice::report(std::string message) {
    ++errorCount_;
    log::error("RHI validation: {}", message);
    if (messages_.size() < kKeptMessages) {
        messages_.push_back(std::move(message));
    }
}

bool ValidatingDevice::textureInfo(TextureHandle handle, TextureInfo& out) const {
    if (handle.valid() && handle == inner_->swapchainTexture()) {
        out = {inner_->swapchainExtent(), inner_->swapchainFormat(), TextureUsage::RenderTarget};
        return true;
    }
    const auto it = textures_.find(handle.key());
    if (!handle.valid() || it == textures_.end()) {
        return false;
    }
    out = it->second;
    return true;
}

// --- Resources ------------------------------------------------------------------------------

BufferHandle ValidatingDevice::createBuffer(const BufferDesc& desc, const void* initialData) {
    if (desc.size == 0 || static_cast<uint32_t>(desc.usage) == 0) {
        report(std::format("createBuffer{}: size and usage must be non-zero", label(desc.debugName)));
        return {};
    }
    const BufferHandle handle = inner_->createBuffer(desc, initialData);
    if (handle) {
        buffers_[handle.key()] = {desc.size, desc.usage};
    }
    return handle;
}

TextureHandle ValidatingDevice::createTexture(const TextureDesc& desc) {
    const uint32_t limit = inner_->caps().maxTextureSize;
    if (desc.width == 0 || desc.height == 0 || desc.width > limit || desc.height > limit) {
        report(std::format("createTexture{}: size {}x{} must be 1..{}", label(desc.debugName), desc.width,
                           desc.height, limit));
        return {};
    }
    if (desc.format == Format::Undefined || static_cast<uint32_t>(desc.usage) == 0) {
        report(std::format("createTexture{}: format and usage must be set", label(desc.debugName)));
        return {};
    }
    const TextureHandle handle = inner_->createTexture(desc);
    if (handle) {
        textures_[handle.key()] = {{desc.width, desc.height}, desc.format, desc.usage};
    }
    return handle;
}

SamplerHandle ValidatingDevice::createSampler(const SamplerDesc& desc) {
    const SamplerHandle handle = inner_->createSampler(desc);
    if (handle) {
        samplers_[handle.key()] = true;
    }
    return handle;
}

ShaderHandle ValidatingDevice::createShader(const ShaderDesc& desc) {
    if (!desc.package) {
        report(std::format("createShader{}: no shader package", label(desc.debugName)));
        return {};
    }
    std::set<std::pair<uint32_t, uint32_t>> seen;
    for (const ShaderBinding& b : desc.package->bindings) {
        if (b.set >= kMaxBindGroups || b.binding >= kMaxBindingsPerSet) {
            report(std::format("createShader{}: binding (set {}, binding {}) exceeds the limits {} x {}",
                               label(desc.debugName), b.set, b.binding, kMaxBindGroups, kMaxBindingsPerSet));
            return {};
        }
        if (!seen.insert({b.set, b.binding}).second) {
            report(std::format("createShader{}: (set {}, binding {}) declared twice", label(desc.debugName), b.set,
                               b.binding));
            return {};
        }
    }
    const ShaderHandle handle = inner_->createShader(desc);
    if (handle) {
        shaders_[handle.key()] = {desc.package->stage, desc.package->bindings};
    }
    return handle;
}

PipelineHandle ValidatingDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    const std::string name = label(desc.debugName);
    const auto vs = shaders_.find(desc.vertexShader.key());
    const auto fs = shaders_.find(desc.fragmentShader.key());
    if (!desc.vertexShader || vs == shaders_.end() || vs->second.stage != ShaderStage::Vertex) {
        report(std::format("createGraphicsPipeline{}: needs a valid vertex shader", name));
        return {};
    }
    if (!desc.fragmentShader || fs == shaders_.end() || fs->second.stage != ShaderStage::Fragment) {
        report(std::format("createGraphicsPipeline{}: needs a valid fragment shader", name));
        return {};
    }
    if (desc.colorTargets.empty() || desc.colorTargets.size() > kMaxColorAttachments) {
        report(std::format("createGraphicsPipeline{}: needs 1..{} color targets", name, kMaxColorAttachments));
        return {};
    }
    PipelineInfo info;
    info.name = name;
    for (const ColorTargetDesc& target : desc.colorTargets) {
        if (target.format == Format::Undefined) {
            report(std::format("createGraphicsPipeline{}: a color target has no format", name));
            return {};
        }
        if (target.format == Format::RGBA32_Float && target.blend.enabled) {
            report(std::format("createGraphicsPipeline{}: RGBA32_Float targets cannot blend", name));
            return {};
        }
        info.colorFormats.push_back(target.format);
    }
    std::set<uint32_t> locations;
    for (const VertexAttribute& a : desc.vertexAttributes) {
        if (a.bufferSlot >= desc.vertexBuffers.size()) {
            report(std::format("createGraphicsPipeline{}: attribute {} uses vertex buffer slot {} but only {} are "
                               "declared",
                               name, a.location, a.bufferSlot, desc.vertexBuffers.size()));
            return {};
        }
        if (!locations.insert(a.location).second) {
            report(std::format("createGraphicsPipeline{}: attribute location {} declared twice", name, a.location));
            return {};
        }
    }
    info.vertexBuffers = static_cast<uint32_t>(desc.vertexBuffers.size());
    // Both stages share one layout per set; the same binding must mean the same thing in both.
    std::map<std::pair<uint32_t, uint32_t>, BindingKind> bindings;
    for (const ShaderInfo* shader : {&vs->second, &fs->second}) {
        for (const ShaderBinding& b : shader->bindings) {
            const auto [it, added] = bindings.try_emplace({b.set, b.binding}, b.kind);
            if (!added && it->second != b.kind) {
                report(std::format("createGraphicsPipeline{}: (set {}, binding {}) is a different kind of resource "
                                   "in the two stages",
                                   name, b.set, b.binding));
                return {};
            }
        }
    }
    for (const auto& [key, kind] : bindings) {
        info.sets[key.first].push_back({key.second, kind});
    }
    const PipelineHandle handle = inner_->createGraphicsPipeline(desc);
    if (handle) {
        pipelines_[handle.key()] = std::move(info);
    }
    return handle;
}

BindGroupHandle ValidatingDevice::createBindGroup(const BindGroupDesc& desc) {
    const std::string name = label(desc.debugName);
    const auto pipeline = pipelines_.find(desc.pipeline.key());
    if (!desc.pipeline || pipeline == pipelines_.end()) {
        report(std::format("createBindGroup{}: invalid pipeline", name));
        return {};
    }
    if (desc.set >= kMaxBindGroups || pipeline->second.sets[desc.set].empty()) {
        report(std::format("createBindGroup{}: pipeline{} does not use set {}", name, pipeline->second.name,
                           desc.set));
        return {};
    }
    const SetLayout& layout = pipeline->second.sets[desc.set];
    BindGroupInfo info{desc.set, layout, {}, {}};
    std::set<uint32_t> provided;
    for (const BindGroupEntry& e : desc.entries) {
        const auto slot = std::find_if(layout.begin(), layout.end(), [&e](const auto& l) { return l.first == e.binding; });
        if (slot == layout.end()) {
            report(std::format("createBindGroup{}: binding {} is not part of set {}", name, e.binding, desc.set));
            return {};
        }
        if (!provided.insert(e.binding).second) {
            report(std::format("createBindGroup{}: binding {} given twice", name, e.binding));
            return {};
        }
        if (slot->second == BindingKind::UniformBuffer) {
            const auto buffer = buffers_.find(e.buffer.key());
            if (!e.buffer || buffer == buffers_.end() || !hasFlag(buffer->second.usage, BufferUsage::Uniform)) {
                report(std::format("createBindGroup{}: binding {} needs a valid uniform buffer", name, e.binding));
                return {};
            }
            const uint64_t size = e.size ? e.size : buffer->second.size - std::min(e.offset, buffer->second.size);
            if (e.offset % inner_->caps().uniformBufferAlignment != 0 || e.offset + size > buffer->second.size) {
                report(std::format("createBindGroup{}: binding {} range (offset {}, size {}) is misaligned or out of "
                                   "bounds",
                                   name, e.binding, e.offset, size));
                return {};
            }
            info.buffers.push_back(e.buffer);
        } else {
            TextureInfo texture;
            if (!textureInfo(e.texture, texture) || !hasFlag(texture.usage, TextureUsage::Sampled) ||
                !samplers_.count(e.sampler.key()) || !e.sampler) {
                report(std::format("createBindGroup{}: binding {} needs a sampled texture and a sampler", name,
                                   e.binding));
                return {};
            }
            info.textures.push_back(e.texture);
        }
    }
    if (provided.size() != layout.size()) {
        report(std::format("createBindGroup{}: set {} has {} bindings but {} were given", name, desc.set, layout.size(),
                           provided.size()));
        return {};
    }
    const BindGroupHandle handle = inner_->createBindGroup(desc);
    if (handle) {
        bindGroups_[handle.key()] = std::move(info);
    }
    return handle;
}

// Destroying an unknown handle is a double destroy or a stale handle.
#define ANIMAL_VALIDATE_DESTROY(map, kind)                                                                  \
    if (map.erase(handle.key()) == 0) {                                                                     \
        report("destroy: invalid or already destroyed " kind);                                              \
        return;                                                                                             \
    }                                                                                                       \
    inner_->destroy(handle);

void ValidatingDevice::destroy(BufferHandle handle) {
    if (!handle) {
        return;
    }
    ANIMAL_VALIDATE_DESTROY(buffers_, "buffer")
}
void ValidatingDevice::destroy(TextureHandle handle) {
    if (!handle) {
        return;
    }
    ANIMAL_VALIDATE_DESTROY(textures_, "texture")
}
void ValidatingDevice::destroy(SamplerHandle handle) {
    if (!handle) {
        return;
    }
    ANIMAL_VALIDATE_DESTROY(samplers_, "sampler")
}
void ValidatingDevice::destroy(ShaderHandle handle) {
    if (!handle) {
        return;
    }
    ANIMAL_VALIDATE_DESTROY(shaders_, "shader")
}
void ValidatingDevice::destroy(PipelineHandle handle) {
    if (!handle) {
        return;
    }
    ANIMAL_VALIDATE_DESTROY(pipelines_, "pipeline")
}
void ValidatingDevice::destroy(BindGroupHandle handle) {
    if (!handle) {
        return;
    }
    ANIMAL_VALIDATE_DESTROY(bindGroups_, "bind group")
}

#undef ANIMAL_VALIDATE_DESTROY

void ValidatingDevice::writeBuffer(BufferHandle buffer, uint64_t offset, const void* data, uint64_t size) {
    const auto it = buffers_.find(buffer.key());
    if (!buffer || it == buffers_.end()) {
        report("writeBuffer: invalid buffer");
        return;
    }
    if (!data || offset + size > it->second.size) {
        report(std::format("writeBuffer: {} bytes at offset {} exceed the buffer size {}", size, offset,
                           it->second.size));
        return;
    }
    inner_->writeBuffer(buffer, offset, data, size);
}

void ValidatingDevice::writeTexture(TextureHandle texture, const TextureRegion& region, const void* pixels,
                                    uint32_t bytesPerRow) {
    TextureInfo info;
    if (!textureInfo(texture, info)) {
        report("writeTexture: invalid texture");
        return;
    }
    if (!pixels || region.width == 0 || region.height == 0 || region.x + region.width > info.extent.width ||
        region.y + region.height > info.extent.height || bytesPerRow < region.width * bytesPerPixel(info.format)) {
        report(std::format("writeTexture: region {}x{} at ({}, {}) with {} bytes per row does not fit the {}x{} {} "
                           "texture",
                           region.width, region.height, region.x, region.y, bytesPerRow, info.extent.width,
                           info.extent.height, formatName(info.format)));
        return;
    }
    inner_->writeTexture(texture, region, pixels, bytesPerRow);
}

std::vector<uint8_t> ValidatingDevice::readTexture(TextureHandle texture) {
    TextureInfo info;
    if (!textureInfo(texture, info)) {
        report("readTexture: invalid texture");
        return {};
    }
    return inner_->readTexture(texture);
}

// --- Command lists -----------------------------------------------------------------------------

void ValidatingDevice::submit(const CommandList& commands) {
    validate(commands);
    inner_->submit(commands);
}

uint32_t ValidatingDevice::validate(const CommandList& list) {
    const uint32_t before = errorCount_;
    const std::vector<Command>& commands = list.commands();
    // State within the current render pass (pass structure, a bound pipeline before draws and
    // balanced timer scopes are asserted while recording, see CommandList).
    std::vector<TextureHandle> attachments;
    std::vector<Format> attachmentFormats;
    const PipelineInfo* pipeline = nullptr;
    std::array<const BindGroupInfo*, kMaxBindGroups> groups{};
    std::vector<bool> vertexBuffers;
    bool indexBuffer = false;

    for (size_t i = 0; i < commands.size(); ++i) {
        const Command& command = commands[i];
        const std::string at = std::format("command {}", i);
        if (const auto* begin = std::get_if<cmd::BeginRenderPass>(&command)) {
            attachments.clear();
            attachmentFormats.clear();
            pipeline = nullptr;
            groups = {};
            vertexBuffers.clear();
            indexBuffer = false;
            std::optional<Extent2D> extent;
            for (uint32_t a = 0; a < begin->desc.colorAttachmentCount; ++a) {
                TextureInfo info;
                const TextureHandle texture = begin->desc.colorAttachments[a].texture;
                if (!textureInfo(texture, info) || !hasFlag(info.usage, TextureUsage::RenderTarget)) {
                    report(std::format("{}: render pass attachment {} is not a valid render target", at, a));
                    continue;
                }
                if (extent && *extent != info.extent) {
                    report(std::format("{}: render pass attachments have different sizes", at));
                }
                extent = info.extent;
                attachments.push_back(texture);
                attachmentFormats.push_back(info.format);
            }
        } else if (const auto* c = std::get_if<cmd::SetPipeline>(&command)) {
            const auto it = pipelines_.find(c->pipeline.key());
            if (it == pipelines_.end()) {
                report(std::format("{}: setPipeline with an invalid or destroyed pipeline", at));
                pipeline = nullptr;
                continue;
            }
            pipeline = &it->second;
            if (pipeline->colorFormats != attachmentFormats) {
                report(std::format("{}: pipeline{} color targets do not match the render pass attachments", at,
                                   pipeline->name));
            }
        } else if (const auto* bind = std::get_if<cmd::SetBindGroup>(&command)) {
            const auto it = bindGroups_.find(bind->group.key());
            if (it == bindGroups_.end() || bind->set >= kMaxBindGroups || it->second.set != bind->set) {
                report(std::format("{}: setBindGroup({}) with an invalid, destroyed or mismatched bind group", at,
                                   bind->set));
                continue;
            }
            groups[bind->set] = &it->second;
        } else if (const auto* vb = std::get_if<cmd::SetVertexBuffer>(&command)) {
            const auto it = buffers_.find(vb->buffer.key());
            if (it == buffers_.end() || !hasFlag(it->second.usage, BufferUsage::Vertex)) {
                report(std::format("{}: setVertexBuffer({}) needs a valid vertex buffer", at, vb->slot));
                continue;
            }
            if (vertexBuffers.size() <= vb->slot) {
                vertexBuffers.resize(vb->slot + 1, false);
            }
            vertexBuffers[vb->slot] = true;
        } else if (const auto* ib = std::get_if<cmd::SetIndexBuffer>(&command)) {
            const auto it = buffers_.find(ib->buffer.key());
            if (it == buffers_.end() || !hasFlag(it->second.usage, BufferUsage::Index)) {
                report(std::format("{}: setIndexBuffer needs a valid index buffer", at));
                continue;
            }
            indexBuffer = true;
        } else if (std::holds_alternative<cmd::Draw>(command) || std::holds_alternative<cmd::DrawIndexed>(command)) {
            const bool indexed = std::holds_alternative<cmd::DrawIndexed>(command);
            const uint32_t firstInstance = indexed ? std::get<cmd::DrawIndexed>(command).firstInstance
                                                   : std::get<cmd::Draw>(command).firstInstance;
            if (!pipeline) {
                continue; // the invalid setPipeline was reported
            }
            if (indexed && !indexBuffer) {
                report(std::format("{}: drawIndexed without an index buffer", at));
            }
            if (firstInstance != 0 && !inner_->caps().firstInstance) {
                report(std::format("{}: firstInstance is not supported by this backend", at));
            }
            for (uint32_t slot = 0; slot < pipeline->vertexBuffers; ++slot) {
                if (slot >= vertexBuffers.size() || !vertexBuffers[slot]) {
                    report(std::format("{}: pipeline{} needs vertex buffer slot {}", at, pipeline->name, slot));
                }
            }
            for (uint32_t set = 0; set < kMaxBindGroups; ++set) {
                if (pipeline->sets[set].empty()) {
                    continue;
                }
                if (!groups[set]) {
                    report(std::format("{}: pipeline{} needs a bind group for set {}", at, pipeline->name, set));
                } else if (groups[set]->layout != pipeline->sets[set]) {
                    report(std::format("{}: bind group for set {} does not match the layout of pipeline{}", at, set,
                                       pipeline->name));
                }
            }
            for (const BindGroupInfo* group : groups) {
                if (!group) {
                    continue;
                }
                for (TextureHandle t : group->textures) {
                    if (std::find(attachments.begin(), attachments.end(), t) != attachments.end()) {
                        report(std::format("{}: a texture is sampled while it is an attachment of the render pass", at));
                    }
                    TextureInfo info;
                    if (!textureInfo(t, info)) {
                        report(std::format("{}: a bound group samples a destroyed texture", at));
                    }
                }
                for (BufferHandle b : group->buffers) {
                    if (!buffers_.count(b.key())) {
                        report(std::format("{}: a bound group uses a destroyed buffer", at));
                    }
                }
            }
        }
    }
    return errorCount_ - before;
}

} // namespace animal::rhi
