// OpenGL 4.1 Core backend. Emulates the Vulkan conventions of the RHI:
//  - Every pass renders into an offscreen texture whose row 0 is the top row (Vulkan memory
//    layout). Only the final present flips vertically while blitting to the window.
//  - Viewport/scissor y and texture uploads/readbacks therefore need no flipping, but the
//    front-face winding is mirrored (Vulkan CCW == OpenGL CW in this layout).
//  - (set, binding) map to GL slots set * kMaxBindingsPerSet + binding; the shader compiler
//    renamed resources so the program can be wired up by name after linking.
//  - Clip-space depth [0, 1] is converted in the vertex shader by the shader compiler.

#include "gl_loader.h"

#include <animal/core/assert.h>
#include <animal/core/log.h>
#include <animal/platform/window.h>
#include <animal/rhi/command_list.h>
#include <animal/rhi/gpu_timers.h>
#include <animal/rhi/rhi.h>

#include <SDL3/SDL_video.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace animal::rhi {

namespace gl {
namespace {

struct GlBuffer {
    GLuint id = 0;
    uint64_t size = 0;
    BufferUsage usage = BufferUsage::Vertex;
};

struct GlTexture {
    GLuint id = 0;
    GLuint framebuffer = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    Format format = Format::RGBA8_UNorm;
    TextureUsage usage = TextureUsage::Sampled;
};

struct GlSampler {
    GLuint id = 0;
};

struct GlShader {
    GLuint id = 0;
    ShaderStage stage = ShaderStage::Vertex;
    std::vector<ShaderBinding> bindings;
    std::string name;
};

struct GlPipeline {
    GLuint program = 0;
    ShaderHandle vertexShader;   // relinked when either shader is reloaded
    ShaderHandle fragmentShader;
    std::string name;
    std::vector<VertexBufferLayout> vertexBuffers;
    std::vector<VertexAttribute> vertexAttributes;
    GLenum topology = GL_TRIANGLES;
    CullMode cullMode = CullMode::None;
    FrontFace frontFace = FrontFace::CounterClockwise;
    std::vector<ColorTargetDesc> colorTargets;
    std::vector<ShaderBinding> bindings;
};

struct GlBindGroup {
    uint32_t set = 0;
    std::vector<BindGroupEntry> entries;
};

struct TextureFormatInfo {
    GLint internalFormat;
    GLenum format;
    GLenum type;
};

TextureFormatInfo textureFormatInfo(Format format) {
    switch (format) {
    case Format::RGBA8_UNorm:
        return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
    case Format::R8_UNorm:
        return {GL_R8, GL_RED, GL_UNSIGNED_BYTE};
    case Format::RGBA16_Float:
        return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
    case Format::RGBA32_Float:
        return {GL_RGBA32F, GL_RGBA, GL_FLOAT};
    case Format::Undefined:
        break;
    }
    return {0, 0, 0};
}

struct VertexFormatInfo {
    GLint components;
    GLenum type;
    GLboolean normalized;
};

VertexFormatInfo vertexFormatInfo(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float2:
        return {2, GL_FLOAT, GL_FALSE};
    case VertexFormat::Float3:
        return {3, GL_FLOAT, GL_FALSE};
    case VertexFormat::Float4:
        return {4, GL_FLOAT, GL_FALSE};
    case VertexFormat::UNorm8x4:
        return {4, GL_UNSIGNED_BYTE, GL_TRUE};
    }
    return {0, 0, GL_FALSE};
}

GLenum toGl(BlendFactor factor) {
    switch (factor) {
    case BlendFactor::Zero:
        return GL_ZERO;
    case BlendFactor::One:
        return GL_ONE;
    case BlendFactor::SrcColor:
        return GL_SRC_COLOR;
    case BlendFactor::OneMinusSrcColor:
        return GL_ONE_MINUS_SRC_COLOR;
    case BlendFactor::SrcAlpha:
        return GL_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha:
        return GL_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstColor:
        return GL_DST_COLOR;
    case BlendFactor::OneMinusDstColor:
        return GL_ONE_MINUS_DST_COLOR;
    case BlendFactor::DstAlpha:
        return GL_DST_ALPHA;
    case BlendFactor::OneMinusDstAlpha:
        return GL_ONE_MINUS_DST_ALPHA;
    }
    return GL_ONE;
}

GLenum toGl(BlendOp op) {
    switch (op) {
    case BlendOp::Add:
        return GL_FUNC_ADD;
    case BlendOp::Subtract:
        return GL_FUNC_SUBTRACT;
    case BlendOp::ReverseSubtract:
        return GL_FUNC_REVERSE_SUBTRACT;
    case BlendOp::Min:
        return GL_MIN;
    case BlendOp::Max:
        return GL_MAX;
    }
    return GL_FUNC_ADD;
}

GLenum toGl(PrimitiveTopology topology) {
    switch (topology) {
    case PrimitiveTopology::TriangleList:
        return GL_TRIANGLES;
    case PrimitiveTopology::TriangleStrip:
        return GL_TRIANGLE_STRIP;
    case PrimitiveTopology::LineList:
        return GL_LINES;
    }
    return GL_TRIANGLES;
}

GLint toGl(Filter filter) {
    return filter == Filter::Nearest ? GL_NEAREST : GL_LINEAR;
}

GLint toGl(AddressMode mode) {
    return mode == AddressMode::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
}

GLuint bindingSlot(uint32_t set, uint32_t binding) {
    return set * kMaxBindingsPerSet + binding;
}

std::string shaderInfoLog(GLuint shader) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string text(static_cast<size_t>(length > 0 ? length : 0), '\0');
    if (length > 0) {
        glGetShaderInfoLog(shader, length, nullptr, text.data());
    }
    return text;
}

