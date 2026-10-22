#include <animal/core/file.h>
#include <animal/core/log.h>
#include <animal/resources/loaders.h>

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wshadow"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <SDL3/SDL_iostream.h>

#include <cstring>

namespace animal {

std::optional<Image> loadImage(const std::string& path, std::string* error) {
    std::string readError;
    const auto bytes = readFile(path, &readError);
    if (!bytes) {
        if (error) {
            *error = readError;
        }
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes->data()),
                                             static_cast<int>(bytes->size()), &width, &height, &channels, 4);
    if (!decoded) {
        if (error) {
            *error = stbi_failure_reason();
        }
        return std::nullopt;
    }

    Image image;
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.pixels.assign(decoded, decoded + size_t{image.width} * image.height * 4);
    stbi_image_free(decoded);
    return image;
}

void premultiplyAlpha(Image& image) {
    for (size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
        const uint32_t a = image.pixels[i + 3];
        for (size_t c = 0; c < 3; ++c) {
            image.pixels[i + c] = static_cast<uint8_t>((image.pixels[i + c] * a + 127) / 255);
        }
    }
}

bool writePng(const std::string& path, uint32_t width, uint32_t height, const uint8_t* rgba) {
    int length = 0;
    unsigned char* png = stbi_write_png_to_mem(rgba, static_cast<int>(width * 4), static_cast<int>(width),
                                               static_cast<int>(height), 4, &length);
    if (!png) {
        return false;
    }
    const bool ok = SDL_SaveFile(path.c_str(), png, static_cast<size_t>(length));
    STBIW_FREE(png);
    return ok;
}

std::optional<LoadedTexture> loadTexture(rhi::Device& device, const std::string& path) {
    std::string error;
    std::optional<Image> image = loadImage(path, &error);
    if (!image) {
        log::error("Failed to load texture '{}': {}", path, error);
        return std::nullopt;
    }
    premultiplyAlpha(*image);

    rhi::TextureDesc desc;
    desc.width = image->width;
    desc.height = image->height;
    desc.format = rhi::Format::RGBA8_UNorm;
    desc.usage = rhi::TextureUsage::Sampled;
    desc.debugName = path.c_str();
    const rhi::TextureHandle handle = device.createTexture(desc);
    if (!handle) {
        return std::nullopt;
    }
    device.writeTexture(handle, {0, 0, image->width, image->height}, image->pixels.data(), image->width * 4);
    return LoadedTexture{handle, image->width, image->height};
}

rhi::ShaderHandle loadShader(rhi::Device& device, const std::string& path) {
    std::string error;
    const auto bytes = readFile(path, &error);
    if (!bytes) {
        log::error("Failed to read shader '{}': {}", path, error);
        return {};
    }
    const std::optional<rhi::ShaderPackage> package = rhi::parseShaderPackage(*bytes, &error);
    if (!package) {
        log::error("Invalid shader package '{}': {}", path, error);
        return {};
    }
    rhi::ShaderDesc desc;
    desc.package = &*package;
    desc.debugName = path.c_str();
    return device.createShader(desc);
}

} // namespace animal
