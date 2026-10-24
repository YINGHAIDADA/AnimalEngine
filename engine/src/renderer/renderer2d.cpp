#include "debug_font.h"
#include "fluid_pass.h"
#include "gpu_particle_pass.h"
#include "lighting_pass.h"
#include "post_pass.h"
#include "water_pass.h"

#include <animal/core/assert.h>
#include <animal/core/log.h>
#include <animal/renderer/renderer2d.h>
#include <animal/rhi/command_list.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace animal {

namespace {

rhi::ClearColor toClearColor(Color c) {
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
}

// G-buffer clear values: "nothing here" is a flat normal and no emission.
constexpr rhi::ClearColor kFlatNormalClear{0.5f, 0.5f, 1.0f, 0.0f};
constexpr rhi::ClearColor kNoEmissionClear{0.0f, 0.0f, 0.0f, 0.0f};

} // namespace

struct WaterFrame : WaterPass::Frame {
    size_t styleCount = 0;
};

Renderer2D::Renderer2D()
    : font_(std::make_unique<DebugFont>()),
      lightingPass_(std::make_unique<LightingPass>()),
      fluidPass_(std::make_unique<FluidPass>()),
      postPass_(std::make_unique<PostPass>()),
      waterPass_(std::make_unique<WaterPass>()),
      gpuParticles_(std::make_unique<GpuParticlePass>()),
      water_(std::make_unique<WaterFrame>()) {}

Renderer2D::~Renderer2D() = default;

rhi::TextureHandle Renderer2D::createTarget(rhi::Format format, const char* name) {
    rhi::TextureDesc desc;
    desc.width = kCanvasExtent.width;
    desc.height = kCanvasExtent.height;
    desc.format = format;
    desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderTarget;
    desc.debugName = name;
    return device_->createTexture(desc);
}

rhi::TextureHandle Renderer2D::createSolidTexture(Color color, const char* name) {
    rhi::TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.debugName = name;
    const rhi::TextureHandle texture = device_->createTexture(desc);
    if (texture) {
        device_->writeTexture(texture, {0, 0, 1, 1}, &color, 4);
    }
    return texture;
}

