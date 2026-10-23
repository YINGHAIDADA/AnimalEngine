#include <animal/core/file.h>
#include <animal/core/math.h>
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

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace animal {

namespace {

struct TrackedShader {
    std::string path;
    rhi::Device* device;
    rhi::ShaderHandle handle;
    rhi::ShaderStage stage;
    std::vector<rhi::ShaderBinding> bindings;
};

struct TrackedTexture {
    std::string path;
    rhi::Device* device;
    rhi::TextureHandle handle;
    AlphaMode alpha;
    uint32_t width;
    uint32_t height;
};

std::vector<TrackedShader> gShaders;
std::vector<TrackedTexture> gTextures;

std::string normalized(const std::string& path) {
    const std::filesystem::path p(std::u8string(path.begin(), path.end()));
    const std::u8string s = p.lexically_normal().generic_u8string();
    return {s.begin(), s.end()};
}

std::optional<rhi::ShaderPackage> readShaderPackage(const std::string& path) {
    std::string error;
    const auto bytes = readFile(path, &error);
    if (!bytes) {
        log::error("Failed to read shader '{}': {}", path, error);
        return std::nullopt;
    }
    std::optional<rhi::ShaderPackage> package = rhi::parseShaderPackage(*bytes, &error);
    if (!package) {
        log::error("Invalid shader package '{}': {}", path, error);
    }
    return package;
}

} // namespace

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

std::optional<LoadedTexture> loadTexture(rhi::Device& device, const std::string& path, AlphaMode alpha) {
    std::string error;
    std::optional<Image> image = loadImage(path, &error);
    if (!image) {
        log::error("Failed to load texture '{}': {}", path, error);
        return std::nullopt;
    }
    if (alpha == AlphaMode::Premultiply) {
        premultiplyAlpha(*image);
    }

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
    gTextures.push_back({normalized(path), &device, handle, alpha, image->width, image->height});
    return LoadedTexture{handle, image->width, image->height};
}

std::optional<SpriteMaterial> loadMaterial(rhi::Device& device, const std::string& basePath) {
    const auto albedo = loadTexture(device, basePath + ".png");
    if (!albedo) {
        return std::nullopt;
    }
    SpriteMaterial material;
    material.albedo = *albedo;

    // A missing (or unusable) companion is a 1×1 texture of its neutral value, tracked under the
    // companion's path: drawing is the same as without one, and a file added later hot-reloads into it.
    auto loadCompanion = [&](const std::string& suffix, Color neutral) -> rhi::TextureHandle {
        const std::string path = basePath + suffix + ".png";
        if (SDL_GetPathInfo(path.c_str(), nullptr)) {
            if (const auto texture = loadTexture(device, path, AlphaMode::Keep)) {
                if (texture->width == albedo->width && texture->height == albedo->height) {
                    return texture->handle;
                }
                log::error("'{}' is {}x{} but its albedo is {}x{}; ignoring it", path, texture->width,
                           texture->height, albedo->width, albedo->height);
                forgetLoaded(texture->handle);
                device.destroy(texture->handle);
            }
        }
        rhi::TextureDesc desc;
        desc.width = 1;
        desc.height = 1;
        desc.debugName = path.c_str();
        const rhi::TextureHandle placeholder = device.createTexture(desc);
        if (placeholder) {
            device.writeTexture(placeholder, {0, 0, 1, 1}, &neutral, 4);
            gTextures.push_back({normalized(path), &device, placeholder, AlphaMode::Keep, 1, 1});
        }
        return placeholder;
    };
    material.normal = loadCompanion("_n", Color::rgba(128, 128, 255)); // flat, facing the viewer
    material.emissive = loadCompanion("_e", Color::rgba(0, 0, 0));     // no emission
    return material;
}

void destroyMaterial(rhi::Device& device, SpriteMaterial& material) {
    forgetLoaded(material.albedo.handle);
    forgetLoaded(material.normal);
    forgetLoaded(material.emissive);
    device.destroy(material.albedo.handle);
    device.destroy(material.normal);
    device.destroy(material.emissive);
    material = {};
}

rhi::ShaderHandle loadShader(rhi::Device& device, const std::string& path) {
    const std::optional<rhi::ShaderPackage> package = readShaderPackage(path);
    if (!package) {
        return {};
    }
    rhi::ShaderDesc desc;
    desc.package = &*package;
    desc.debugName = path.c_str();
    const rhi::ShaderHandle handle = device.createShader(desc);
    if (handle) {
        gShaders.push_back({normalized(path), &device, handle, package->stage, package->bindings});
    }
    return handle;
}

ReloadResult reloadShaderFile(rhi::Device& device, const std::string& path) {
    const std::string key = normalized(path);
    const bool loaded = std::ranges::any_of(gShaders, [&](const TrackedShader& s) {
        return s.device == &device && s.path == key;
    });
    if (!loaded) {
        return ReloadResult::NotLoaded;
    }
    const std::optional<rhi::ShaderPackage> package = readShaderPackage(path);
    if (!package) {
        return ReloadResult::Failed;
    }
    for (const TrackedShader& s : gShaders) {
        if (s.device == &device && s.path == key &&
            (s.stage != package->stage || !rhi::sameBindings(s.bindings, package->bindings))) {
            log::warn("'{}' changed its stage or resource bindings: its pipelines must be rebuilt", path);
            return ReloadResult::Incompatible;
        }
    }
    rhi::ShaderDesc desc;
    desc.package = &*package;
    desc.debugName = path.c_str();
    bool ok = true;
    for (const TrackedShader& s : gShaders) {
        if (s.device == &device && s.path == key) {
            ok = device.reloadShader(s.handle, desc) && ok;
        }
    }
    return ok ? ReloadResult::Reloaded : ReloadResult::Failed;
}

ReloadResult reloadTextureFile(rhi::Device& device, const std::string& path) {
    const std::string key = normalized(path);
    bool loaded = false;
    bool ok = true;
    std::optional<Image> straight;
    for (TrackedTexture& t : gTextures) {
        if (t.device != &device || t.path != key) {
            continue;
        }
        if (!loaded) {
            loaded = true;
            std::string error;
            straight = loadImage(path, &error);
            if (!straight) {
                log::error("Failed to reload texture '{}': {}", path, error);
                return ReloadResult::Failed;
            }
        }
        if (straight->width != t.width || straight->height != t.height) {
            // Same handle, new size: bind groups stay valid; renderers read the size from the device.
            rhi::TextureDesc desc;
            desc.width = straight->width;
            desc.height = straight->height;
            desc.debugName = path.c_str();
            if (!device.recreateTexture(t.handle, desc)) {
                log::error("'{}' is now {}x{} (was {}x{}) and could not be resized", path, straight->width,
                           straight->height, t.width, t.height);
                ok = false;
                continue;
            }
            log::info("'{}' resized from {}x{} to {}x{}", path, t.width, t.height, straight->width, straight->height);
            t.width = straight->width;
            t.height = straight->height;
        }
        Image image = *straight;
        if (t.alpha == AlphaMode::Premultiply) {
            premultiplyAlpha(image);
        }
        device.writeTexture(t.handle, {0, 0, image.width, image.height}, image.pixels.data(), image.width * 4);
    }
    if (!loaded) {
        return ReloadResult::NotLoaded;
    }
    if (ok) {
        log::info("Reloaded texture '{}'", path);
    }
    return ok ? ReloadResult::Reloaded : ReloadResult::Failed;
}

void forgetLoaded(rhi::TextureHandle texture) {
    std::erase_if(gTextures, [texture](const TrackedTexture& t) { return t.handle == texture; });
}

void forgetLoaded(rhi::ShaderHandle shader) {
    std::erase_if(gShaders, [shader](const TrackedShader& s) { return s.handle == shader; });
}

void destroyShader(rhi::Device& device, rhi::ShaderHandle shader) {
    forgetLoaded(shader);
    device.destroy(shader);
}

} // namespace animal
