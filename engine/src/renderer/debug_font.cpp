#include "debug_font.h"

#include <animal/core/assert.h>

#include <cstring>
#include <vector>

namespace animal {

namespace {

// Atlas: ASCII 32..127 in a 16x6 grid of 4x6 cells. Cell 127 is solid (for panels).
constexpr int kColumns = 16;
constexpr int kRows = 6;
constexpr int kCellWidth = 4;
constexpr int kCellHeight = 6;
constexpr int kAtlasWidth = kColumns * kCellWidth;
constexpr int kAtlasHeight = kRows * kCellHeight;
constexpr char kSolidCell = 127;

struct Glyph {
    char c;
    const char* rows; // 5 rows of 3 pixels, '#' = set
};

// clang-format off
constexpr Glyph kGlyphs[] = {
    {'0', "###" "#.#" "#.#" "#.#" "###"}, {'1', ".#." "##." ".#." ".#." "###"},
    {'2', "###" "..#" "###" "#.." "###"}, {'3', "###" "..#" ".##" "..#" "###"},
    {'4', "#.#" "#.#" "###" "..#" "..#"}, {'5', "###" "#.." "###" "..#" "###"},
    {'6', "###" "#.." "###" "#.#" "###"}, {'7', "###" "..#" "..#" ".#." ".#."},
    {'8', "###" "#.#" "###" "#.#" "###"}, {'9', "###" "#.#" "###" "..#" "###"},
    {'A', ".#." "#.#" "###" "#.#" "#.#"}, {'B', "##." "#.#" "##." "#.#" "##."},
    {'C', ".##" "#.." "#.." "#.." ".##"}, {'D', "##." "#.#" "#.#" "#.#" "##."},
    {'E', "###" "#.." "##." "#.." "###"}, {'F', "###" "#.." "##." "#.." "#.."},
    {'G', ".##" "#.." "#.#" "#.#" ".##"}, {'H', "#.#" "#.#" "###" "#.#" "#.#"},
    {'I', "###" ".#." ".#." ".#." "###"}, {'J', "..#" "..#" "..#" "#.#" ".#."},
    {'K', "#.#" "#.#" "##." "#.#" "#.#"}, {'L', "#.." "#.." "#.." "#.." "###"},
    {'M', "#.#" "###" "###" "#.#" "#.#"}, {'N', "##." "#.#" "#.#" "#.#" "#.#"},
    {'O', ".#." "#.#" "#.#" "#.#" ".#."}, {'P', "##." "#.#" "##." "#.." "#.."},
    {'Q', ".#." "#.#" "#.#" "##." ".##"}, {'R', "##." "#.#" "##." "#.#" "#.#"},
    {'S', ".##" "#.." ".#." "..#" "##."}, {'T', "###" ".#." ".#." ".#." ".#."},
    {'U', "#.#" "#.#" "#.#" "#.#" "###"}, {'V', "#.#" "#.#" "#.#" "#.#" ".#."},
    {'W', "#.#" "#.#" "###" "###" "#.#"}, {'X', "#.#" "#.#" ".#." "#.#" "#.#"},
    {'Y', "#.#" "#.#" ".#." ".#." ".#."}, {'Z', "###" "..#" ".#." "#.." "###"},
    {'.', "..." "..." "..." "..." ".#."}, {',', "..." "..." "..." ".#." "#.."},
    {':', "..." ".#." "..." ".#." "..."}, {'-', "..." "..." "###" "..." "..."},
    {'+', "..." ".#." "###" ".#." "..."}, {'/', "..#" "..#" ".#." "#.." "#.."},
    {'%', "#.#" "..#" ".#." "#.." "#.#"}, {'(', ".#." "#.." "#.." "#.." ".#."},
    {')', ".#." "..#" "..#" "..#" ".#."}, {'=', "..." "###" "..." "###" "..."},
    {'_', "..." "..." "..." "..." "###"}, {'!', ".#." ".#." ".#." "..." ".#."},
    {'?', "##." "..#" ".#." "..." ".#."}, {'<', "..#" ".#." "#.." ".#." "..#"},
    {'>', "#.." ".#." "..#" ".#." "#.."}, {'[', "##." "#.." "#.." "#.." "##."},
    {']', ".##" "..#" "..#" "..#" ".##"}, {'\'', ".#." ".#." "..." "..." "..."},
    {'*', "..." "#.#" ".#." "#.#" "..."}, {'#', "#.#" "###" "#.#" "###" "#.#"},
    {'|', ".#." ".#." ".#." ".#." ".#."}, {'@', "###" "#.#" "#.#" "#.." "###"},
};
// clang-format on

bool hasGlyph(char c) {
    if (c == ' ') {
        return true;
    }
    for (const Glyph& glyph : kGlyphs) {
        if (glyph.c == c) {
            return true;
        }
    }
    return false;
}

RectF cellUV(char c, float x, float y, float w, float h) {
    const int index = static_cast<unsigned char>(c) - 32;
    const float cx = static_cast<float>((index % kColumns) * kCellWidth);
    const float cy = static_cast<float>((index / kColumns) * kCellHeight);
    return {(cx + x) / kAtlasWidth, (cy + y) / kAtlasHeight, w / kAtlasWidth, h / kAtlasHeight};
}

} // namespace

bool DebugFont::init(rhi::Device& device) {
    std::vector<uint8_t> pixels(size_t{kAtlasWidth} * kAtlasHeight * 4, 0);
    auto setPixel = [&pixels](int x, int y) {
        uint8_t* p = &pixels[(size_t(y) * kAtlasWidth + size_t(x)) * 4];
        p[0] = p[1] = p[2] = p[3] = 255;
    };

    for (const Glyph& glyph : kGlyphs) {
        ANIMAL_ASSERT(std::strlen(glyph.rows) == kGlyphWidth * kGlyphHeight);
        const int index = glyph.c - 32;
        const int ox = (index % kColumns) * kCellWidth;
        const int oy = (index / kColumns) * kCellHeight;
        for (int y = 0; y < kGlyphHeight; ++y) {
            for (int x = 0; x < kGlyphWidth; ++x) {
                if (glyph.rows[y * kGlyphWidth + x] == '#') {
                    setPixel(ox + x, oy + y);
                }
            }
        }
    }
    const int solid = kSolidCell - 32;
    for (int y = 0; y < kCellHeight; ++y) {
        for (int x = 0; x < kCellWidth; ++x) {
            setPixel((solid % kColumns) * kCellWidth + x, (solid / kColumns) * kCellHeight + y);
        }
    }

    rhi::TextureDesc desc;
    desc.width = kAtlasWidth;
    desc.height = kAtlasHeight;
    desc.format = rhi::Format::RGBA8_UNorm;
    desc.usage = rhi::TextureUsage::Sampled;
    desc.debugName = "debug font";
    texture_ = device.createTexture(desc);
    if (!texture_) {
        return false;
    }
    device.writeTexture(texture_, {0, 0, kAtlasWidth, kAtlasHeight}, pixels.data(), kAtlasWidth * 4);
    return true;
}

void DebugFont::shutdown(rhi::Device& device) {
    device.destroy(texture_);
    texture_ = {};
}

RectF DebugFont::glyphUV(char c) const {
    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
    }
    if (!hasGlyph(c)) {
        c = '?';
    }
    return cellUV(c, 0.0f, 0.0f, kGlyphWidth, kGlyphHeight);
}

RectF DebugFont::solidUV() const {
    return cellUV(kSolidCell, 1.0f, 1.0f, 1.0f, 1.0f);
}

} // namespace animal