bool Renderer2D::init(rhi::Device& device, const std::string& dataDirectory) {
    device_ = &device;
    dataDirectory_ = dataDirectory;
    const std::string shaders = dataDirectory + "shaders/";

    SpriteBatchDesc unlit;
    unlit.colorTargets = {{device.swapchainFormat(), rhi::BlendState::premultipliedAlpha()}};
    unlit.debugName = "sprite";

    SpriteBatchDesc lit;
    lit.fragmentShader = "sprite_lit.frag.ashd";
    lit.colorTargets = {
        {rhi::Format::RGBA8_UNorm, rhi::BlendState::premultipliedAlpha()}, // albedo
        {rhi::Format::RGBA8_UNorm, rhi::BlendState::premultipliedAlpha()}, // normal
        {rhi::Format::RGBA8_UNorm, rhi::BlendState::premultipliedAlpha()}, // emissive
        {rhi::Format::RGBA8_UNorm, rhi::BlendState::premultipliedAlpha()}, // material flags (r: rim)
    };
    lit.texturesPerSprite = 3;
    lit.debugName = "sprite lit";

    SpriteBatchDesc glow;
    glow.colorTargets = {{rhi::Format::RGBA8_UNorm,
                          {true, rhi::BlendFactor::One, rhi::BlendFactor::One, rhi::BlendOp::Add, rhi::BlendFactor::One,
                           rhi::BlendFactor::One, rhi::BlendOp::Add}}};
    glow.debugName = "sprite glow";

    if (!batch_.init(device, shaders, unlit) || !litBatch_.init(device, shaders, lit) ||
        !glowBatch_.init(device, shaders, glow)) {
        log::error("Renderer2D: sprite batch initialization failed (is '{}' deployed?)", dataDirectory);
        return false;
    }
    if (!font_->init(device)) {
        return false;
    }

    canvas_ = createTarget(rhi::Format::RGBA8_UNorm, "pixel canvas");
    final_ = createTarget(rhi::Format::RGBA8_UNorm, "final canvas");
    albedo_ = createTarget(rhi::Format::RGBA8_UNorm, "g-buffer albedo");
    normal_ = createTarget(rhi::Format::RGBA8_UNorm, "g-buffer normal");
    emissive_ = createTarget(rhi::Format::RGBA8_UNorm, "g-buffer emissive");
    flags_ = createTarget(rhi::Format::RGBA8_UNorm, "g-buffer material flags");
    light_ = createTarget(rhi::Format::RGBA16_Float, "light accumulation");
    rimLight_ = createTarget(rhi::Format::RGBA16_Float, "rim light accumulation");
    lit_ = createTarget(rhi::Format::RGBA8_UNorm, "lit scene");
    flatNormal_ = createSolidTexture(Color::rgba(128, 128, 255), "flat normal");
    noEmission_ = createSolidTexture(Color::rgba(0, 0, 0), "no emission");
    if (!canvas_ || !final_ || !albedo_ || !normal_ || !emissive_ || !flags_ || !light_ || !rimLight_ || !lit_ || !flatNormal_ || !noEmission_) {
        return false;
    }

    if (!fluidPass_->init(device, shaders)) {
        return false;
    }
    LightingPass::Targets targets{albedo_, normal_, emissive_, flags_, light_, rimLight_, fluidPass_->smokeTexture(),
                                  kCanvasExtent};
    if (!waterPass_->init(device, shaders, {lit_, light_, canvas_, kCanvasExtent})) {
        return false;
    }
    if (!lightingPass_->init(device, shaders, targets, rhi::Format::RGBA8_UNorm)) {
        return false;
    }
    const GpuParticlePass::Formats particleFormats{
        {rhi::Format::RGBA8_UNorm, rhi::Format::RGBA8_UNorm, rhi::Format::RGBA8_UNorm, rhi::Format::RGBA8_UNorm},
        rhi::Format::RGBA8_UNorm};
    if (!gpuParticles_->init(device, shaders, lightingPass_->occluders(), kCanvasExtent, particleFormats)) {
        return false;
    }
    return
           postPass_->init(device, shaders, canvas_, final_, kCanvasExtent);
}

void Renderer2D::shutdown() {
    if (!device_) {
        return;
    }
    postPass_->shutdown();
    waterPass_->shutdown();
    gpuParticles_->shutdown();
    lightingPass_->shutdown();
    fluidPass_->shutdown();
    glowBatch_.shutdown();
    litBatch_.shutdown();
    batch_.shutdown();
    font_->shutdown(*device_);
    for (rhi::TextureHandle texture : {canvas_, final_, albedo_, normal_, emissive_, flags_, light_, rimLight_, lit_, flatNormal_, noEmission_}) {
        device_->destroy(texture);
    }
    device_ = nullptr;
}

bool Renderer2D::rebuild() {
    ANIMAL_ASSERT(device_ != nullptr && phase_ == Phase::Idle && commands_ == nullptr);
    Renderer2D fresh;
    if (!fresh.init(*device_, dataDirectory_)) {
        fresh.shutdown();
        log::error("Renderer2D: rebuild failed; the current renderer stays in use");
        return false;
    }
    swapResources(fresh);
    fresh.shutdown(); // releases the previous resources
    log::info("Renderer2D: rebuilt from the shader files");
    return true;
}

// Everything created by init(); settings and per-frame collections stay with this renderer.
void Renderer2D::swapResources(Renderer2D& other) {
    using std::swap;
    swap(batch_, other.batch_);
    swap(litBatch_, other.litBatch_);
    swap(glowBatch_, other.glowBatch_);
    swap(font_, other.font_);
    swap(lightingPass_, other.lightingPass_);
    swap(fluidPass_, other.fluidPass_);
    swap(postPass_, other.postPass_);
    swap(waterPass_, other.waterPass_);
    swap(gpuParticles_, other.gpuParticles_);
    swap(water_, other.water_);
    swap(fluidQuietSeconds_, other.fluidQuietSeconds_);
    for (auto [mine, theirs] : {std::pair{&canvas_, &other.canvas_}, {&final_, &other.final_},
                                {&albedo_, &other.albedo_}, {&normal_, &other.normal_},
                                {&emissive_, &other.emissive_}, {&light_, &other.light_},
                                {&rimLight_, &other.rimLight_}, {&lit_, &other.lit_}, {&flags_, &other.flags_},
                                {&flatNormal_, &other.flatNormal_}, {&noEmission_, &other.noEmission_}}) {
        swap(*mine, *theirs);
    }
}

