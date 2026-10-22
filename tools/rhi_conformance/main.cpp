// RHI conformance (ADR-0025): draws one 32×32 tile per RHI feature into an offscreen target and
// writes target + second MRT target side by side as a PNG. render_regression.py runs it on every
// backend; the results must be identical and match tests/golden/rhi_conformance.png.
//
//   row 0: triangle list, triangle strip, line list, cull back (CCW front), cull front (CCW front),
//          instancing, uniform buffer offset, Float3 colour interpolation
//   row 1: nearest clamp, nearest repeat, linear, clamp outside [0, 1], R8, RGBA16F, RGBA32F,
//          partial texture update
//   row 2: blend Add, Subtract, ReverseSubtract, Min, Max, alpha, multiply, opaque overwrite
//   row 3: multiple render targets (second target on the right half), LoadOp::Load across passes
//
// usage: AnimalRhiConformance --backend=opengl|vulkan --out=path.png

#include <animal/core/log.h>
#include <animal/platform/platform.h>
#include <animal/platform/window.h>
#include <animal/resources/loaders.h>
#include <animal/rhi/command_list.h>
#include <animal/rhi/rhi.h>

#include <array>
#include <bit>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace animal;
using namespace animal::rhi;

constexpr uint32_t kWidth = 256;
constexpr uint32_t kHeight = 128;
constexpr float kTile = 32.0f;

struct Vertex {
    float x, y;
    float r, g, b;
    float u, v;
};

uint16_t toHalf(float value) {
    // Exact for the simple values used here (normal range, few mantissa bits).
    const auto bits = std::bit_cast<uint32_t>(value);
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const int exponent = static_cast<int>((bits >> 23) & 0xFFu) - 127 + 15;
    const uint32_t mantissa = (bits >> 13) & 0x3FFu;
    if (value == 0.0f || exponent <= 0) {
        return static_cast<uint16_t>(sign);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | mantissa);
}

class Builder {
public:
    // A quad covering `inset` px inside tile (column, row); returns its first vertex.
    uint32_t quad(int column, int row, std::array<float, 3> c0, std::array<float, 3> c1, std::array<float, 3> c2,
                  std::array<float, 3> c3, float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f,
                  float inset = 4.0f) {
        const float x0 = column * kTile + inset;
        const float y0 = row * kTile + inset;
        const float x1 = (column + 1) * kTile - inset;
        const float y1 = (row + 1) * kTile - inset;
        return add({{x0, y0, c0[0], c0[1], c0[2], u0, v0},
                    {x1, y0, c1[0], c1[1], c1[2], u1, v0},
                    {x1, y1, c2[0], c2[1], c2[2], u1, v1},
                    {x0, y0, c0[0], c0[1], c0[2], u0, v0},
                    {x1, y1, c2[0], c2[1], c2[2], u1, v1},
                    {x0, y1, c3[0], c3[1], c3[2], u0, v1}});
    }
    uint32_t add(std::initializer_list<Vertex> vertices) {
        const auto first = static_cast<uint32_t>(vertices_.size());
        vertices_.insert(vertices_.end(), vertices);
        return first;
    }
    const std::vector<Vertex>& vertices() const { return vertices_; }

private:
    std::vector<Vertex> vertices_;
};

constexpr std::array<float, 3> kWhite{1, 1, 1};
constexpr std::array<float, 3> kRed{1, 0, 0};
constexpr std::array<float, 3> kGreen{0, 1, 0};
constexpr std::array<float, 3> kBlue{0, 0, 1};
constexpr std::array<float, 3> kGrey{0.5f, 0.5f, 0.5f};
constexpr std::array<float, 3> kOverlay{0.25f, 0.5f, 0.75f};