std::string programInfoLog(GLuint program) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string text(static_cast<size_t>(length > 0 ? length : 0), '\0');
    if (length > 0) {
        glGetProgramInfoLog(program, length, nullptr, text.data());
    }
    return text;
}

void checkErrors([[maybe_unused]] const char* where) {
#if defined(ANIMAL_DEBUG)
    for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError()) {
        log::error("OpenGL error 0x{:04X} after {}", static_cast<unsigned>(err), where);
    }
#endif
}

const char* nameOr(const char* name, const char* fallback) {
    return name ? name : fallback;
}

class GlDevice final : public Device {
public:
    GlDevice() = default;
    ~GlDevice() override;

    bool initialize(const DeviceDesc& desc);

    Backend backend() const override { return Backend::OpenGL; }
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
    TextureHandle swapchainTexture() const override { return swapchain_; }
    Extent2D swapchainExtent() const override { return swapchainExtent_; }
    Format swapchainFormat() const override { return Format::RGBA8_UNorm; }
    void submit(const CommandList& commands) override;
    void endFrame() override;
    void waitIdle() override { glFinish(); }
    void setVsync(bool vsync) override {
        if (!SDL_GL_SetSwapInterval(vsync ? 1 : 0)) {
            log::warn("OpenGL: cannot set the swap interval: {}", SDL_GetError());
        }
    }
    std::vector<GpuTiming> gpuTimings() const override { return gpuTimings_; }

private:
    class Executor;

    // Timer queries of one frame; read back kTimerFrames frames later, when the GPU is certainly
    // done with them (a frame whose results are still not available is dropped).
    //   Timestamp: one GL_TIMESTAMP query per slot.
    //   Elapsed (drivers without timestamps, e.g. macOS): a chain of GL_TIME_ELAPSED queries, one
    //   from each timer event to the next; summing them gives each event's time since the first.
    enum class TimerMode : uint8_t { None, Timestamp, Elapsed };
    struct TimerFrame {
        std::array<GLuint, TimerScopes::kMaxTimestamps> queries{};
        TimerScopes scopes;
        std::array<uint32_t, TimerScopes::kMaxTimestamps> eventSlots{}; // Elapsed: slot of each event
        uint32_t events = 0;
        bool segmentOpen = false;
        bool pending = false;
    };
    static constexpr size_t kTimerFrames = 4;
    void recordTimerEvent(uint32_t slot);
    void collectTimers(TimerFrame& frame);
    using AttachmentIds = std::array<GLuint, kMaxColorAttachments>;

    bool createFramebuffer(GlTexture& texture);
    bool makeTexture(const TextureDesc& desc, GlTexture& texture);
    // Single attachment: the texture's own framebuffer; several: a cached one per attachment set.
    GLuint framebufferFor(const RenderPassDesc& desc, Extent2D& extent);
    void forgetFramebuffersUsing(GLuint textureId);
    void resizeSwapchain(Extent2D extent);

    platform::Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    DeviceCaps caps_;
    GLuint vertexArray_ = 0;

    HandlePool<BufferTag, GlBuffer> buffers_;
    HandlePool<TextureTag, GlTexture> textures_;
    HandlePool<SamplerTag, GlSampler> samplers_;
    HandlePool<ShaderTag, GlShader> shaders_;
    HandlePool<PipelineTag, GlPipeline> pipelines_;
    HandlePool<BindGroupTag, GlBindGroup> bindGroups_;

    TextureHandle swapchain_;
    Extent2D swapchainExtent_;
    bool frameActive_ = false;
    std::map<AttachmentIds, GLuint> multiTargetFramebuffers_;

    TimerMode timerMode_ = TimerMode::None;
    std::array<TimerFrame, kTimerFrames> timerFrames_{};
    size_t timerFrame_ = 0;
    std::vector<GpuTiming> gpuTimings_;
};

// Translates one CommandList into GL calls. Lives only for the duration of submit().
class GlDevice::Executor {
public:
    explicit Executor(GlDevice& device) : device_(device) {}

