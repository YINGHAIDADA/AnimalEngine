#include <animal/devtools/imgui_layer.h>

#include <animal/core/log.h>
#include <animal/core/math.h>
#include <animal/platform/input.h>
#include <animal/platform/platform.h>
#include <animal/platform/window.h>
#include <animal/resources/loaders.h>
#include <animal/rhi/command_list.h>

#include <algorithm>
#include <cfloat>
#include <cstring>

namespace animal::devtools {

namespace {

using platform::InputEvent;
using platform::Key;

ImGuiKey imguiKey(Key key) {
    switch (key) {
    case Key::Left: return ImGuiKey_LeftArrow;
    case Key::Right: return ImGuiKey_RightArrow;
    case Key::Up: return ImGuiKey_UpArrow;
    case Key::Down: return ImGuiKey_DownArrow;
    case Key::Space: return ImGuiKey_Space;
    case Key::Enter: return ImGuiKey_Enter;
    case Key::Escape: return ImGuiKey_Escape;
    case Key::Tab: return ImGuiKey_Tab;
    case Key::Backspace: return ImGuiKey_Backspace;
    case Key::Delete: return ImGuiKey_Delete;
    case Key::Insert: return ImGuiKey_Insert;
    case Key::Home: return ImGuiKey_Home;
    case Key::End: return ImGuiKey_End;
    case Key::PageUp: return ImGuiKey_PageUp;
    case Key::PageDown: return ImGuiKey_PageDown;
    case Key::LeftShift: return ImGuiKey_LeftShift;
    case Key::RightShift: return ImGuiKey_RightShift;
    case Key::LeftCtrl: return ImGuiKey_LeftCtrl;
    case Key::RightCtrl: return ImGuiKey_RightCtrl;
    case Key::LeftAlt: return ImGuiKey_LeftAlt;
    case Key::RightAlt: return ImGuiKey_RightAlt;
    case Key::LeftSuper: return ImGuiKey_LeftSuper;
    case Key::RightSuper: return ImGuiKey_RightSuper;
    case Key::Grave: return ImGuiKey_GraveAccent;
    case Key::Minus: return ImGuiKey_Minus;
    case Key::Equals: return ImGuiKey_Equal;
    case Key::LeftBracket: return ImGuiKey_LeftBracket;
    case Key::RightBracket: return ImGuiKey_RightBracket;
    case Key::Backslash: return ImGuiKey_Backslash;
    case Key::Semicolon: return ImGuiKey_Semicolon;
    case Key::Apostrophe: return ImGuiKey_Apostrophe;
    case Key::Comma: return ImGuiKey_Comma;
    case Key::Period: return ImGuiKey_Period;
    case Key::Slash: return ImGuiKey_Slash;
    case Key::KeypadEnter: return ImGuiKey_KeypadEnter;
    default: break;
    }
    if (key >= Key::A && key <= Key::Z) {
        return static_cast<ImGuiKey>(ImGuiKey_A + (static_cast<int>(key) - static_cast<int>(Key::A)));
    }
    if (key >= Key::Num0 && key <= Key::Num9) {
        return static_cast<ImGuiKey>(ImGuiKey_0 + (static_cast<int>(key) - static_cast<int>(Key::Num0)));
    }
    if (key >= Key::F1 && key <= Key::F12) {
        return static_cast<ImGuiKey>(ImGuiKey_F1 + (static_cast<int>(key) - static_cast<int>(Key::F1)));
    }
    return ImGuiKey_None;
}

platform::Cursor platformCursor(ImGuiMouseCursor cursor) {
    switch (cursor) {
    case ImGuiMouseCursor_None: return platform::Cursor::Hidden;
    case ImGuiMouseCursor_TextInput: return platform::Cursor::TextInput;
    case ImGuiMouseCursor_ResizeAll: return platform::Cursor::Move;
    case ImGuiMouseCursor_ResizeNS: return platform::Cursor::ResizeNS;
    case ImGuiMouseCursor_ResizeEW: return platform::Cursor::ResizeEW;
    case ImGuiMouseCursor_ResizeNESW: return platform::Cursor::ResizeNESW;
    case ImGuiMouseCursor_ResizeNWSE: return platform::Cursor::ResizeNWSE;
    case ImGuiMouseCursor_Hand: return platform::Cursor::Hand;
    case ImGuiMouseCursor_NotAllowed: return platform::Cursor::NotAllowed;
    default: return platform::Cursor::Arrow;
    }
}

rhi::TextureHandle handleOf(ImTextureID id) {
    return {static_cast<uint32_t>(id & 0xFFFFFFFFu), static_cast<uint32_t>(id >> 32)};
}

// Standard draw callbacks; only their addresses matter.
void resetRenderState(const ImDrawList*, const ImDrawCmd*) {}
void samplerNearest(const ImDrawList*, const ImDrawCmd*) {}
void samplerLinear(const ImDrawList*, const ImDrawCmd*) {}

std::string gClipboard; // keeps the text returned to ImGui alive until the next request

const char* getClipboard(ImGuiContext*) {
    gClipboard = platform::clipboardText();
    return gClipboard.empty() ? nullptr : gClipboard.c_str();
}

void setClipboard(ImGuiContext*, const char* text) {
    platform::setClipboardText(text ? text : "");
}

void applyStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 3.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.WindowBorderSize = 1.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.92f;
    // Cave palette accents (the HUD's teal).
    const ImVec4 accent{0.35f, 0.75f, 0.68f, 1.0f};
    style.Colors[ImGuiCol_CheckMark] = accent;
    style.Colors[ImGuiCol_SliderGrab] = accent;
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4{0.5f, 0.9f, 0.82f, 1.0f};
    style.Colors[ImGuiCol_Header] = ImVec4{0.2f, 0.42f, 0.4f, 0.6f};
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4{0.26f, 0.55f, 0.52f, 0.8f};
    style.Colors[ImGuiCol_HeaderActive] = ImVec4{0.3f, 0.62f, 0.58f, 1.0f};
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4{0.12f, 0.28f, 0.27f, 1.0f};
}

} // namespace

