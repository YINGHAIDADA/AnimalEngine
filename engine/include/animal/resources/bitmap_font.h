#pragma once

// Pixel fonts from a glyph sheet (ADR-0041): every glyph in a fixed cell of one texture, each with its
// own advance; text is UTF-8. Renderer2D::drawText without a font uses the built-in debug font instead.

#include <animal/core/math.h>
#include <animal/resources/loaders.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace animal {

// <name>.font.json:
//   {"format": "animal-font", "version": 1, "image": "ui.png", "cellWidth": 12, "cellHeight": 16,
//    "baseline": 13, "columns": 128, "missing": 0, "characters": "...", "advances": [12, 4, ...]}
// Cell 0 of the sheet is the missing-glyph box and characters[i] (UTF-8) is in cell i + 1; `advances`
// has one entry per cell, the box first. Cells run left to right, then down.
struct BitmapFontDesc {
    std::string image; // resolved against the description's folder
    int cellWidth = 0;
    int cellHeight = 0;
    int baseline = 0; // rows above the baseline
    int columns = 0;
    std::vector<char32_t> characters; // cell i + 1
    std::vector<int> advances;        // per cell
};

std::optional<BitmapFontDesc> parseBitmapFont(std::string_view json, const std::string& path, std::string* error);

// The code point starting at `offset`, which moves past it; a malformed sequence gives U+FFFD and
// skips one byte.
char32_t nextCodePoint(std::string_view text, size_t& offset);

class BitmapFont {
public:
    struct Glyph {
        int cell = 0; // 0: the missing-glyph box
        int advance = 0;
    };

    BitmapFont() = default;
    BitmapFont(BitmapFontDesc desc, LoadedTexture texture);
    // Loads the description and its sheet; logs and returns nullopt on failure.
    static std::optional<BitmapFont> load(rhi::Device& device, const std::string& path);
    void destroy(rhi::Device& device);

    bool has(char32_t c) const;
    Glyph glyph(char32_t c) const; // the box for characters the font lacks
    // The characters of `text` the font lacks (each once, in order).
    std::u32string missing(std::string_view text) const;
    int width(std::string_view text) const; // in font pixels
    int lineHeight() const { return desc_.cellHeight; }
    int cellWidth() const { return desc_.cellWidth; }
    // The cell's rectangle in normalized texture coordinates.
    RectF cellUV(int cell) const;
    rhi::TextureHandle texture() const { return texture_.handle; }

private:
    BitmapFontDesc desc_;
    LoadedTexture texture_;
    std::unordered_map<char32_t, int> cells_;
};

} // namespace animal