    void operator()(const cmd::BeginRenderPass& c) {
        const RenderPassDesc& desc = c.desc;
        pipeline_ = nullptr;
        Extent2D extent;
        const GLuint framebuffer = device_.framebufferFor(desc, extent);
        skipPass_ = framebuffer == 0;
        if (skipPass_) {
            return;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, static_cast<GLsizei>(extent.width), static_cast<GLsizei>(extent.height));
        glDepthRangef(0.0f, 1.0f);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i) {
            const ColorAttachment& attachment = desc.colorAttachments[i];
            if (attachment.loadOp == LoadOp::Clear) {
                const ClearColor& cc = attachment.clearColor;
                const GLfloat color[4] = {cc.r, cc.g, cc.b, cc.a};
                glClearBufferfv(GL_COLOR, static_cast<GLint>(i), color);
            }
#if defined(ANIMAL_DEBUG)
            else if (attachment.loadOp == LoadOp::DontCare) {
                // Undefined contents in Vulkan; make accidental reliance on them visible.
                const GLfloat magenta[4] = {1.0f, 0.0f, 1.0f, 1.0f};
                glClearBufferfv(GL_COLOR, static_cast<GLint>(i), magenta);
            }
#endif
        }
    }

    void operator()(const cmd::EndRenderPass&) { skipPass_ = false; }

    void operator()(const cmd::BeginTimer& c) {
        if (device_.caps_.gpuTimers) {
            device_.recordTimerEvent(device_.timerFrames_[device_.timerFrame_].scopes.begin(c.name));
        }
    }

    void operator()(const cmd::EndTimer&) {
        if (device_.caps_.gpuTimers) {
            device_.recordTimerEvent(device_.timerFrames_[device_.timerFrame_].scopes.end());
        }
    }

    void operator()(const cmd::SetPipeline& c) {
        if (skipPass_) {
            return;
        }
        pipeline_ = device_.pipelines_.get(c.pipeline);
        if (!pipeline_) {
            log::error("OpenGL backend: setPipeline with an invalid pipeline");
            return;
        }
        glUseProgram(pipeline_->program);

        for (GLuint i = 0; i < pipeline_->colorTargets.size(); ++i) {
            const BlendState& blend = pipeline_->colorTargets[i].blend;
            if (blend.enabled) {
                glEnablei(GL_BLEND, i);
                glBlendFuncSeparatei(i, toGl(blend.srcColor), toGl(blend.dstColor), toGl(blend.srcAlpha),
                                     toGl(blend.dstAlpha));
                glBlendEquationSeparatei(i, toGl(blend.colorOp), toGl(blend.alphaOp));
            } else {
                glDisablei(GL_BLEND, i);
            }
        }

        if (pipeline_->cullMode == CullMode::None) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(pipeline_->cullMode == CullMode::Front ? GL_FRONT : GL_BACK);
        }
        glFrontFace(pipeline_->frontFace == FrontFace::CounterClockwise ? GL_CW : GL_CCW);
        vertexInputDirty_ = true;
    }

    void operator()(const cmd::SetViewport& c) {
        if (skipPass_) {
            return;
        }
        const Viewport& v = c.viewport;
        glViewport(static_cast<GLint>(std::lround(v.x)), static_cast<GLint>(std::lround(v.y)),
                   static_cast<GLsizei>(std::lround(v.width)), static_cast<GLsizei>(std::lround(v.height)));
        glDepthRangef(v.minDepth, v.maxDepth);
    }

    void operator()(const cmd::SetScissor& c) {
        if (skipPass_) {
            return;
        }
        glEnable(GL_SCISSOR_TEST);
        glScissor(c.scissor.x, c.scissor.y, static_cast<GLsizei>(c.scissor.width),
                  static_cast<GLsizei>(c.scissor.height));
    }

    void operator()(const cmd::SetVertexBuffer& c) {
        if (skipPass_) {
            return;
        }
        const GlBuffer* buffer = device_.buffers_.get(c.buffer);
        vertexBuffers_[c.slot] = {buffer ? buffer->id : 0, c.offset};
        vertexInputDirty_ = true;
    }

    void operator()(const cmd::SetIndexBuffer& c) {
        if (skipPass_) {
            return;
        }
        const GlBuffer* buffer = device_.buffers_.get(c.buffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer ? buffer->id : 0);
        indexType_ = c.type == IndexType::UInt16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
        indexSize_ = c.type == IndexType::UInt16 ? 2u : 4u;
        indexOffset_ = c.offset;
        hasIndexBuffer_ = buffer != nullptr;
    }

    void operator()(const cmd::SetBindGroup& c) {
        if (skipPass_) {
            return;
        }
        const GlBindGroup* group = device_.bindGroups_.get(c.group);
        if (!group || group->set != c.set) {
            log::error("OpenGL backend: setBindGroup({}) with an invalid or mismatched bind group", c.set);
            return;
        }
        for (const BindGroupEntry& entry : group->entries) {
            const GLuint slot = bindingSlot(group->set, entry.binding);
            if (entry.buffer.valid()) {
                const GlBuffer* buffer = device_.buffers_.get(entry.buffer);
                if (!buffer) {
                    log::error("OpenGL backend: bind group references a destroyed buffer");
                    continue;
                }
                const uint64_t size = entry.size ? entry.size : buffer->size - entry.offset;
                glBindBufferRange(GL_UNIFORM_BUFFER, slot, buffer->id, static_cast<GLintptr>(entry.offset),
                                  static_cast<GLsizeiptr>(size));
            } else {
                const GlTexture* texture = device_.textures_.get(entry.texture);
                const GlSampler* sampler = device_.samplers_.get(entry.sampler);
                if (!texture || !sampler) {
                    log::error("OpenGL backend: bind group references a destroyed texture or sampler");
                    continue;
                }
                glActiveTexture(GL_TEXTURE0 + slot);
                glBindTexture(GL_TEXTURE_2D, texture->id);
                glBindSampler(slot, sampler->id);
            }
        }
    }

    void operator()(const cmd::Draw& c) {
        if (!prepareDraw(c.firstInstance)) {
            return;
        }
        glDrawArraysInstanced(pipeline_->topology, static_cast<GLint>(c.firstVertex),
                              static_cast<GLsizei>(c.vertexCount), static_cast<GLsizei>(c.instanceCount));
    }

    void operator()(const cmd::DrawIndexed& c) {
        if (!prepareDraw(c.firstInstance)) {
            return;
        }
        if (!hasIndexBuffer_) {
            log::error("OpenGL backend: drawIndexed without an index buffer");
            return;
        }
        const uint64_t byteOffset = indexOffset_ + uint64_t{c.firstIndex} * indexSize_;
        glDrawElementsInstancedBaseVertex(pipeline_->topology, static_cast<GLsizei>(c.indexCount), indexType_,
                                          reinterpret_cast<const void*>(static_cast<uintptr_t>(byteOffset)),
                                          static_cast<GLsizei>(c.instanceCount), c.vertexOffset);
    }