void Renderer2D::beginFrame(rhi::CommandList& commands) {
    commands_ = &commands;
    commands_->beginTimer("frame");
    batch_.beginFrame();
    litBatch_.beginFrame();
    glowBatch_.beginFrame();
    lightingPass_->beginFrame();
    fluidPass_->beginFrame();
}

void Renderer2D::beginWorld(const Camera2D& camera, Color clearColor) {
    ANIMAL_ASSERT(commands_ != nullptr && phase_ == Phase::Idle);
    phase_ = Phase::World;
    worldViewProjection_ = camera.viewProjection(kCanvasExtent);
    worldOrigin_ = camera.topLeft(kCanvasExtent);
    lights_.clear();
    glow_.clear();
    fluidSplats_.clear();
    water_->columns.assign(size_t{kCanvasExtent.width} * 4, 0);
    water_->styleCount = 0;

    rhi::RenderPassDesc pass;
    pass.colorAttachments[0] = {albedo_, rhi::LoadOp::Clear, rhi::StoreOp::Store, toClearColor(clearColor)};
    pass.colorAttachments[1] = {normal_, rhi::LoadOp::Clear, rhi::StoreOp::Store, kFlatNormalClear};
    pass.colorAttachments[2] = {emissive_, rhi::LoadOp::Clear, rhi::StoreOp::Store, kNoEmissionClear};
    pass.colorAttachments[3] = {flags_, rhi::LoadOp::Clear, rhi::StoreOp::Store, {0.0f, 0.0f, 0.0f, 0.0f}};
    pass.colorAttachmentCount = 4;
    if (gpuParticles_->active()) {
        commands_->beginTimer("gpu particles");
        gpuParticles_->update(*commands_, gpuParticleStep_, gpuParticleWind_, worldOrigin_);
        commands_->endTimer();
    }
    commands_->beginTimer("material");
    commands_->beginRenderPass(pass);
    litBatch_.begin(*commands_, worldViewProjection_);
}

void Renderer2D::drawSprite(const SpriteMaterial& material, Vec2 position, const RectF& source, Color tint,
                            bool flipX) {
    const LoadedTexture& texture = material.albedo;
    const rhi::Extent2D size = device_->textureExtent(texture.handle); // current size (hot reload may resize)
    if (size.width == 0 || size.height == 0) {
        return;
    }
    const Vec2 p = roundToPixel(position);
    const auto tw = static_cast<float>(size.width);
    const auto th = static_cast<float>(size.height);
    const auto ox = static_cast<float>(texture.x); // atlas region
    const auto oy = static_cast<float>(texture.y);
    RectF uv{(ox + source.x) / tw, (oy + source.y) / th, source.width / tw, source.height / th};
    if (flipX) {
        uv.x += uv.width;
        uv.width = -uv.width;
    }
    const RectF dst{p.x, p.y, source.width, source.height};
    if (phase_ == Phase::World) {
        litBatch_.draw({texture.handle, material.normal ? material.normal : flatNormal_,
                        material.emissive ? material.emissive : noEmission_},
                       dst, uv, tint, material.rim);
    } else {
        batch_.draw(texture.handle, dst, uv, tint);
    }
}

void Renderer2D::drawSprite(const LoadedTexture& texture, Vec2 position, const RectF& source, Color tint,
                            bool flipX) {
    SpriteMaterial material;
    material.albedo = texture;
    drawSprite(material, position, source, tint, flipX);
}

