#pragma once

#include <animal/rhi/rhi.h>

#include <imgui.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace animal::platform {
class Window;
struct FrameEvents;
} // namespace animal::platform

namespace animal::devtools {

// Dear ImGui on the RHI (ADR-0026): feeds platform events to ImGui and draws its lists in a render
// pass over the swapchain. Works on every backend; textures follow ImGui's texture protocol
// (ImGuiBackendFlags_RendererHasTextures), so the font atlas is rasterized at the display density.
class ImGuiLayer {
public:
    ImGuiLayer() = default;
    ~ImGuiLayer();
    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    // `settingsPath`: where ImGui keeps window positions (empty: not kept).
    bool init(rhi::Device& device, platform::Window& window, const std::string& shaderDirectory,
              const std::string& settingsPath);
    void shutdown();

    // Starts an ImGui frame with this frame's events; build windows between this and render().
    void beginFrame(const platform::FrameEvents& events);
    // Finishes the frame and records a render pass that draws it over the swapchain texture.
    void render(rhi::CommandList& commands);

    bool wantsKeyboard() const;
    bool wantsMouse() const;
    bool frameActive() const { return frameActive_; }

    // For ImGui::Image: an engine texture, sampled with nearest filtering (pixel art).
    ImTextureID textureId(rhi::TextureHandle texture) const;
    // Call before destroying a texture that was shown with textureId().
    void forgetTexture(rhi::TextureHandle texture);

private:
    enum class SamplerChoice : uint8_t {
        Default, // linear for ImGui's own textures (fonts), nearest for engine textures
        Nearest,
        Linear,
    };

    void updateTexture(ImTextureData& texture);
    void destroyTexture(ImTextureData& texture);
    rhi::BindGroupHandle groupFor(ImTextureID id, SamplerChoice choice);
    void forgetGroups(ImTextureID id);
    void ensureCapacity(rhi::BufferHandle& buffer, uint64_t& capacity, uint64_t size, rhi::BufferUsage usage,
                        const char* name);

    rhi::Device* device_ = nullptr;
    platform::Window* window_ = nullptr;
    std::string settingsPath_;
    rhi::ShaderHandle vertexShader_;
    rhi::ShaderHandle fragmentShader_;
    rhi::PipelineHandle pipeline_;
    rhi::BufferHandle uniforms_;
    rhi::BindGroupHandle viewGroup_;
    rhi::SamplerHandle linearSampler_;
    rhi::SamplerHandle nearestSampler_;
    rhi::BufferHandle vertices_;
    rhi::BufferHandle indices_;
    uint64_t vertexCapacity_ = 0;
    uint64_t indexCapacity_ = 0;
    std::unordered_map<ImTextureID, rhi::BindGroupHandle> linearGroups_;
    std::unordered_map<ImTextureID, rhi::BindGroupHandle> nearestGroups_;
    std::unordered_set<ImTextureID> ownTextures_; // created for ImGui (font atlas): linear sampling
    std::vector<uint8_t> vertexData_;
    std::vector<uint8_t> indexData_;
    uint64_t lastTime_ = 0;
    bool frameActive_ = false;
    bool initialized_ = false;
};

} // namespace animal::devtools