private:
    bool prepareDraw(uint32_t firstInstance) {
        if (skipPass_ || !pipeline_) {
            return false;
        }
        if (firstInstance != 0) {
            log::error("OpenGL backend: firstInstance is not supported on OpenGL 4.1");
            return false;
        }
        if (vertexInputDirty_ && !applyVertexInput()) {
            return false;
        }
        return true;
    }

    // GL 4.1 has no separate vertex format / binding state, so attributes are re-specified
    // whenever the pipeline or a vertex buffer changes.
    bool applyVertexInput() {
        uint32_t enabled = 0;
        for (const VertexAttribute& attribute : pipeline_->vertexAttributes) {
            const auto& [bufferId, bufferOffset] = vertexBuffers_[attribute.bufferSlot];
            if (bufferId == 0) {
                log::error("OpenGL backend: vertex buffer slot {} is not bound", attribute.bufferSlot);
                return false;
            }
            const VertexBufferLayout& layout = pipeline_->vertexBuffers[attribute.bufferSlot];
            const VertexFormatInfo info = vertexFormatInfo(attribute.format);
            glBindBuffer(GL_ARRAY_BUFFER, bufferId);
            glEnableVertexAttribArray(attribute.location);
            glVertexAttribPointer(attribute.location, info.components, info.type, info.normalized,
                                  static_cast<GLsizei>(layout.stride),
                                  reinterpret_cast<const void*>(static_cast<uintptr_t>(bufferOffset + attribute.offset)));
            glVertexAttribDivisor(attribute.location, layout.stepMode == VertexStepMode::Instance ? 1 : 0);
            enabled |= 1u << attribute.location;
        }
        for (uint32_t location = 0; location < kMaxVertexAttributes; ++location) {
            const uint32_t bit = 1u << location;
            if ((enabledAttributes_ & bit) && !(enabled & bit)) {
                glDisableVertexAttribArray(location);
            }
        }
        enabledAttributes_ = enabled;
        vertexInputDirty_ = false;
        return true;
    }

    GlDevice& device_;
    const GlPipeline* pipeline_ = nullptr;
    std::array<std::pair<GLuint, uint64_t>, kMaxVertexBuffers> vertexBuffers_{};
    GLenum indexType_ = GL_UNSIGNED_SHORT;
    uint32_t indexSize_ = 2;
    uint64_t indexOffset_ = 0;
    bool hasIndexBuffer_ = false;
    bool vertexInputDirty_ = true;
    bool skipPass_ = false;
    uint32_t enabledAttributes_ = 0;
};

GlDevice::~GlDevice() {
    if (!context_) {
        return;
    }
    for (TimerFrame& frame : timerFrames_) {
        glDeleteQueries(static_cast<GLsizei>(frame.queries.size()), frame.queries.data());
    }
    for (auto& [ids, framebuffer] : multiTargetFramebuffers_) {
        glDeleteFramebuffers(1, &framebuffer);
    }
    pipelines_.forEach([](GlPipeline& p) { glDeleteProgram(p.program); });
    shaders_.forEach([](GlShader& s) { glDeleteShader(s.id); });
    samplers_.forEach([](GlSampler& s) { glDeleteSamplers(1, &s.id); });
    textures_.forEach([](GlTexture& t) {
        if (t.framebuffer) {
            glDeleteFramebuffers(1, &t.framebuffer);
        }
        glDeleteTextures(1, &t.id);
    });
    buffers_.forEach([](GlBuffer& b) { glDeleteBuffers(1, &b.id); });
    if (vertexArray_) {
        glDeleteVertexArrays(1, &vertexArray_);
    }
    SDL_GL_DestroyContext(context_);
}

bool GlDevice::initialize(const DeviceDesc& desc) {
    window_ = desc.window;
    SDL_Window* sdlWindow = window_->sdlWindow();

    context_ = SDL_GL_CreateContext(sdlWindow);
    if (!context_) {
        log::error("Failed to create an OpenGL 4.1 Core context: {}", SDL_GetError());
        return false;
    }
    if (!SDL_GL_MakeCurrent(sdlWindow, context_)) {
        log::error("SDL_GL_MakeCurrent failed: {}", SDL_GetError());
        return false;
    }
    if (!loadFunctions()) {
        return false;
    }

    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 4 || (major == 4 && minor < 1)) {
        log::error("OpenGL 4.1 is required, got {}.{}", major, minor);
        return false;
    }

    if (!SDL_GL_SetSwapInterval(desc.vsync ? 1 : 0)) {
        log::warn("Could not set swap interval: {}", SDL_GetError());
    }

    GLint maxTextureSize = 0;
    GLint uboAlignment = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uboAlignment);

    caps_.apiVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    caps_.deviceName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    caps_.maxTextureSize = static_cast<uint32_t>(maxTextureSize);
    caps_.uniformBufferAlignment = static_cast<uint32_t>(uboAlignment > 0 ? uboAlignment : 256);
    caps_.framesInFlight = 1;
    caps_.computeShaders = false;
    caps_.firstInstance = false;
    GLint timestampBits = 0;
    GLint elapsedBits = 0;
    glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &timestampBits);
    glGetQueryiv(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &elapsedBits);
    timerMode_ = timestampBits > 0 ? TimerMode::Timestamp : (elapsedBits > 0 ? TimerMode::Elapsed : TimerMode::None);
    caps_.gpuTimers = timerMode_ != TimerMode::None;
    for (TimerFrame& frame : timerFrames_) {
        glGenQueries(static_cast<GLsizei>(frame.queries.size()), frame.queries.data());
    }

    glGenVertexArrays(1, &vertexArray_);
    glBindVertexArray(vertexArray_);

    const platform::PixelSize size = window_->pixelSize();
    TextureDesc swapDesc;
    swapDesc.width = size.width ? size.width : 1;
    swapDesc.height = size.height ? size.height : 1;
    swapDesc.format = Format::RGBA8_UNorm;
    swapDesc.usage = TextureUsage::RenderTarget;
    swapDesc.debugName = "swapchain";
    swapchain_ = createTexture(swapDesc);
    if (!swapchain_) {
        return false;
    }
    swapchainExtent_ = {swapDesc.width, swapDesc.height};

    log::info("OpenGL device: {} | {} | max texture {} | UBO alignment {}", caps_.deviceName, caps_.apiVersion,
              caps_.maxTextureSize, caps_.uniformBufferAlignment);
    checkErrors("device initialization");
    return true;
}