ImGuiLayer::~ImGuiLayer() {
    shutdown();
}

bool ImGuiLayer::init(rhi::Device& device, platform::Window& window, const std::string& shaderDirectory,
                      const std::string& settingsPath) {
    device_ = &device;
    window_ = &window;
    settingsPath_ = settingsPath;

    vertexShader_ = loadShader(device, shaderDirectory + "imgui.vert.ashd");
    fragmentShader_ = loadShader(device, shaderDirectory + "imgui.frag.ashd");
    if (!vertexShader_ || !fragmentShader_) {
        return false;
    }
    rhi::GraphicsPipelineDesc pipeline;
    pipeline.vertexShader = vertexShader_;
    pipeline.fragmentShader = fragmentShader_;
    pipeline.vertexBuffers = {{sizeof(ImDrawVert), rhi::VertexStepMode::Vertex}};
    pipeline.vertexAttributes = {
        {0, 0, rhi::VertexFormat::Float2, offsetof(ImDrawVert, pos)},
        {1, 0, rhi::VertexFormat::Float2, offsetof(ImDrawVert, uv)},
        {2, 0, rhi::VertexFormat::UNorm8x4, offsetof(ImDrawVert, col)},
    };
    const rhi::BlendState straightAlpha{true,
                                        rhi::BlendFactor::SrcAlpha,
                                        rhi::BlendFactor::OneMinusSrcAlpha,
                                        rhi::BlendOp::Add,
                                        rhi::BlendFactor::One,
                                        rhi::BlendFactor::OneMinusSrcAlpha,
                                        rhi::BlendOp::Add};
    pipeline.colorTargets = {{device.swapchainFormat(), straightAlpha}};
    pipeline.debugName = "imgui";
    pipeline_ = device.createGraphicsPipeline(pipeline);

    rhi::BufferDesc uniformDesc;
    uniformDesc.size = sizeof(Mat4);
    uniformDesc.usage = rhi::BufferUsage::Uniform;
    uniformDesc.debugName = "imgui view";
    uniforms_ = device.createBuffer(uniformDesc);

    rhi::SamplerDesc linear;
    linear.minFilter = rhi::Filter::Linear;
    linear.magFilter = rhi::Filter::Linear;
    linearSampler_ = device.createSampler(linear);
    nearestSampler_ = device.createSampler({});
    if (!pipeline_ || !uniforms_ || !linearSampler_ || !nearestSampler_) {
        return false;
    }
    viewGroup_ = device.createBindGroup({pipeline_, 0, {rhi::BindGroupEntry::uniformBuffer(0, uniforms_)}, "imgui view"});
    if (!viewGroup_) {
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = settingsPath_.empty() ? nullptr : settingsPath_.c_str();
    io.BackendPlatformName = "animal_platform";
    io.BackendRendererName = "animal_rhi";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_RendererHasVtxOffset |
                       ImGuiBackendFlags_RendererHasTextures;
    // No keyboard navigation: the keyboard stays with the game unless a text field is being edited.
    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    platformIo.Platform_GetClipboardTextFn = getClipboard;
    platformIo.Platform_SetClipboardTextFn = setClipboard;
    platformIo.DrawCallback_ResetRenderState = resetRenderState;
    platformIo.DrawCallback_SetSamplerNearest = samplerNearest;
    platformIo.DrawCallback_SetSamplerLinear = samplerLinear;
    platformIo.Renderer_TextureMaxWidth = platformIo.Renderer_TextureMaxHeight =
        static_cast<int>(device.caps().maxTextureSize);
    applyStyle();

    lastTime_ = platform::nowNanoseconds();
    initialized_ = true;
    return true;
}

void ImGuiLayer::shutdown() {
    if (!initialized_) {
        return;
    }
    if (frameActive_) {
        ImGui::EndFrame();
        frameActive_ = false;
    }
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures) {
        if (texture->RefCount == 1) {
            destroyTexture(*texture);
        }
    }
    ImGui::DestroyContext();
    for (auto* groups : {&linearGroups_, &nearestGroups_}) {
        for (const auto& [id, group] : *groups) {
            device_->destroy(group);
        }
        groups->clear();
    }
    device_->destroy(viewGroup_);
    device_->destroy(vertices_);
    device_->destroy(indices_);
    device_->destroy(uniforms_);
    device_->destroy(linearSampler_);
    device_->destroy(nearestSampler_);
    device_->destroy(pipeline_);
    destroyShader(*device_, vertexShader_);
    destroyShader(*device_, fragmentShader_);
    vertexCapacity_ = indexCapacity_ = 0;
    initialized_ = false;
}

