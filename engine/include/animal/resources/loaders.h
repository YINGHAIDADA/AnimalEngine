#pragma once

#include <animal/rhi/rhi.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace animal {

// 8-bit RGBA, top row first.
struct Image {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};

// PNG, or a cooked image (ADR-0031: "ATEX", version, width, height, raw straight-alpha RGBA8).
std::optional<Image> loadImage(const std::string& path, std::string* error = nullptr);
std::vector<std::byte> encodeCookedImage(const Image& image);
void premultiplyAlpha(Image& image);
bool writePng(const std::string& path, uint32_t width, uint32_t height, const uint8_t* rgba);

struct LoadedTexture {
    rhi::TextureHandle handle;
    uint32_t width = 0; // the image (its region of an atlas, or the size at load time)
    uint32_t height = 0;
    uint32_t x = 0; // where the image starts in the texture (atlas regions)
    uint32_t y = 0;
    bool atlasRegion = false; // part of a shared atlas texture (Device::textureExtent is the atlas)
};

enum class AlphaMode {
    Premultiply, // color textures
    Keep,        // data textures: normal maps, emissive maps
};

// Loads a PNG as an RGBA8 texture. Logs and returns nullopt on failure.
std::optional<LoadedTexture> loadTexture(rhi::Device& device, const std::string& path,
                                         AlphaMode alpha = AlphaMode::Premultiply);

// Sprite sheet plus optional companions with the same layout (docs/ART_PIPELINE.md):
// <base>.png albedo, <base>_n.png normal map, <base>_e.png emissive map.
struct SpriteMaterial {
    LoadedTexture albedo;
    rhi::TextureHandle normal;   // invalid: flat (loadMaterial always provides one)
    rhi::TextureHandle emissive; // invalid: none (loadMaterial always provides one)
    float rim = 0.0f;            // 0..1: silhouette catches rim light (LightingSettings::rim*)
};

// `basePath` excludes the extension. Missing companions are allowed (a 1×1 neutral texture stands in,
// so a companion added later hot-reloads into the material); a missing albedo fails. When the folder
// holds a cooked atlas (atlas.json, ADR-0031) that lists the material, it is a region of the shared
// atlas textures instead.
std::optional<SpriteMaterial> loadMaterial(rhi::Device& device, const std::string& basePath);
void destroyMaterial(rhi::Device& device, SpriteMaterial& material);

// Loads a compiled .ashd shader package. Logs and returns an invalid handle on failure.
rhi::ShaderHandle loadShader(rhi::Device& device, const std::string& path);

// Hot reload (ADR-0028). Shaders and textures loaded above are remembered by path, so a changed file
// can be reloaded into the live handles (nothing else needs to know); a texture of another size is
// recreated under the same handle (LoadedTexture::width / height keep the size at load time: ask
// Device::textureExtent for the current one). A file that fails to load or would change a shader's
// bindings leaves the old contents in place.
enum class ReloadResult {
    NotLoaded,    // nothing was loaded from this path
    Reloaded,
    Failed,       // logged; the previous contents stay
    Incompatible, // a shader whose stage or resource bindings changed: whoever created pipelines
                  // with it must rebuild them (e.g. Renderer2D::rebuild); nothing was changed
};
ReloadResult reloadShaderFile(rhi::Device& device, const std::string& path);
ReloadResult reloadTextureFile(rhi::Device& device, const std::string& path);
// Stop tracking a handle (destroyMaterial does this for its textures).
void forgetLoaded(rhi::TextureHandle texture);
void forgetLoaded(rhi::ShaderHandle shader);
// Destroys a shader from loadShader and stops tracking it.
void destroyShader(rhi::Device& device, rhi::ShaderHandle shader);

} // namespace animal