BufferHandle GlDevice::createBuffer(const BufferDesc& desc, const void* initialData) {
    if (desc.size == 0) {
        log::error("createBuffer '{}': size must be non-zero", nameOr(desc.debugName, "unnamed"));
        return {};
    }
    GlBuffer buffer;
    buffer.size = desc.size;
    buffer.usage = desc.usage;
    glGenBuffers(1, &buffer.id);
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer.id);
    glBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(desc.size), initialData, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    checkErrors("createBuffer");
    return buffers_.insert(buffer);
}

bool GlDevice::createFramebuffer(GlTexture& texture) {
    glGenFramebuffers(1, &texture.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, texture.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.id, 0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        log::error("Framebuffer incomplete (status 0x{:04X})", static_cast<unsigned>(status));
        glDeleteFramebuffers(1, &texture.framebuffer);
        texture.framebuffer = 0;
        return false;
    }
    return true;
}

GLuint GlDevice::framebufferFor(const RenderPassDesc& desc, Extent2D& extent) {
    AttachmentIds ids{};
    extent = {UINT32_MAX, UINT32_MAX};
    const GlTexture* first = nullptr;
    for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i) {
        const GlTexture* texture = textures_.get(desc.colorAttachments[i].texture);
        if (!texture || !hasFlag(texture->usage, TextureUsage::RenderTarget) || !texture->framebuffer) {
            log::error("OpenGL backend: render pass attachment {} is not a valid render target", i);
            return 0;
        }
        ids[i] = texture->id;
        extent.width = std::min(extent.width, texture->width);
        extent.height = std::min(extent.height, texture->height);
        first = i == 0 ? texture : first;
    }
    if (desc.colorAttachmentCount == 1) {
        return first->framebuffer;
    }
    if (const auto it = multiTargetFramebuffers_.find(ids); it != multiTargetFramebuffers_.end()) {
        return it->second;
    }

    GLuint framebuffer = 0;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    std::array<GLenum, kMaxColorAttachments> drawBuffers{};
    for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, ids[i], 0);
        drawBuffers[i] = GL_COLOR_ATTACHMENT0 + i;
    }
    glDrawBuffers(static_cast<GLsizei>(desc.colorAttachmentCount), drawBuffers.data());
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        log::error("OpenGL backend: multi-target framebuffer incomplete (status 0x{:04X})",
                   static_cast<unsigned>(status));
        glDeleteFramebuffers(1, &framebuffer);
        return 0;
    }
    multiTargetFramebuffers_.emplace(ids, framebuffer);
    return framebuffer;
}

void GlDevice::forgetFramebuffersUsing(GLuint textureId) {
    for (auto it = multiTargetFramebuffers_.begin(); it != multiTargetFramebuffers_.end();) {
        if (std::find(it->first.begin(), it->first.end(), textureId) != it->first.end()) {
            glDeleteFramebuffers(1, &it->second);
            it = multiTargetFramebuffers_.erase(it);
        } else {
            ++it;
        }
    }
}

bool GlDevice::makeTexture(const TextureDesc& desc, GlTexture& texture) {
    const char* name = nameOr(desc.debugName, "unnamed");
    if (desc.width == 0 || desc.height == 0 || desc.width > caps_.maxTextureSize ||
        desc.height > caps_.maxTextureSize) {
        log::error("createTexture '{}': invalid size {}x{}", name, desc.width, desc.height);
        return false;
    }
    const TextureFormatInfo info = textureFormatInfo(desc.format);
    if (info.internalFormat == 0) {
        log::error("createTexture '{}': unsupported format", name);
        return false;
    }

    texture = {};
    texture.width = desc.width;
    texture.height = desc.height;
    texture.format = desc.format;
    texture.usage = desc.usage;
    glGenTextures(1, &texture.id);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, info.internalFormat, static_cast<GLsizei>(desc.width),
                 static_cast<GLsizei>(desc.height), 0, info.format, info.type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (hasFlag(desc.usage, TextureUsage::RenderTarget) && !createFramebuffer(texture)) {
        log::error("createTexture '{}': cannot be used as a render target", name);
        glDeleteTextures(1, &texture.id);
        return false;
    }
    checkErrors("createTexture");
    return true;
}

TextureHandle GlDevice::createTexture(const TextureDesc& desc) {
    GlTexture texture;
    return makeTexture(desc, texture) ? textures_.insert(texture) : TextureHandle{};
}

bool GlDevice::recreateTexture(TextureHandle handle, const TextureDesc& desc) {
    GlTexture* texture = handle == swapchain_ ? nullptr : textures_.get(handle);
    if (!texture) {
        log::error("recreateTexture: invalid texture");
        return false;
    }
    GlTexture fresh;
    if (!makeTexture(desc, fresh)) {
        return false;
    }
    forgetFramebuffersUsing(texture->id);
    if (texture->framebuffer) {
        glDeleteFramebuffers(1, &texture->framebuffer);
    }
    glDeleteTextures(1, &texture->id);
    *texture = fresh; // bind groups refer to the handle and resolve it when bound
    return true;
}

Extent2D GlDevice::textureExtent(TextureHandle handle) const {
    if (handle == swapchain_) {
        return swapchainExtent_;
    }
    const GlTexture* texture = textures_.get(handle);
    return texture ? Extent2D{texture->width, texture->height} : Extent2D{};
}