void Renderer2D::drawGlow(Vec2 position, Color color, float size) {
    ANIMAL_ASSERT(phase_ == Phase::World);
    const Vec2 p = roundToPixel(position);
    glow_.push_back({{p.x, p.y, size, size}, color});
}

void Renderer2D::addLight(const PointLight& light) {
    ANIMAL_ASSERT(phase_ == Phase::World);
    lights_.push_back(light);
}

void Renderer2D::addWater(const WaterSurface& surface) {
    ANIMAL_ASSERT(phase_ == Phase::World);
    if (water_->styleCount >= kMaxWaterStyles) {
        log::warn("Renderer2D: more than {} water surfaces this frame; extra ones are not drawn", kMaxWaterStyles);
        return;
    }
    const auto style = static_cast<uint8_t>(water_->styleCount);
    WaterSurface& stored = water_->styles[style];
    stored = surface;
    stored.offsets = {};
    if (style == 0) {
        water_->phase = static_cast<int>(std::floor(surface.time * 8.0f));
    }
    ++water_->styleCount;

    // Whole canvas rows, so the shader's decisions are exact integers.
    const int left = static_cast<int>(std::lround(surface.topLeft.x - worldOrigin_.x));
    const int height = static_cast<int>(kCanvasExtent.height);
    const int bottom = std::clamp(static_cast<int>(std::lround(surface.topLeft.y + surface.depth - worldOrigin_.y)), 0,
                                  height);
    for (int i = 0; i < surface.width; ++i) {
        const int x = left + i;
        if (x < 0 || x >= static_cast<int>(kCanvasExtent.width)) {
            continue;
        }
        const float offset = static_cast<size_t>(i) < surface.offsets.size() ? surface.offsets[static_cast<size_t>(i)] : 0.0f;
        const int top = std::clamp(static_cast<int>(std::lround(surface.topLeft.y + offset - worldOrigin_.y)), 0, height);
        if (top >= bottom) {
            continue;
        }
        uint8_t* c = &water_->columns[static_cast<size_t>(x) * 4];
        c[0] = static_cast<uint8_t>(top);
        c[1] = static_cast<uint8_t>(bottom);
        c[2] = style;
        c[3] = 255;
    }
}

void Renderer2D::setGpuEmitters(std::span<const GpuEmitter> emitters) {
    gpuParticles_->setEmitters(emitters);
}

uint32_t Renderer2D::gpuParticleCount() const {
    return gpuParticles_->slotsUsed();
}

void Renderer2D::addFluidSplat(const FluidSplat& splat) {
    ANIMAL_ASSERT(phase_ == Phase::World);
    fluidSplats_.push_back(splat);
}

void Renderer2D::resetFluid() {
    fluidPass_->reset();
    fluidQuietSeconds_ = std::numeric_limits<float>::infinity();
}

// Time for smoke to fade from full density to below 1/512 (invisible in RGBA8 output).
float Renderer2D::fluidFadeSeconds() const {
    const float keep = fluidSettings_.smokeKeepPerSecond;
    return keep > 0.0f && keep < 1.0f ? std::log(1.0f / 512.0f) / std::log(keep)
                                      : std::numeric_limits<float>::infinity();
}

void Renderer2D::setLightOccluders(std::span<const uint8_t> mask) {
    lightingPass_->setOccluders(mask);
}

void Renderer2D::setFluidObstacles(std::span<const uint8_t> mask) {
    fluidPass_->setObstacles(mask);
}