int run(Backend backend, const std::string& out) {
    platform::WindowDesc windowDesc;
    windowDesc.title = "Animal Engine RHI conformance";
    windowDesc.width = 320;
    windowDesc.height = 180;
    windowDesc.api = backend == Backend::Vulkan ? platform::GraphicsApi::Vulkan : platform::GraphicsApi::OpenGL;
    std::unique_ptr<platform::Window> window = platform::Window::create(windowDesc);
    if (!window) {
        return 1;
    }
    std::unique_ptr<Device> device = createDevice({backend, window.get(), false, true});
    if (!device) {
        return 1;
    }
    Device& d = *device;
    const std::string shaders = platform::executableDirectory() + "data/shaders/";
    const ShaderHandle vs = loadShader(d, shaders + "conformance.vert.ashd");
    const ShaderHandle fs = loadShader(d, shaders + "conformance.frag.ashd");
    const ShaderHandle mrtFs = loadShader(d, shaders + "conformance_mrt.frag.ashd");
    if (!vs || !fs || !mrtFs) {
        return 1;
    }

    // --- Pipelines -----------------------------------------------------------------------
    auto pipeline = [&](const char* name, PrimitiveTopology topology, CullMode cull, FrontFace front, BlendState blend,
                        bool mrt = false) {
        GraphicsPipelineDesc desc;
        desc.vertexShader = vs;
        desc.fragmentShader = mrt ? mrtFs : fs;
        desc.vertexBuffers = {{sizeof(Vertex), VertexStepMode::Vertex}, {2 * sizeof(float), VertexStepMode::Instance}};
        desc.vertexAttributes = {{0, 0, VertexFormat::Float2, offsetof(Vertex, x)},
                                 {1, 0, VertexFormat::Float3, offsetof(Vertex, r)},
                                 {2, 0, VertexFormat::Float2, offsetof(Vertex, u)},
                                 {3, 1, VertexFormat::Float2, 0}};
        desc.topology = topology;
        desc.cullMode = cull;
        desc.frontFace = front;
        desc.colorTargets = {{Format::RGBA8_UNorm, blend}};
        if (mrt) {
            desc.colorTargets.push_back({Format::RGBA8_UNorm, blend});
        }
        desc.debugName = name;
        return d.createGraphicsPipeline(desc);
    };
    const BlendState opaque = BlendState::opaque();
    auto blendOp = [](BlendOp op, BlendFactor src = BlendFactor::One, BlendFactor dst = BlendFactor::One) {
        return BlendState{true, src, dst, op, BlendFactor::One, BlendFactor::Zero, BlendOp::Add};
    };
    using T = PrimitiveTopology;
    const PipelineHandle triangles = pipeline("triangles", T::TriangleList, CullMode::None, FrontFace::CounterClockwise, opaque);
    const PipelineHandle strip = pipeline("strip", T::TriangleStrip, CullMode::None, FrontFace::CounterClockwise, opaque);
    const PipelineHandle lines = pipeline("lines", T::LineList, CullMode::None, FrontFace::CounterClockwise, opaque);
    const PipelineHandle cullBack = pipeline("cull back", T::TriangleList, CullMode::Back, FrontFace::CounterClockwise, opaque);
    const PipelineHandle cullFront = pipeline("cull front", T::TriangleList, CullMode::Front, FrontFace::CounterClockwise, opaque);
    const std::array<PipelineHandle, 7> blends = {
        pipeline("add", T::TriangleList, CullMode::None, FrontFace::CounterClockwise, blendOp(BlendOp::Add)),
        pipeline("subtract", T::TriangleList, CullMode::None, FrontFace::CounterClockwise, blendOp(BlendOp::Subtract)),
        pipeline("reverse subtract", T::TriangleList, CullMode::None, FrontFace::CounterClockwise,
                 blendOp(BlendOp::ReverseSubtract)),
        pipeline("min", T::TriangleList, CullMode::None, FrontFace::CounterClockwise, blendOp(BlendOp::Min)),
        pipeline("max", T::TriangleList, CullMode::None, FrontFace::CounterClockwise, blendOp(BlendOp::Max)),
        pipeline("alpha", T::TriangleList, CullMode::None, FrontFace::CounterClockwise,
                 blendOp(BlendOp::Add, BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha)),
        pipeline("multiply", T::TriangleList, CullMode::None, FrontFace::CounterClockwise,
                 blendOp(BlendOp::Add, BlendFactor::DstColor, BlendFactor::Zero)),
    };
    const PipelineHandle mrt = pipeline("mrt", T::TriangleList, CullMode::None, FrontFace::CounterClockwise, opaque, true);

    // --- Buffers, textures, bind groups -------------------------------------------------------
    const uint32_t align = std::max<uint32_t>(256, d.caps().uniformBufferAlignment);
    struct Draw {
        float tint[4];
        float target[4];
    };
    const std::array<Draw, 4> draws = {{
        {{1, 1, 1, 1}, {kWidth, kHeight, 0, 0}},          // vertex colour
        {{1, 1, 1, 1}, {kWidth, kHeight, 1, 0}},          // texture
        {{1, 0.5f, 0.25f, 1}, {kWidth, kHeight, 0, 0}},   // tinted (uniform offset test)
        {{1, 1, 1, 0.5f}, {kWidth, kHeight, 0, 0}},       // half alpha
    }};
    std::vector<uint8_t> uniformData(align * draws.size());
    for (size_t i = 0; i < draws.size(); ++i) {
        std::memcpy(uniformData.data() + i * align, &draws[i], sizeof(Draw));
    }
    const BufferHandle uniforms =
        d.createBuffer({uniformData.size(), BufferUsage::Uniform, "conformance uniforms"}, uniformData.data());
    std::array<BindGroupHandle, 4> drawGroups{};
    for (size_t i = 0; i < draws.size(); ++i) {
        drawGroups[i] = d.createBindGroup(
            {triangles, 0, {BindGroupEntry::uniformBuffer(0, uniforms, i * align, sizeof(Draw))}, "conformance draw"});
    }

    auto texture = [&](uint32_t w, uint32_t h, Format format, const void* data, uint32_t rowBytes, const char* name) {
        const TextureHandle t = d.createTexture({w, h, format, TextureUsage::Sampled, name});
        d.writeTexture(t, {0, 0, w, h}, data, rowBytes);
        return t;
    };
    std::array<uint8_t, 4 * 4 * 4> checkerData{};
    for (int i = 0; i < 16; ++i) {
        const uint8_t v = ((i % 4) + (i / 4)) % 2 ? 255 : 40;
        checkerData[i * 4 + 0] = v;
        checkerData[i * 4 + 1] = static_cast<uint8_t>(255 - v / 2);
        checkerData[i * 4 + 2] = static_cast<uint8_t>(i * 16);
        checkerData[i * 4 + 3] = 255;
    }
    const TextureHandle checker = texture(4, 4, Format::RGBA8_UNorm, checkerData.data(), 16, "checker");
    const std::array<uint8_t, 4> redData = {0, 85, 170, 255};
    const TextureHandle r8 = texture(2, 2, Format::R8_UNorm, redData.data(), 2, "r8");
    std::array<uint16_t, 16> halfData{};
    const float halves[16] = {1, 0.25f, 0, 1, 0, 0.5f, 1, 1, 0.75f, 0.75f, 0.25f, 1, 0.5f, 0, 0.5f, 1};
    for (int i = 0; i < 16; ++i) {
        halfData[i] = toHalf(halves[i]);
    }
    const TextureHandle rgba16 = texture(2, 2, Format::RGBA16_Float, halfData.data(), 2 * 8, "rgba16f");
    const float floats[16] = {0.2f, 0.4f, 0.6f, 1, 0.9f, 0.1f, 0.3f, 1, 0, 1, 0.5f, 1, 1, 1, 0, 1};
    const TextureHandle rgba32 = texture(2, 2, Format::RGBA32_Float, floats, 2 * 16, "rgba32f");
    std::vector<uint8_t> greyData(8 * 8 * 4, 128);
    const TextureHandle partial = texture(8, 8, Format::RGBA8_UNorm, greyData.data(), 32, "partial");
    std::vector<uint8_t> patch(4 * 4 * 4);
    for (size_t i = 0; i < patch.size(); i += 4) {
        patch[i] = 255;
        patch[i + 1] = 40;
        patch[i + 2] = 40;
        patch[i + 3] = 255;
    }
    d.writeTexture(partial, {2, 3, 4, 4}, patch.data(), 16);

    const SamplerHandle nearestClamp = d.createSampler({});
    const SamplerHandle nearestRepeat =
        d.createSampler({Filter::Nearest, Filter::Nearest, AddressMode::Repeat, AddressMode::Repeat});
    const SamplerHandle linearClamp = d.createSampler({Filter::Linear, Filter::Linear});
    auto image = [&](TextureHandle t, SamplerHandle s) {
        return d.createBindGroup({triangles, 1, {BindGroupEntry::textureSampler(0, t, s)}, "conformance image"});
    };

    // --- Geometry ---------------------------------------------------------------------------------
    Builder b;
    const uint32_t tri = b.add({{4, 28, 1, 0, 0, 0, 0}, {16, 4, 0, 1, 0, 0, 0}, {28, 28, 0, 0, 1, 0, 0}});
    const uint32_t stripFirst = b.add({{36, 4, 1, 0, 0, 0, 0}, {60, 4, 0, 1, 0, 0, 0},
                                       {36, 28, 0, 0, 1, 0, 0}, {60, 28, 1, 1, 1, 0, 0}});
    const uint32_t lineFirst = b.add({{68, 6, 1, 1, 1, 0, 0}, {92, 6, 1, 1, 1, 0, 0},
                                      {70, 4, 1, 1, 0, 0, 0}, {70, 28, 1, 1, 0, 0, 0},
                                      {72, 26, 0, 1, 1, 0, 0}, {92, 10, 0, 1, 1, 0, 0}});
    // Two triangles per cull tile: the left one counter-clockwise on screen, the right one clockwise.
    auto cullPair = [&](int column) {
        const float x = column * kTile;
        return b.add({{x + 2, 28, 1, 1, 0, 0, 0}, {x + 14, 28, 1, 1, 0, 0, 0}, {x + 8, 4, 1, 1, 0, 0, 0},
                      {x + 18, 28, 0, 1, 1, 0, 0}, {x + 24, 4, 0, 1, 1, 0, 0}, {x + 30, 28, 0, 1, 1, 0, 0}});
    };
    const uint32_t cullBackFirst = cullPair(3);
    const uint32_t cullFrontFirst = cullPair(4);
    const uint32_t small = b.add({{5 * kTile + 2, 2, 1, 0.5f, 0, 0, 0}, {5 * kTile + 10, 2, 1, 0.5f, 0, 0, 0},
                                  {5 * kTile + 10, 10, 1, 0.5f, 0, 0, 0}, {5 * kTile + 2, 2, 1, 0.5f, 0, 0, 0},
                                  {5 * kTile + 10, 10, 1, 0.5f, 0, 0, 0}, {5 * kTile + 2, 10, 1, 0.5f, 0, 0, 0}});
    const uint32_t tinted = b.quad(6, 0, kWhite, kWhite, kWhite, kWhite);
    const uint32_t gradient = b.quad(7, 0, kRed, kGreen, kBlue, kWhite);
    const uint32_t samplingClamp = b.quad(0, 1, kWhite, kWhite, kWhite, kWhite);
    const uint32_t samplingRepeat = b.quad(1, 1, kWhite, kWhite, kWhite, kWhite, 0, 0, 2.5f, 2.5f);
    const uint32_t samplingLinear = b.quad(2, 1, kWhite, kWhite, kWhite, kWhite);
    const uint32_t samplingOutside = b.quad(3, 1, kWhite, kWhite, kWhite, kWhite, -0.5f, -0.5f, 1.5f, 1.5f);
    std::array<uint32_t, 4> formatQuads{};
    for (int i = 0; i < 4; ++i) {
        formatQuads[i] = b.quad(4 + i, 1, kWhite, kWhite, kWhite, kWhite);
    }
    std::array<uint32_t, 8> backgrounds{};
    std::array<uint32_t, 8> overlays{};
    for (int i = 0; i < 8; ++i) {
        backgrounds[i] = b.quad(i, 2, kGrey, kGrey, kGrey, kGrey, 0, 0, 1, 1, 2.0f);
        overlays[i] = b.quad(i, 2, kOverlay, kOverlay, kOverlay, kOverlay, 0, 0, 1, 1, 8.0f);
    }
    const uint32_t mrtQuad = b.quad(0, 3, kRed, kGreen, kBlue, kWhite);
    const uint32_t loadFirst = b.add({{36, 124, 1, 0, 1, 0, 0}, {48, 100, 1, 0, 1, 0, 0}, {60, 124, 1, 0, 1, 0, 0}});
    const uint32_t loadSecond = b.add({{36, 100, 0, 1, 0, 0, 0}, {60, 100, 0, 1, 0, 0, 0}, {48, 124, 0, 1, 0, 0, 0}});

    const BufferHandle vertices = d.createBuffer(
        {b.vertices().size() * sizeof(Vertex), BufferUsage::Vertex, "conformance vertices"}, b.vertices().data());
    const float single[2] = {0, 0};
    const BufferHandle oneInstance = d.createBuffer({sizeof(single), BufferUsage::Vertex, "one instance"}, single);
    const float offsets[8] = {0, 0, 12, 0, 0, 12, 18, 18};
    const BufferHandle fourInstances = d.createBuffer({sizeof(offsets), BufferUsage::Vertex, "four instances"}, offsets);

    const TextureHandle target =
        d.createTexture({kWidth, kHeight, Format::RGBA8_UNorm, TextureUsage::RenderTarget | TextureUsage::Sampled, "target"});
    const TextureHandle second =
        d.createTexture({kWidth, kHeight, Format::RGBA8_UNorm, TextureUsage::RenderTarget | TextureUsage::Sampled, "second"});

    // --- Record ------------------------------------------------------------------------------------
    if (!d.beginFrame()) {
        log::error("conformance: beginFrame failed");
        return 1;
    }
    CommandList list;
    auto use = [&](PipelineHandle p, BindGroupHandle drawGroup, BindGroupHandle imageGroup, BufferHandle instances) {
        list.setPipeline(p);
        list.setVertexBuffer(0, vertices);
        list.setVertexBuffer(1, instances);
        list.setBindGroup(0, drawGroup);
        if (imageGroup) {
            list.setBindGroup(1, imageGroup);
        }
    };
    const BindGroupHandle checkerNearest = image(checker, nearestClamp);
    list.beginRenderPass(RenderPassDesc::single({target, LoadOp::Clear, StoreOp::Store, {0.05f, 0.05f, 0.15f, 1.0f}}));
    use(triangles, drawGroups[0], checkerNearest, oneInstance);
    list.draw(3, 1, tri);
    list.draw(3, 1, loadFirst);
    use(strip, drawGroups[0], checkerNearest, oneInstance);
    list.draw(4, 1, stripFirst);
    use(lines, drawGroups[0], checkerNearest, oneInstance);
    list.draw(6, 1, lineFirst);
    use(cullBack, drawGroups[0], checkerNearest, oneInstance);
    list.draw(6, 1, cullBackFirst);
    use(cullFront, drawGroups[0], checkerNearest, oneInstance);
    list.draw(6, 1, cullFrontFirst);
    use(triangles, drawGroups[0], checkerNearest, fourInstances);
    list.draw(6, 4, small);
    use(triangles, drawGroups[2], checkerNearest, oneInstance);
    list.draw(6, 1, tinted);
    use(triangles, drawGroups[0], checkerNearest, oneInstance);
    list.draw(6, 1, gradient);
    use(triangles, drawGroups[1], checkerNearest, oneInstance);
    list.draw(6, 1, samplingClamp);
    use(triangles, drawGroups[1], image(checker, nearestRepeat), oneInstance);
    list.draw(6, 1, samplingRepeat);
    use(triangles, drawGroups[1], image(checker, linearClamp), oneInstance);
    list.draw(6, 1, samplingLinear);
    use(triangles, drawGroups[1], checkerNearest, oneInstance);
    list.draw(6, 1, samplingOutside);
    const std::array<TextureHandle, 4> formatTextures = {r8, rgba16, rgba32, partial};
    for (size_t i = 0; i < formatTextures.size(); ++i) {
        use(triangles, drawGroups[1], image(formatTextures[i], nearestClamp), oneInstance);
        list.draw(6, 1, formatQuads[i]);
    }
    for (int i = 0; i < 8; ++i) {
        use(triangles, drawGroups[0], checkerNearest, oneInstance);
        list.draw(6, 1, backgrounds[i]);
        use(i < 7 ? blends[static_cast<size_t>(i)] : triangles, drawGroups[i == 5 ? 3 : 0], checkerNearest, oneInstance);
        list.draw(6, 1, overlays[i]);
    }
    list.endRenderPass();

    RenderPassDesc both;
    both.colorAttachments[0] = {target, LoadOp::Load, StoreOp::Store, {}};
    both.colorAttachments[1] = {second, LoadOp::Clear, StoreOp::Store, {0, 0, 0, 1}};
    both.colorAttachmentCount = 2;
    list.beginRenderPass(both);
    use(mrt, drawGroups[0], {}, oneInstance);
    list.draw(6, 1, mrtQuad);
    list.draw(3, 1, loadSecond);
    list.endRenderPass();
    d.submit(list);

    const std::vector<uint8_t> left = d.readTexture(target);
    const std::vector<uint8_t> right = d.readTexture(second);
    d.endFrame();
    d.waitIdle();
    if (left.size() != kWidth * kHeight * 4 || right.size() != left.size()) {
        log::error("conformance: readback failed");
        return 1;
    }
    std::vector<uint8_t> image2x(left.size() * 2);
    for (uint32_t y = 0; y < kHeight; ++y) {
        std::memcpy(&image2x[(y * 2 * kWidth) * 4], &left[y * kWidth * 4], kWidth * 4);
        std::memcpy(&image2x[(y * 2 * kWidth + kWidth) * 4], &right[y * kWidth * 4], kWidth * 4);
    }
    if (!writePng(out, kWidth * 2, kHeight, image2x.data())) {
        log::error("conformance: cannot write {}", out);
        return 1;
    }
    log::info("conformance: wrote {}", out);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Backend backend = Backend::OpenGL;
    std::string out = "rhi_conformance.png";
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--backend=vulkan") {
            backend = Backend::Vulkan;
        } else if (arg == "--backend=opengl") {
            backend = Backend::OpenGL;
        } else if (arg.starts_with("--out=")) {
            out = std::string(arg.substr(6));
        } else {
            log::error("usage: AnimalRhiConformance --backend=opengl|vulkan --out=path.png");
            return 2;
        }
    }
    if (!platform::initialize()) {
        return 1;
    }
    const int result = run(backend, out);
    platform::shutdown();
    return result;
}