SamplerHandle GlDevice::createSampler(const SamplerDesc& desc) {
    GlSampler sampler;
    glGenSamplers(1, &sampler.id);
    glSamplerParameteri(sampler.id, GL_TEXTURE_MIN_FILTER, toGl(desc.minFilter));
    glSamplerParameteri(sampler.id, GL_TEXTURE_MAG_FILTER, toGl(desc.magFilter));
    glSamplerParameteri(sampler.id, GL_TEXTURE_WRAP_S, toGl(desc.addressU));
    glSamplerParameteri(sampler.id, GL_TEXTURE_WRAP_T, toGl(desc.addressV));
    checkErrors("createSampler");
    return samplers_.insert(sampler);
}

namespace {

// Links a program and points its resources at the flattened binding slots; 0 on failure (logged).
GLuint linkProgram(GLuint vs, GLuint fs, const std::vector<ShaderBinding>& bindings, const std::string& label) {
    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDetachShader(program, vs);
    glDetachShader(program, fs);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        log::error("OpenGL: link failed for {}:\n{}", label, programInfoLog(program));
        glDeleteProgram(program);
        return 0;
    }

    glUseProgram(program);
    for (const ShaderBinding& b : bindings) {
        const std::string resourceName = glslResourceName(b.kind, b.set, b.binding);
        const GLuint slot = bindingSlot(b.set, b.binding);
        if (b.kind == BindingKind::UniformBuffer) {
            const GLuint blockIndex = glGetUniformBlockIndex(program, resourceName.c_str());
            if (blockIndex != GL_INVALID_INDEX) {
                glUniformBlockBinding(program, blockIndex, slot);
            }
        } else {
            const GLint location = glGetUniformLocation(program, resourceName.c_str());
            if (location >= 0) {
                glUniform1i(location, static_cast<GLint>(slot));
            }
        }
    }
    glUseProgram(0);
    return program;
}

GLuint compileShader(const ShaderPackage& package, const char* name) {
    const GLenum type = package.stage == ShaderStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
    const GLuint id = glCreateShader(type);
    const char* source = package.glsl.c_str();
    glShaderSource(id, 1, &source, nullptr);
    glCompileShader(id);
    GLint compiled = GL_FALSE;
    glGetShaderiv(id, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        log::error("OpenGL: GLSL compilation of '{}' failed:\n{}", name, shaderInfoLog(id));
        glDeleteShader(id);
        return 0;
    }
    return id;
}

} // namespace

ShaderHandle GlDevice::createShader(const ShaderDesc& desc) {
    const char* name = nameOr(desc.debugName, "unnamed");
    if (!desc.package || desc.package->glsl.empty()) {
        log::error("createShader '{}': package has no GLSL code", name);
        return {};
    }
    const GLuint id = compileShader(*desc.package, name);
    if (id == 0) {
        return {};
    }

    GlShader shader;
    shader.id = id;
    shader.stage = desc.package->stage;
    shader.bindings = desc.package->bindings;
    shader.name = name;
    return shaders_.insert(std::move(shader));
}

PipelineHandle GlDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    const char* name = nameOr(desc.debugName, "unnamed");
    const GlShader* vs = shaders_.get(desc.vertexShader);
    const GlShader* fs = shaders_.get(desc.fragmentShader);
    if (!vs || !fs || vs->stage != ShaderStage::Vertex || fs->stage != ShaderStage::Fragment) {
        log::error("createGraphicsPipeline '{}': needs a valid vertex and fragment shader", name);
        return {};
    }
    if (desc.colorTargets.empty() || desc.colorTargets.size() > kMaxColorAttachments) {
        log::error("createGraphicsPipeline '{}': needs 1..{} color targets", name, kMaxColorAttachments);
        return {};
    }
    for (const VertexAttribute& attribute : desc.vertexAttributes) {
        if (attribute.bufferSlot >= desc.vertexBuffers.size() || attribute.location >= kMaxVertexAttributes) {
            log::error("createGraphicsPipeline '{}': vertex attribute {} is out of range", name, attribute.location);
            return {};
        }
    }

    std::vector<ShaderBinding> bindings = vs->bindings;
    for (const ShaderBinding& b : fs->bindings) {
        bool merged = false;
        for (const ShaderBinding& existing : bindings) {
            if (existing.set == b.set && existing.binding == b.binding) {
                if (existing.kind != b.kind) {
                    log::error("createGraphicsPipeline '{}': set {} binding {} has different types per stage", name,
                               b.set, b.binding);
                    return {};
                }
                merged = true;
            }
        }
        if (!merged) {
            bindings.push_back(b);
        }
    }

    const GLuint program = linkProgram(vs->id, fs->id, bindings, std::format("{} ({} + {})", name, vs->name, fs->name));
    if (program == 0) {
        return {};
    }

    GlPipeline pipeline;
    pipeline.program = program;
    pipeline.vertexShader = desc.vertexShader;
    pipeline.fragmentShader = desc.fragmentShader;
    pipeline.name = name;
    pipeline.vertexBuffers = desc.vertexBuffers;
    pipeline.vertexAttributes = desc.vertexAttributes;
    pipeline.topology = toGl(desc.topology);
    pipeline.cullMode = desc.cullMode;
    pipeline.frontFace = desc.frontFace;
    pipeline.colorTargets = desc.colorTargets;
    pipeline.bindings = std::move(bindings);
    return pipelines_.insert(std::move(pipeline));
}