void Renderer2D::endWorld() {
    ANIMAL_ASSERT(phase_ == Phase::World);
    litBatch_.end();
    gpuParticles_->draw(*commands_, worldViewProjection_, GpuEmitter::Style::Lit);
    commands_->endRenderPass();
    commands_->endTimer();

    // World pixels -> grid: uv over the canvas, velocities in cells per second.
    const float dt = fluidSettings_.enabled ? fluidTimeStep_ : 0.0f;
    const float cellsPerPixel = static_cast<float>(kFluidGrid.width) / static_cast<float>(kCanvasExtent.width);
    std::vector<FluidPass::GridSplat> forces;
    std::vector<FluidPass::GridSplat> smoke;
    for (const FluidSplat& s : fluidSplats_) {
        FluidPass::GridSplat g;
        g.uv = {(s.position.x - worldOrigin_.x) / static_cast<float>(kCanvasExtent.width),
                (s.position.y - worldOrigin_.y) / static_cast<float>(kCanvasExtent.height)};
        g.radius = s.radius / static_cast<float>(kCanvasExtent.height);
        if (s.force.x != 0.0f || s.force.y != 0.0f) {
            FluidPass::GridSplat f = g;
            f.value[0] = s.force.x * cellsPerPixel * dt;
            f.value[1] = s.force.y * cellsPerPixel * dt;
            forces.push_back(f);
        }
        if (s.smoke > 0.0f) {
            const float amount = s.smoke * dt;
            g.value[0] = s.color.x * amount;
            g.value[1] = s.color.y * amount;
            g.value[2] = s.color.z * amount;
            g.value[3] = amount;
            smoke.push_back(g);
        }
    }
    commands_->beginTimer("fluid");
    const bool wasSleeping = fluidSleeping();
    fluidQuietSeconds_ = smoke.empty() ? fluidQuietSeconds_ + dt : 0.0f;
    const bool sleeping = fluidSleeping();
    if (sleeping && !wasSleeping) {
        fluidPass_->reset(); // wake up from a clean state
    }
    fluidPass_->simulate(*commands_, sleeping ? 0.0f : dt, forces, smoke, fluidSettings_);
    commands_->endTimer();

    const LightingPass::SmokeLook smokeLook{fluidSettings_.smokeTint,
                                            fluidSettings_.enabled && !sleeping ? fluidSettings_.opacity : 0.0f};
    const bool water = water_->styleCount > 0;
    commands_->beginTimer("lighting");
    lightingPass_->render(*commands_, worldViewProjection_, worldOrigin_, lights_, lightingSettings_, smokeLook,
                          water ? lit_ : canvas_);
    commands_->endTimer();
    if (water) {
        water_->ambient = lightingSettings_.enabled ? lightingSettings_.ambient : Vec3{1.0f, 1.0f, 1.0f};
        water_->bands = lightingSettings_.bands;
        water_->dither = lightingSettings_.dither;
        commands_->beginTimer("water");
        waterPass_->render(*commands_, *water_);
        commands_->endTimer();
    }

    if (!glow_.empty() || gpuParticles_->hasStyle(GpuEmitter::Style::Glow)) {
        commands_->beginTimer("glow");
        commands_->beginRenderPass(rhi::RenderPassDesc::single({canvas_, rhi::LoadOp::Load, rhi::StoreOp::Store, {}}));
        glowBatch_.begin(*commands_, worldViewProjection_);
        for (const GlowQuad& quad : glow_) {
            glowBatch_.draw(font_->texture(), quad.rect, font_->solidUV(), quad.color);
        }
        glowBatch_.end();
        gpuParticles_->draw(*commands_, worldViewProjection_, GpuEmitter::Style::Glow);
        commands_->endRenderPass();
        commands_->endTimer();
    }
    commands_->beginTimer("post");
    postPass_->render(*commands_, postSettings_);
    commands_->endTimer();
    phase_ = Phase::Idle;
}

void Renderer2D::beginWorldOverlay(const Camera2D& camera) {
    ANIMAL_ASSERT(commands_ != nullptr && phase_ == Phase::Idle);
    phase_ = Phase::WorldOverlay;
    commands_->beginTimer("world overlay");
    commands_->beginRenderPass(rhi::RenderPassDesc::single({final_, rhi::LoadOp::Load, rhi::StoreOp::Store, {}}));
    batch_.begin(*commands_, camera.viewProjection(kCanvasExtent));
}

void Renderer2D::endWorldOverlay() {
    ANIMAL_ASSERT(phase_ == Phase::WorldOverlay);
    batch_.end();
    commands_->endRenderPass();
    commands_->endTimer();
    phase_ = Phase::Idle;
}