void ImGuiLayer::beginFrame(const platform::FrameEvents& events) {
    ImGuiIO& io = ImGui::GetIO();
    for (const InputEvent& e : events.input) {
        switch (e.type) {
        case InputEvent::Type::Key: {
            io.AddKeyEvent(ImGuiMod_Ctrl, e.modifiers.ctrl);
            io.AddKeyEvent(ImGuiMod_Shift, e.modifiers.shift);
            io.AddKeyEvent(ImGuiMod_Alt, e.modifiers.alt);
            io.AddKeyEvent(ImGuiMod_Super, e.modifiers.super);
            if (const ImGuiKey key = imguiKey(e.key); key != ImGuiKey_None) {
                io.AddKeyEvent(key, e.down);
            }
            break;
        }
        case InputEvent::Type::Text:
            io.AddInputCharactersUTF8(e.text.c_str());
            break;
        case InputEvent::Type::MouseMove:
            if (e.x < -1.0e29f) {
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            } else {
                io.AddMousePosEvent(e.x, e.y);
            }
            break;
        case InputEvent::Type::MouseButton:
            io.AddMouseButtonEvent(static_cast<int>(e.button), e.down);
            break;
        case InputEvent::Type::MouseWheel:
            io.AddMouseWheelEvent(-e.x, e.y);
            break;
        case InputEvent::Type::Focus:
            io.AddFocusEvent(e.down);
            break;
        }
    }

    const platform::PixelSize points = window_->sizeInPoints();
    const rhi::Extent2D pixels = device_->swapchainExtent();
    io.DisplaySize = ImVec2(static_cast<float>(points.width), static_cast<float>(points.height));
    if (points.width > 0 && points.height > 0) {
        io.DisplayFramebufferScale = ImVec2(static_cast<float>(pixels.width) / static_cast<float>(points.width),
                                            static_cast<float>(pixels.height) / static_cast<float>(points.height));
    }
    const uint64_t now = platform::nowNanoseconds();
    io.DeltaTime = std::max(1.0e-4f, static_cast<float>(now - lastTime_) * 1.0e-9f);
    lastTime_ = now;

    ImGui::NewFrame();
    frameActive_ = true;
    window_->setTextInput(io.WantTextInput);
}