bool GlDevice::reloadShader(ShaderHandle handle, const ShaderDesc& desc) {
    GlShader* shader = shaders_.get(handle);
    const char* name = shader ? shader->name.c_str() : "?";
    if (!shader || !desc.package || desc.package->glsl.empty()) {
        log::error("reloadShader '{}': invalid shader or package", name);
        return false;
    }
    if (desc.package->stage != shader->stage || !sameBindings(desc.package->bindings, shader->bindings)) {
        log::error("reloadShader '{}': the stage or the resource bindings changed; restart to apply", name);
        return false;
    }
    const GLuint id = compileShader(*desc.package, name);
    if (id == 0) {
        return false;
    }
    // Relink every pipeline that uses the shader; commit only if all succeed.
    std::vector<std::pair<GlPipeline*, GLuint>> relinked;
    bool ok = true;
    pipelines_.forEach([&](GlPipeline& pipeline) {
        if (!ok || (pipeline.vertexShader != handle && pipeline.fragmentShader != handle)) {
            return;
        }
        const GlShader* vs = shaders_.get(pipeline.vertexShader);
        const GlShader* fs = shaders_.get(pipeline.fragmentShader);
        if (!vs || !fs) {
            return; // its other shader is gone; the pipeline keeps its program
        }
        const GLuint program = linkProgram(pipeline.vertexShader == handle ? id : vs->id,
                                           pipeline.fragmentShader == handle ? id : fs->id, pipeline.bindings,
                                           std::format("{} (reloading {})", pipeline.name, name));
        if (program == 0) {
            ok = false;
            return;
        }
        relinked.emplace_back(&pipeline, program);
    });
    if (!ok) {
        for (const auto& [pipeline, program] : relinked) {
            glDeleteProgram(program);
        }
        glDeleteShader(id);
        return false;
    }
    for (const auto& [pipeline, program] : relinked) {
        glDeleteProgram(pipeline->program);
        pipeline->program = program;
    }
    glDeleteShader(shader->id);
    shader->id = id;
    checkErrors("reloadShader");
    log::info("Reloaded shader '{}' ({} pipeline(s))", name, relinked.size());
    return true;
}

BindGroupHandle GlDevice::createBindGroup(const BindGroupDesc& desc) {
    const char* name = nameOr(desc.debugName, "unnamed");
    const GlPipeline* pipeline = pipelines_.get(desc.pipeline);
    if (!pipeline) {
        log::error("createBindGroup '{}': invalid layout pipeline", name);
        return {};
    }

    uint32_t provided = 0;
    std::vector<BindGroupEntry> used;
    for (const BindGroupEntry& entry : desc.entries) {
        const ShaderBinding* layout = nullptr;
        for (const ShaderBinding& b : pipeline->bindings) {
            if (b.set == desc.set && b.binding == entry.binding) {
                layout = &b;
            }
        }
        if (!layout) {
            continue; // the shader does not use this resource (any more)
        }
        used.push_back(entry);
        if (layout->kind == BindingKind::UniformBuffer) {
            const GlBuffer* buffer = buffers_.get(entry.buffer);
            if (!buffer || !hasFlag(buffer->usage, BufferUsage::Uniform)) {
                log::error("createBindGroup '{}': binding {} needs a uniform buffer", name, entry.binding);
                return {};
            }
            if (entry.offset % caps_.uniformBufferAlignment != 0 || entry.offset + entry.size > buffer->size) {
                log::error("createBindGroup '{}': binding {} has a misaligned or out-of-range offset", name,
                           entry.binding);
                return {};
            }
        } else {
            const GlTexture* texture = textures_.get(entry.texture);
            if (!texture || !hasFlag(texture->usage, TextureUsage::Sampled) || !samplers_.get(entry.sampler)) {
                log::error("createBindGroup '{}': binding {} needs a sampled texture and a sampler", name,
                           entry.binding);
                return {};
            }
        }
        provided |= 1u << entry.binding;
    }

    for (const ShaderBinding& b : pipeline->bindings) {
        if (b.set == desc.set && !(provided & (1u << b.binding))) {
            log::error("createBindGroup '{}': missing resource for set {} binding {}", name, b.set, b.binding);
            return {};
        }
    }

    return bindGroups_.insert(GlBindGroup{desc.set, std::move(used)});
}

void GlDevice::destroy(BufferHandle handle) {
    if (auto buffer = buffers_.take(handle)) {
        glDeleteBuffers(1, &buffer->id);
    }
}

void GlDevice::destroy(TextureHandle handle) {
    if (handle == swapchain_) {
        log::error("destroy: the swapchain texture is owned by the device");
        return;
    }
    if (auto texture = textures_.take(handle)) {
        forgetFramebuffersUsing(texture->id);
        if (texture->framebuffer) {
            glDeleteFramebuffers(1, &texture->framebuffer);
        }
        glDeleteTextures(1, &texture->id);
    }
}

void GlDevice::destroy(SamplerHandle handle) {
    if (auto sampler = samplers_.take(handle)) {
        glDeleteSamplers(1, &sampler->id);
    }
}

void GlDevice::destroy(ShaderHandle handle) {
    if (auto shader = shaders_.take(handle)) {
        glDeleteShader(shader->id);
    }
}

void GlDevice::destroy(PipelineHandle handle) {
    if (auto pipeline = pipelines_.take(handle)) {
        glDeleteProgram(pipeline->program);
    }
}

void GlDevice::destroy(BindGroupHandle handle) {
    bindGroups_.take(handle);
}