void Renderer2D::beginScreen(Color letterboxColor) {
    ANIMAL_ASSERT(commands_ != nullptr && phase_ == Phase::Idle);
    phase_ = Phase::Screen;
    const rhi::Extent2D window = device_->swapchainExtent();
    presentLayout_ = computePresentLayout(kCanvasExtent, window);

    commands_->beginTimer("screen");
    commands_->beginRenderPass(rhi::RenderPassDesc::single(
        {device_->swapchainTexture(), rhi::LoadOp::Clear, rhi::StoreOp::Store, toClearColor(letterboxColor)}));
    batch_.begin(*commands_, Mat4::orthographic(0.0f, static_cast<float>(window.width), 0.0f,
                                                static_cast<float>(window.height)));
    batch_.draw(final_,
                {static_cast<float>(presentLayout_.x), static_cast<float>(presentLayout_.y),
                 static_cast<float>(presentLayout_.width), static_cast<float>(presentLayout_.height)},
                {0.0f, 0.0f, 1.0f, 1.0f});
}

uint32_t Renderer2D::overlayScale() const {
    return std::max(1u, presentLayout_.scale / 2);
}

float Renderer2D::textLineHeight() const {
    return static_cast<float>(DebugFont::kLineHeight * overlayScale());
}

float Renderer2D::textWidth(std::string_view text, uint32_t size) const {
    return static_cast<float>(text.size() * DebugFont::kAdvance * overlayScale() * size);
}

void Renderer2D::drawText(Vec2 position, std::string_view text, Color color, uint32_t size) {
    const auto scale = static_cast<float>(overlayScale() * size);
    const Color shadow = Color::rgba(0, 0, 0, color.a);
    const float glyphW = DebugFont::kGlyphWidth * scale;
    const float glyphH = DebugFont::kGlyphHeight * scale;

    for (int layer = 0; layer < 2; ++layer) {
        const Vec2 offset = layer == 0 ? Vec2{scale, scale} : Vec2{};
        const Color c = layer == 0 ? shadow : color;
        float x = position.x;
        for (const char ch : text) {
            if (ch != ' ') {
                batch_.draw(font_->texture(), {x + offset.x, position.y + offset.y, glyphW, glyphH},
                            font_->glyphUV(ch), c);
            }
            x += DebugFont::kAdvance * scale;
        }
    }
}

void Renderer2D::endScreen() {
    ANIMAL_ASSERT(phase_ == Phase::Screen);
    batch_.end();
    commands_->endRenderPass();
    commands_->endTimer();
    phase_ = Phase::Idle;
}

void Renderer2D::fillRect(const RectF& rect, Color color) {
    if (phase_ == Phase::World) {
        litBatch_.draw({font_->texture(), flatNormal_, noEmission_}, rect, font_->solidUV(), color);
    } else {
        batch_.draw(font_->texture(), rect, font_->solidUV(), color);
    }
}

void Renderer2D::strokeRect(const RectF& rect, Color color, float thickness) {
    const float t = thickness;
    fillRect({rect.x, rect.y, rect.width, t}, color);
    fillRect({rect.x, rect.y + rect.height - t, rect.width, t}, color);
    fillRect({rect.x, rect.y + t, t, rect.height - 2.0f * t}, color);
    fillRect({rect.x + rect.width - t, rect.y + t, t, rect.height - 2.0f * t}, color);
}

void Renderer2D::endFrame() {
    ANIMAL_ASSERT(phase_ == Phase::Idle);
    commands_->endTimer(); // "frame"
    batch_.endFrame();
    litBatch_.endFrame();
    glowBatch_.endFrame();
    lightingPass_->endFrame();
    commands_ = nullptr;
}

SpriteBatchStats Renderer2D::stats() const {
    const SpriteBatchStats a = batch_.stats();
    const SpriteBatchStats b = litBatch_.stats();
    const SpriteBatchStats c = glowBatch_.stats();
    return {a.quads + b.quads + c.quads, a.drawCalls + b.drawCalls + c.drawCalls};
}

uint32_t Renderer2D::lightCount() const {
    return lightingPass_->lightCount();
}

} // namespace animal
