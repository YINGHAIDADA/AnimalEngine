#pragma once

// Render Hardware Interface. Modeled on Vulkan semantics (immutable pipelines, explicit
// render passes, recorded command lists, set/binding resource groups); see ADR-0001 and
// docs/ARCHITECTURE.md. Nothing in this header may expose a backend API type.

#include <animal/core/handle.h>
#include <animal/rhi/shader_package.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace animal::platform {
class Window;
}

namespace animal::rhi {

class CommandList;

struct BufferTag;
struct TextureTag;
struct SamplerTag;
struct ShaderTag;
struct PipelineTag;
struct BindGroupTag;

using BufferHandle = Handle<BufferTag>;
using TextureHandle = Handle<TextureTag>;
using SamplerHandle = Handle<SamplerTag>;
using ShaderHandle = Handle<ShaderTag>;
using PipelineHandle = Handle<PipelineTag>;
using BindGroupHandle = Handle<BindGroupTag>;

inline constexpr uint32_t kMaxColorAttachments = 4;
inline constexpr uint32_t kMaxVertexBuffers = 4;
inline constexpr uint32_t kMaxVertexAttributes = 8;

enum class Backend : uint8_t {
    OpenGL,
    Vulkan,
};

const char* backendName(Backend backend);
bool isBackendImplemented(Backend backend);

enum class Format : uint8_t {
    Undefined,
    RGBA8_UNorm,
    R8_UNorm,
    RGBA16_Float,
    RGBA32_Float, // simulation state (render target, no blending)
};

uint32_t bytesPerPixel(Format format);

struct Extent2D {
    uint32_t width = 0;
    uint32_t height = 0;