void GlDevice::writeBuffer(BufferHandle handle, uint64_t offset, const void* data, uint64_t size) {
    const GlBuffer* buffer = buffers_.get(handle);
    if (!buffer || offset + size > buffer->size) {
        log::error("writeBuffer: invalid buffer or range [{}, {})", offset, offset + size);
        return;
    }
    if (size == 0) {
        return;
    }
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer->id);
    glBufferSubData(GL_COPY_WRITE_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

void GlDevice::writeTexture(TextureHandle handle, const TextureRegion& region, const void* pixels,
                            uint32_t bytesPerRow) {
    const GlTexture* texture = textures_.get(handle);
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
    const TextureFormatInfo info = textureFormatInfo(texture->format);
    glBindTexture(GL_TEXTURE_2D, texture->id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(bytesPerRow / bpp));
    glTexSubImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(region.x), static_cast<GLint>(region.y),
                    static_cast<GLsizei>(region.width), static_cast<GLsizei>(region.height), info.format, info.type,
                    pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    checkErrors("writeTexture");
}

std::vector<uint8_t> GlDevice::readTexture(TextureHandle handle) {
    GlTexture* texture = textures_.get(handle);
    if (!texture) {
        log::error("readTexture: invalid texture");
        return {};
    }
    if (!texture->framebuffer && !createFramebuffer(*texture)) {
        return {};
    }
    const TextureFormatInfo info = textureFormatInfo(texture->format);
    std::vector<uint8_t> pixels(size_t{texture->width} * texture->height * bytesPerPixel(texture->format));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, texture->framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, static_cast<GLsizei>(texture->width), static_cast<GLsizei>(texture->height), info.format,
                 info.type, pixels.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    checkErrors("readTexture");
    return pixels;
}

void GlDevice::resizeSwapchain(Extent2D extent) {
    GlTexture* texture = textures_.get(swapchain_);
    ANIMAL_ASSERT(texture != nullptr);
    const TextureFormatInfo info = textureFormatInfo(texture->format);
    glBindTexture(GL_TEXTURE_2D, texture->id);
    glTexImage2D(GL_TEXTURE_2D, 0, info.internalFormat, static_cast<GLsizei>(extent.width),
                 static_cast<GLsizei>(extent.height), 0, info.format, info.type, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    texture->width = extent.width;
    texture->height = extent.height;
    swapchainExtent_ = extent;
    log::debug("OpenGL swapchain resized: {}x{}", extent.width, extent.height);
    checkErrors("resizeSwapchain");
}

bool GlDevice::beginFrame() {
    ANIMAL_ASSERT(!frameActive_);
    const platform::PixelSize size = window_->pixelSize();
    if (size.width == 0 || size.height == 0) {
        return false;
    }
    const Extent2D extent{size.width, size.height};
    if (extent != swapchainExtent_) {
        resizeSwapchain(extent);
    }
    timerFrame_ = (timerFrame_ + 1) % kTimerFrames;
    collectTimers(timerFrames_[timerFrame_]);
    frameActive_ = true;
    return true;
}

void GlDevice::recordTimerEvent(uint32_t slot) {
    if (slot == TimerScopes::kNone) {
        return;
    }
    TimerFrame& frame = timerFrames_[timerFrame_];
    if (timerMode_ == TimerMode::Timestamp) {
        glQueryCounter(frame.queries[slot], GL_TIMESTAMP);
        return;
    }
    if (frame.segmentOpen) {
        glEndQuery(GL_TIME_ELAPSED);
    }
    glBeginQuery(GL_TIME_ELAPSED, frame.queries[frame.events]);
    frame.eventSlots[frame.events++] = slot;
    frame.segmentOpen = true;
}

void GlDevice::collectTimers(TimerFrame& frame) {
    const uint32_t queries = timerMode_ == TimerMode::Timestamp ? frame.scopes.used() : frame.events;
    if (frame.pending && queries > 0) {
        GLint available = 0;
        glGetQueryObjectiv(frame.queries[queries - 1], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available) {
            std::vector<uint64_t> values(queries);
            for (uint32_t i = 0; i < queries; ++i) {
                GLuint64 value = 0;
                glGetQueryObjectui64v(frame.queries[i], GL_QUERY_RESULT, &value);
                values[i] = value;
            }
            std::vector<uint64_t> ticks(frame.scopes.used());
            if (timerMode_ == TimerMode::Timestamp) {
                ticks = values;
            } else {
                uint64_t elapsed = 0; // segment i runs from event i to event i + 1
                for (uint32_t i = 0; i < frame.events; ++i) {
                    ticks[frame.eventSlots[i]] = elapsed;
                    elapsed += values[i];
                }
            }
            gpuTimings_ = frame.scopes.timings(ticks, 1.0); // nanoseconds
        }
    }
    frame.pending = false;
    frame.events = 0;
    frame.scopes.reset();
}

void GlDevice::submit(const CommandList& commands) {
    ANIMAL_ASSERT(frameActive_);
    ANIMAL_ASSERT(!commands.insideRenderPass() && commands.openTimers() == 0);
    Executor executor(*this);
    for (const Command& command : commands.commands()) {
        std::visit(executor, command);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    checkErrors("submit");
}

void GlDevice::endFrame() {
    ANIMAL_ASSERT(frameActive_);
    frameActive_ = false;
    TimerFrame& timers = timerFrames_[timerFrame_];
    if (timers.segmentOpen) {
        glEndQuery(GL_TIME_ELAPSED);
        timers.segmentOpen = false;
    }
    timers.pending = !timers.scopes.empty();

    const GlTexture* texture = textures_.get(swapchain_);
    const auto w = static_cast<GLint>(texture->width);
    const auto h = static_cast<GLint>(texture->height);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, texture->framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, w, h, 0, h, w, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    checkErrors("present");

    SDL_GL_SwapWindow(window_->sdlWindow());
}

} // namespace
} // namespace gl

std::unique_ptr<Device> createOpenGLDevice(const DeviceDesc& desc) {
    auto device = std::make_unique<gl::GlDevice>();
    if (!device->initialize(desc)) {
        return nullptr;
    }
    return device;
}

} // namespace animal::rhi
