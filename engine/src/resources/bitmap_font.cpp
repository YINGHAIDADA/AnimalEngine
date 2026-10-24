#include <animal/core/file.h>
#include <animal/core/log.h>
#include <animal/resources/bitmap_font.h>

#include <json.hpp>

#include <format>

namespace animal {

namespace {

using Json = nlohmann::json;

std::string folderOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

} // namespace

char32_t nextCodePoint(std::string_view text, size_t& offset) {
    const auto byte = [&](size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(offset);
    int length = 0;
    char32_t c = 0;
    if (lead < 0x80) {
        ++offset;
        return lead;
    } else if ((lead & 0xE0) == 0xC0) {
        length = 2;
        c = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        c = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        c = lead & 0x07;
    } else {
        ++offset;
        return 0xFFFD;
    }
    if (offset + length > text.size()) {
        ++offset;
        return 0xFFFD;
    }
    for (int i = 1; i < length; ++i) {
        if ((byte(offset + i) & 0xC0) != 0x80) {
            ++offset;
            return 0xFFFD;
        }
        c = (c << 6) | (byte(offset + i) & 0x3F);
    }
    offset += length;
    return c;
}

std::optional<BitmapFontDesc> parseBitmapFont(std::string_view json, const std::string& path, std::string* error) {
    const auto fail = [&](const std::string& message) -> std::optional<BitmapFontDesc> {
        if (error) {
            *error = std::format("{}: {}", path, message);
        }
        return std::nullopt;
    };
    Json root;
    try {
        root = Json::parse(json.begin(), json.end());
    } catch (const Json::exception& e) {
        return fail(std::string("invalid JSON (") + e.what() + ")");
    }
    if (!root.is_object() || root.value("format", "") != "animal-font") {
        return fail("not a font description");
    }
    if (root.value("version", 0) != 1) {
        return fail("unsupported version");
    }
    BitmapFontDesc desc;
    try {
        desc.image = folderOf(path) + root.at("image").get<std::string>();
        desc.cellWidth = root.at("cellWidth").get<int>();
        desc.cellHeight = root.at("cellHeight").get<int>();
        desc.baseline = root.at("baseline").get<int>();
        desc.columns = root.at("columns").get<int>();
        const std::string characters = root.at("characters").get<std::string>();
        for (size_t offset = 0; offset < characters.size();) {
            desc.characters.push_back(nextCodePoint(characters, offset));
        }
        desc.advances = root.at("advances").get<std::vector<int>>();
    } catch (const Json::exception& e) {
        return fail(std::string("missing or mistyped field (") + e.what() + ")");
    }
    if (desc.cellWidth <= 0 || desc.cellHeight <= 0 || desc.columns <= 0) {
        return fail("cell size and columns must be positive");
    }
    if (desc.advances.size() != desc.characters.size() + 1) {
        return fail(std::format("{} advances for {} cells", desc.advances.size(), desc.characters.size() + 1));
    }
    return desc;
}

BitmapFont::BitmapFont(BitmapFontDesc desc, LoadedTexture texture) : desc_(std::move(desc)), texture_(texture) {
    for (size_t i = 0; i < desc_.characters.size(); ++i) {
        cells_.emplace(desc_.characters[i], static_cast<int>(i + 1));
    }
}

std::optional<BitmapFont> BitmapFont::load(rhi::Device& device, const std::string& path) {
    const auto bytes = readFile(path);
    if (!bytes) {
        log::error("Cannot read font '{}'", path);
        return std::nullopt;
    }
    std::string error;
    auto desc = parseBitmapFont(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), path,
                                &error);
    if (!desc) {
        log::error("Invalid font: {}", error);
        return std::nullopt;
    }
    const auto texture = loadTexture(device, desc->image);
    if (!texture) {
        return std::nullopt;
    }
    const int rows = (static_cast<int>(desc->characters.size()) + 1 + desc->columns - 1) / desc->columns;
    if (static_cast<int>(texture->width) < desc->columns * desc->cellWidth ||
        static_cast<int>(texture->height) < rows * desc->cellHeight) {
        log::error("Font '{}': the sheet is too small for {} cells", path, desc->characters.size() + 1);
        device.destroy(texture->handle);
        return std::nullopt;
    }
    return BitmapFont(std::move(*desc), *texture);
}

void BitmapFont::destroy(rhi::Device& device) {
    if (texture_.handle) {
        forgetLoaded(texture_.handle);
        device.destroy(texture_.handle);
        texture_ = {};
    }
}

bool BitmapFont::has(char32_t c) const {
    return cells_.contains(c);
}

BitmapFont::Glyph BitmapFont::glyph(char32_t c) const {
    const auto it = cells_.find(c);
    const int cell = it == cells_.end() ? 0 : it->second;
    return {cell, desc_.advances[static_cast<size_t>(cell)]};
}

std::u32string BitmapFont::missing(std::string_view text) const {
    std::u32string out;
    for (size_t offset = 0; offset < text.size();) {
        const char32_t c = nextCodePoint(text, offset);
        if (!has(c) && out.find(c) == std::u32string::npos) {
            out.push_back(c);
        }
    }
    return out;
}

int BitmapFont::width(std::string_view text) const {
    int total = 0;
    for (size_t offset = 0; offset < text.size();) {
        total += glyph(nextCodePoint(text, offset)).advance;
    }
    return total;
}

RectF BitmapFont::cellUV(int cell) const {
    const auto w = static_cast<float>(texture_.width);
    const auto h = static_cast<float>(texture_.height);
    const float x = static_cast<float>((cell % desc_.columns) * desc_.cellWidth);
    const float y = static_cast<float>((cell / desc_.columns) * desc_.cellHeight);
    return {x / w, y / h, static_cast<float>(desc_.cellWidth) / w, static_cast<float>(desc_.cellHeight) / h};
}

} // namespace animal
