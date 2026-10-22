#include "debug_font.h"

#include <animal/core/assert.h>
#include <animal/core/log.h>
#include <animal/renderer/renderer2d.h>
#include <animal/rhi/command_list.h>

#include <algorithm>

namespace animal {

namespace {

rhi::ClearColor toClearColor(Color c) {
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
}

} // namespace

Renderer2D::Renderer2D() : font_(std::make_unique<DebugFont>()) {}

Renderer2D::~Renderer2D() = default;

bool Renderer2D::init(rhi::Device& device, const std::string& dataDirectory) {
    device_ = &device;
    if (!batch_.init(device, dataDirectory + "shaders/", device.swapchainFormat())) {
        log::error("Renderer2D: sprite batch initialization failed (is '{}' deployed?)", dataDirectory);
        return false;
    }
    if (!font_->init(device)) {
        return false;
    }

    rhi::TextureDesc canvasDesc;
    canvasDesc.width = kCanvasExtent.width;
    canvasDesc.height = kCanvasExtent.height;
    canvasDesc.format = rhi::Format::RGBA8_UNorm;
    canvasDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderTarget;
    canvasDesc.debugName = "pixel canvas";
    canvas_ = device.createTexture(canvasDesc);
    return canvas_.valid();
}

void Renderer2D::shutdown() {
    if (!device_) {
        return;
    }
    batch_.shutdown();
    font_->shutdown(*device_);
    device_->destroy(canvas_);
    device_ = nullptr;
}

void Renderer2D::beginFrame(rhi::CommandList& commands) {
    commands_ = &commands;
    batch_.beginFrame();
}

void Renderer2D::beginWorld(const Camera2D& camera, Color clearColor) {
    ANIMAL_ASSERT(commands_ != nullptr);
    commands_->beginRenderPass(rhi::RenderPassDesc::single(
        {canvas_, rhi::LoadOp::Clear, rhi::StoreOp::Store, toClearColor(clearColor)}));
    batch_.begin(*commands_, camera.viewProjection(kCanvasExtent));
}

void Renderer2D::drawSprite(const LoadedTexture& texture, Vec2 position, const RectF& source, Color tint,
                            bool flipX) {
    if (texture.width == 0 || texture.height == 0) {
        return;
    }
    const Vec2 p = roundToPixel(position);
    const auto tw = static_cast<float>(texture.width);
    const auto th = static_cast<float>(texture.height);
    RectF uv{source.x / tw, source.y / th, source.width / tw, source.height / th};
    if (flipX) {
        uv.x += uv.width;
        uv.width = -uv.width;
    }
    batch_.draw(texture.handle, {p.x, p.y, source.width, source.height}, uv, tint);
}

void Renderer2D::endWorld() {
    batch_.end();
    commands_->endRenderPass();
}

void Renderer2D::beginScreen(Color letterboxColor) {
    ANIMAL_ASSERT(commands_ != nullptr);
    const rhi::Extent2D window = device_->swapchainExtent();
    presentLayout_ = computePresentLayout(kCanvasExtent, window);

    commands_->beginRenderPass(rhi::RenderPassDesc::single(
        {device_->swapchainTexture(), rhi::LoadOp::Clear, rhi::StoreOp::Store, toClearColor(letterboxColor)}));
    batch_.begin(*commands_, Mat4::orthographic(0.0f, static_cast<float>(window.width), 0.0f,
                                                static_cast<float>(window.height)));
    batch_.draw(canvas_,
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

void Renderer2D::drawText(Vec2 position, std::string_view text, Color color) {
    const auto scale = static_cast<float>(overlayScale());
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

void Renderer2D::fillRect(const RectF& rect, Color color) {
    batch_.draw(font_->texture(), rect, font_->solidUV(), color);
}

void Renderer2D::strokeRect(const RectF& rect, Color color, float thickness) {
    const float t = thickness;
    fillRect({rect.x, rect.y, rect.width, t}, color);
    fillRect({rect.x, rect.y + rect.height - t, rect.width, t}, color);
    fillRect({rect.x, rect.y + t, t, rect.height - 2.0f * t}, color);
    fillRect({rect.x + rect.width - t, rect.y + t, t, rect.height - 2.0f * t}, color);
}

void Renderer2D::endScreen() {
    batch_.end();
    commands_->endRenderPass();
}

void Renderer2D::endFrame() {
    batch_.endFrame();
    commands_ = nullptr;
}

} // namespace animal