    friend constexpr bool operator==(const Extent2D&, const Extent2D&) = default;
};

// --- Resources -------------------------------------------------------------

enum class BufferUsage : uint32_t {
    Vertex = 1u << 0,
    Index = 1u << 1,
    Uniform = 1u << 2,
};

constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr bool hasFlag(BufferUsage value, BufferUsage flag) {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

struct BufferDesc {
    uint64_t size = 0;
    BufferUsage usage = BufferUsage::Vertex;
    const char* debugName = nullptr;
};

enum class TextureUsage : uint32_t {
    Sampled = 1u << 0,
    RenderTarget = 1u << 1,
};

constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr bool hasFlag(TextureUsage value, TextureUsage flag) {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

// Texture coordinates and pixel rows: origin is the top-left corner, row 0 is the top row.
struct TextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    Format format = Format::RGBA8_UNorm;
    TextureUsage usage = TextureUsage::Sampled;
    const char* debugName = nullptr;
};

struct TextureRegion {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

enum class Filter : uint8_t {
    Nearest,
    Linear,
};

enum class AddressMode : uint8_t {
    ClampToEdge,
    Repeat,
};

struct SamplerDesc {
    Filter minFilter = Filter::Nearest;
    Filter magFilter = Filter::Nearest;
    AddressMode addressU = AddressMode::ClampToEdge;
    AddressMode addressV = AddressMode::ClampToEdge;
};

struct ShaderDesc {
    const ShaderPackage* package = nullptr;
    const char* debugName = nullptr;
};

// --- Pipelines -------------------------------------------------------------

enum class VertexFormat : uint8_t {
    Float2,
    Float3,
    Float4,
    UNorm8x4,
};

enum class VertexStepMode : uint8_t {
    Vertex,
    Instance,
};

struct VertexBufferLayout {
    uint32_t stride = 0;
    VertexStepMode stepMode = VertexStepMode::Vertex;
};

struct VertexAttribute {
    uint32_t location = 0;
    uint32_t bufferSlot = 0;
    VertexFormat format = VertexFormat::Float2;
    uint32_t offset = 0;
};

enum class PrimitiveTopology : uint8_t {
    TriangleList,
    TriangleStrip, // indexed strips must not use the maximum index value (restart on some backends)
    LineList,
};

enum class CullMode : uint8_t {
    None,
    Front,
    Back,
};

// Winding as seen on screen (framebuffer y pointing down), as in Vulkan.
enum class FrontFace : uint8_t {
    CounterClockwise,
    Clockwise,
};

enum class BlendFactor : uint8_t {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstColor,
    OneMinusDstColor,
    DstAlpha,
    OneMinusDstAlpha,
};

enum class BlendOp : uint8_t {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
};

struct BlendState {
    bool enabled = false;
    BlendFactor srcColor = BlendFactor::One;
    BlendFactor dstColor = BlendFactor::Zero;
    BlendOp colorOp = BlendOp::Add;
    BlendFactor srcAlpha = BlendFactor::One;
    BlendFactor dstAlpha = BlendFactor::Zero;
    BlendOp alphaOp = BlendOp::Add;

    static constexpr BlendState opaque() { return {}; }
    static constexpr BlendState premultipliedAlpha() {
        return {true,           BlendFactor::One, BlendFactor::OneMinusSrcAlpha, BlendOp::Add,
                BlendFactor::One, BlendFactor::OneMinusSrcAlpha, BlendOp::Add};
    }
};

struct ColorTargetDesc {
    Format format = Format::RGBA8_UNorm;
    BlendState blend;
};

struct GraphicsPipelineDesc {
    ShaderHandle vertexShader;
    ShaderHandle fragmentShader;
    std::vector<VertexBufferLayout> vertexBuffers;
    std::vector<VertexAttribute> vertexAttributes;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    CullMode cullMode = CullMode::None;
    FrontFace frontFace = FrontFace::CounterClockwise;
    std::vector<ColorTargetDesc> colorTargets;
    const char* debugName = nullptr;
};

// --- Bind groups (Vulkan descriptor sets) ------------------------------------

struct BindGroupEntry {
    uint32_t binding = 0;
    // Uniform buffer (size 0 = whole buffer). Offsets must respect DeviceCaps::uniformBufferAlignment.
    BufferHandle buffer;
    uint64_t offset = 0;
    uint64_t size = 0;
    // Combined image sampler.
    TextureHandle texture;
    SamplerHandle sampler;

    static BindGroupEntry uniformBuffer(uint32_t binding, BufferHandle buffer, uint64_t offset = 0,
                                        uint64_t size = 0) {
        BindGroupEntry e;
        e.binding = binding;
        e.buffer = buffer;
        e.offset = offset;
        e.size = size;
        return e;
    }

    static BindGroupEntry textureSampler(uint32_t binding, TextureHandle texture, SamplerHandle sampler) {
        BindGroupEntry e;
        e.binding = binding;
        e.texture = texture;
        e.sampler = sampler;
        return e;
    }
};

// The layout of `set` is taken from `pipeline`; the group is usable with any pipeline
// that declares an identical layout for that set.
struct BindGroupDesc {
    PipelineHandle pipeline;
    uint32_t set = 0;
    std::vector<BindGroupEntry> entries;
    const char* debugName = nullptr;
};

// --- Render passes ---------------------------------------------------------

enum class LoadOp : uint8_t {
    Load,
    Clear,
    DontCare,
};

enum class StoreOp : uint8_t {
    Store,
    DontCare,
};

struct ClearColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct ColorAttachment {
    TextureHandle texture;
    LoadOp loadOp = LoadOp::Clear;
    StoreOp storeOp = StoreOp::Store;
    ClearColor clearColor;
};

struct RenderPassDesc {
    std::array<ColorAttachment, kMaxColorAttachments> colorAttachments{};
    uint32_t colorAttachmentCount = 0;

    static RenderPassDesc single(const ColorAttachment& attachment) {
        RenderPassDesc desc;
        desc.colorAttachments[0] = attachment;
        desc.colorAttachmentCount = 1;
        return desc;
    }
};

// Origin top-left, in framebuffer pixels.
struct Viewport {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct ScissorRect {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

enum class IndexType : uint8_t {
    UInt16,
    UInt32,
};

// --- Device ----------------------------------------------------------------

struct DeviceCaps {
    std::string apiVersion;
    std::string deviceName;
    uint32_t maxTextureSize = 0;
    uint32_t uniformBufferAlignment = 256;
    uint32_t framesInFlight = 1;
    bool computeShaders = false;
    bool firstInstance = false;
    bool gpuTimers = false; // CommandList timer scopes produce Device::gpuTimings()
};

// One CommandList timer scope, measured on the GPU.
struct GpuTiming {
    const char* name; // as passed to CommandList::beginTimer
    uint32_t depth;   // nesting level, 0 = outermost
    double milliseconds;
};

struct DeviceDesc {
    Backend backend = Backend::OpenGL;
    platform::Window* window = nullptr;
    bool vsync = true;
    bool debug = false;
};

class Device {
public:
    virtual ~Device() = default;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    virtual Backend backend() const = 0;
    virtual const DeviceCaps& caps() const = 0;

    // Creation returns an invalid handle on failure and logs the reason.
    virtual BufferHandle createBuffer(const BufferDesc& desc, const void* initialData = nullptr) = 0;
    virtual TextureHandle createTexture(const TextureDesc& desc) = 0;
    virtual SamplerHandle createSampler(const SamplerDesc& desc) = 0;
    virtual ShaderHandle createShader(const ShaderDesc& desc) = 0;
    virtual PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
    virtual BindGroupHandle createBindGroup(const BindGroupDesc& desc) = 0;

    // Developer hot reload (ADR-0028): replaces the code of a live shader. Pipelines that use it are
    // rebuilt in place, so every handle (shader, pipelines, bind groups) stays valid. The package
    // must keep the stage and the resource bindings; on any failure (compilation, linking, changed
    // bindings) nothing changes, the reason is logged and false is returned.
    virtual bool reloadShader(ShaderHandle shader, const ShaderDesc& desc) = 0;
    // Developer hot reload: gives a live texture a new size and format (same usage). The handle and
    // the bind groups that sample it stay valid; the contents are undefined until written. Between
    // frames only (it may wait for the GPU). On failure nothing changes.
    virtual bool recreateTexture(TextureHandle texture, const TextureDesc& desc) = 0;
    // Current size of a texture (the swapchain texture included); {} for an invalid handle.
    virtual Extent2D textureExtent(TextureHandle texture) const = 0;

    // Resources may still be referenced by frames in flight; backends defer the release.
    virtual void destroy(BufferHandle handle) = 0;
    virtual void destroy(TextureHandle handle) = 0;
    virtual void destroy(SamplerHandle handle) = 0;
    virtual void destroy(ShaderHandle handle) = 0;
    virtual void destroy(PipelineHandle handle) = 0;
    virtual void destroy(BindGroupHandle handle) = 0;

    // Queue-ordered writes: the data is visible to every command list submitted after this
    // call, regardless of when those commands were recorded. Writing the same range twice
    // before one submit means only the last write is seen by that submit.
    virtual void writeBuffer(BufferHandle buffer, uint64_t offset, const void* data, uint64_t size) = 0;
    virtual void writeTexture(TextureHandle texture, const TextureRegion& region, const void* pixels,
                              uint32_t bytesPerRow) = 0;

    // Blocking readback (stalls the GPU); for screenshots and render tests only.
    // Returns tightly packed rows, top row first.
    virtual std::vector<uint8_t> readTexture(TextureHandle texture) = 0;

    // Frame: beginFrame() -> record into a CommandList -> submit() -> endFrame() presents.
    // beginFrame() returns false when there is nothing to present to (e.g. minimized window).
    virtual bool beginFrame() = 0;
    virtual TextureHandle swapchainTexture() const = 0;
    virtual Extent2D swapchainExtent() const = 0;
    virtual Format swapchainFormat() const = 0;
    virtual void submit(const CommandList& commands) = 0;
    virtual void endFrame() = 0;
    virtual void waitIdle() = 0;

    // Timer scopes of the most recent frame whose GPU results are available (a few frames old),
    // in the order they began. Empty when unsupported or nothing has been measured yet.
    virtual std::vector<GpuTiming> gpuTimings() const = 0;

protected:
    Device() = default;
};

// Returns nullptr on failure and logs the reason.
std::unique_ptr<Device> createDevice(const DeviceDesc& desc);

} // namespace animal::rhi