void ImGuiLayer::render(rhi::CommandList& commands) {
    if (!frameActive_) {
        return;
    }
    frameActive_ = false;
    if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)) {
        platform::setCursor(platformCursor(ImGui::GetMouseCursor()));
    }
    ImGui::Render();
    ImDrawData* data = ImGui::GetDrawData();
    if (data->Textures) {
        for (ImTextureData* texture : *data->Textures) {
            if (texture->Status != ImTextureStatus_OK) {
                updateTexture(*texture);
            }
        }
    }
    const rhi::Extent2D framebuffer = device_->swapchainExtent();
    if (data->TotalVtxCount == 0 || framebuffer.width == 0 || framebuffer.height == 0) {
        return;
    }

    vertexData_.resize(static_cast<size_t>(data->TotalVtxCount) * sizeof(ImDrawVert));
    indexData_.resize((static_cast<size_t>(data->TotalIdxCount) * sizeof(ImDrawIdx) + 3) & ~size_t{3});
    size_t vertexOffset = 0;
    size_t indexOffset = 0;
    for (const ImDrawList* list : data->CmdLists) {
        const size_t vbytes = static_cast<size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert);
        const size_t ibytes = static_cast<size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
        std::memcpy(vertexData_.data() + vertexOffset, list->VtxBuffer.Data, vbytes);
        std::memcpy(indexData_.data() + indexOffset, list->IdxBuffer.Data, ibytes);
        vertexOffset += vbytes;
        indexOffset += ibytes;
    }
    ensureCapacity(vertices_, vertexCapacity_, vertexData_.size(), rhi::BufferUsage::Vertex, "imgui vertices");
    ensureCapacity(indices_, indexCapacity_, indexData_.size(), rhi::BufferUsage::Index, "imgui indices");
    if (!vertices_ || !indices_) {
        return;
    }
    device_->writeBuffer(vertices_, 0, vertexData_.data(), vertexData_.size());
    device_->writeBuffer(indices_, 0, indexData_.data(), indexData_.size());
    const Mat4 projection = Mat4::orthographic(data->DisplayPos.x, data->DisplayPos.x + data->DisplaySize.x,
                                               data->DisplayPos.y, data->DisplayPos.y + data->DisplaySize.y);
    device_->writeBuffer(uniforms_, 0, &projection, sizeof(projection));

    commands.beginTimer("tools");
    commands.beginRenderPass(
        rhi::RenderPassDesc::single({device_->swapchainTexture(), rhi::LoadOp::Load, rhi::StoreOp::Store, {}}));
    const auto bindState = [&] {
        commands.setPipeline(pipeline_);
        commands.setVertexBuffer(0, vertices_);
        commands.setIndexBuffer(indices_, sizeof(ImDrawIdx) == 2 ? rhi::IndexType::UInt16 : rhi::IndexType::UInt32);
        commands.setBindGroup(0, viewGroup_);
    };
    bindState();

    const ImVec2 origin = data->DisplayPos;
    const ImVec2 scale = data->FramebufferScale;
    uint32_t globalVertex = 0;
    uint32_t globalIndex = 0;
    ImTextureID boundTexture = ImTextureID_Invalid;
    SamplerChoice sampler = SamplerChoice::Default;
    for (const ImDrawList* list : data->CmdLists) {
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) {
                if (cmd.UserCallback == resetRenderState) {
                    bindState();
                    sampler = SamplerChoice::Default;
                } else if (cmd.UserCallback == samplerNearest) {
                    sampler = SamplerChoice::Nearest;
                } else if (cmd.UserCallback == samplerLinear) {
                    sampler = SamplerChoice::Linear;
                } else {
                    cmd.UserCallback(list, &cmd);
                }
                boundTexture = ImTextureID_Invalid;
                continue;
            }
            const float x0 = std::max(0.0f, (cmd.ClipRect.x - origin.x) * scale.x);
            const float y0 = std::max(0.0f, (cmd.ClipRect.y - origin.y) * scale.y);
            const float x1 = std::min(static_cast<float>(framebuffer.width), (cmd.ClipRect.z - origin.x) * scale.x);
            const float y1 = std::min(static_cast<float>(framebuffer.height), (cmd.ClipRect.w - origin.y) * scale.y);
            if (x1 <= x0 || y1 <= y0) {
                continue;
            }
            const ImTextureID texture = cmd.GetTexID();
            if (texture != boundTexture) {
                const rhi::BindGroupHandle group = groupFor(texture, sampler);
                if (!group) {
                    continue;
                }
                commands.setBindGroup(1, group);
                boundTexture = texture;
            }
            commands.setScissor({static_cast<int32_t>(x0), static_cast<int32_t>(y0), static_cast<uint32_t>(x1 - x0),
                                 static_cast<uint32_t>(y1 - y0)});
            commands.drawIndexed(cmd.ElemCount, 1, globalIndex + cmd.IdxOffset,
                                 static_cast<int32_t>(globalVertex + cmd.VtxOffset));
        }
        globalVertex += static_cast<uint32_t>(list->VtxBuffer.Size);
        globalIndex += static_cast<uint32_t>(list->IdxBuffer.Size);
    }
    commands.endRenderPass();
    commands.endTimer();
}

