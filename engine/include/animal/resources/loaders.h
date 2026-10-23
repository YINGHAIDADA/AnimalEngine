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

std::optional<Image> loadImage(const std::string& path, std::string* error = nullptr);
void premultiplyAlpha(Image& image);
bool writePng(const std::string& path, uint32_t width, uint32_t height, const uint8_t* rgba);

struct LoadedTexture {
    rhi::TextureHandle handle;
    uint32_t width = 0;
    uint32_t height = 0;
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
    rhi::TextureHandle normal;   // invalid: flat
    rhi::TextureHandle emissive; // invalid: none
    float rim = 0.0f;            // 0..1: silhouette catches rim light (LightingSettings::rim*)
};

// `basePath` excludes the extension. Missing companions are allowed; a missing albedo fails.
std::optional<SpriteMaterial> loadMaterial(rhi::Device& device, const std::string& basePath);
void destroyMaterial(rhi::Device& device, SpriteMaterial& material);

// Loads a compiled .ashd shader package. Logs and returns an invalid handle on failure.
rhi::ShaderHandle loadShader(rhi::Device& device, const std::string& path);

// Hot reload (ADR-0028). Shaders and textures loaded above are remembered by path, so a changed file
// can be reloaded into the live handles (nothing else needs to know). A file that fails to load or
// would change a handle's shape (shader bindings, texture size) leaves the old contents in place.
enum class ReloadResult {
    NotLoaded, // nothing was loaded from this path
    Reloaded,
    Failed,    // logged; the previous contents stay
};
ReloadResult reloadShaderFile(rhi::Device& device, const std::string& path);
ReloadResult reloadTextureFile(rhi::Device& device, const std::string& path);
// Stop tracking a handle (destroyMaterial does this for its textures).
void forgetLoaded(rhi::TextureHandle texture);
void forgetLoaded(rhi::ShaderHandle shader);

} // namespace animal
