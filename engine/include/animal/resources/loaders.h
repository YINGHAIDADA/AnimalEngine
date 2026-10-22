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

// Loads a PNG as a premultiplied-alpha RGBA8 texture. Logs and returns nullopt on failure.
std::optional<LoadedTexture> loadTexture(rhi::Device& device, const std::string& path);

// Loads a compiled .ashd shader package. Logs and returns an invalid handle on failure.
rhi::ShaderHandle loadShader(rhi::Device& device, const std::string& path);

} // namespace animal