bool ImGuiLayer::wantsKeyboard() const {
    return initialized_ && ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiLayer::wantsMouse() const {
    return initialized_ && ImGui::GetIO().WantCaptureMouse;
}

ImTextureID ImGuiLayer::textureId(rhi::TextureHandle texture) const {
    return texture.key();
}

void ImGuiLayer::forgetTexture(rhi::TextureHandle texture) {
    forgetGroups(texture.key());
}

void ImGuiLayer::updateTexture(ImTextureData& texture) {
    if (texture.Status == ImTextureStatus_WantCreate) {
        if (texture.Format != ImTextureFormat_RGBA32) {
            log::error("ImGuiLayer: only RGBA32 textures are supported");
            return;
        }
        rhi::TextureDesc desc;
        desc.width = static_cast<uint32_t>(texture.Width);
        desc.height = static_cast<uint32_t>(texture.Height);
        desc.debugName = "imgui texture";
        const rhi::TextureHandle handle = device_->createTexture(desc);
        if (!handle) {
            return; // ImGui asks again next frame
        }
        device_->writeTexture(handle, {0, 0, desc.width, desc.height}, texture.GetPixels(),
                              static_cast<uint32_t>(texture.GetPitch()));
        texture.SetTexID(handle.key());
        ownTextures_.insert(handle.key());
        texture.SetStatus(ImTextureStatus_OK);
    } else if (texture.Status == ImTextureStatus_WantUpdates) {
        const rhi::TextureHandle handle = handleOf(texture.TexID);
        for (const ImTextureRect& r : texture.Updates) {
            device_->writeTexture(handle, {r.x, r.y, r.w, r.h}, texture.GetPixelsAt(r.x, r.y),
                                  static_cast<uint32_t>(texture.GetPitch()));
        }
        texture.SetStatus(ImTextureStatus_OK);
    } else if (texture.Status == ImTextureStatus_WantDestroy) {
        destroyTexture(texture);
    }
}

void ImGuiLayer::destroyTexture(ImTextureData& texture) {
    if (texture.TexID != ImTextureID_Invalid) {
        forgetGroups(texture.TexID);
        ownTextures_.erase(texture.TexID);
        device_->destroy(handleOf(texture.TexID));
    }
    texture.SetTexID(ImTextureID_Invalid);
    texture.SetStatus(ImTextureStatus_Destroyed);
}

rhi::BindGroupHandle ImGuiLayer::groupFor(ImTextureID id, SamplerChoice choice) {
    const bool linear = choice == SamplerChoice::Default ? ownTextures_.contains(id) : choice == SamplerChoice::Linear;
    auto& groups = linear ? linearGroups_ : nearestGroups_;
    if (const auto it = groups.find(id); it != groups.end()) {
        return it->second;
    }
    const rhi::BindGroupHandle group = device_->createBindGroup(
        {pipeline_,
         1,
         {rhi::BindGroupEntry::textureSampler(0, handleOf(id), linear ? linearSampler_ : nearestSampler_)},
         "imgui texture"});
    if (group) {
        groups.emplace(id, group);
    }
    return group;
}

void ImGuiLayer::forgetGroups(ImTextureID id) {
    for (auto* groups : {&linearGroups_, &nearestGroups_}) {
        if (const auto it = groups->find(id); it != groups->end()) {
            device_->destroy(it->second);
            groups->erase(it);
        }
    }
}

void ImGuiLayer::ensureCapacity(rhi::BufferHandle& buffer, uint64_t& capacity, uint64_t size, rhi::BufferUsage usage,
                                const char* name) {
    if (buffer && capacity >= size) {
        return;
    }
    device_->destroy(buffer);
    capacity = std::max<uint64_t>(size + size / 2, 64 * 1024);
    rhi::BufferDesc desc;
    desc.size = capacity;
    desc.usage = usage;
    desc.debugName = name;
    buffer = device_->createBuffer(desc);
    if (!buffer) {
        capacity = 0;
    }
}

} // namespace animal::devtools
