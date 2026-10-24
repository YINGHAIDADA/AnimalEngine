// Pixel fonts from a glyph sheet (ADR-0041).

#include <animal/resources/bitmap_font.h>

#include "test_harness.h"

#include <string>

namespace {

using namespace animal;

std::u32string decode(std::string_view text) {
    std::u32string out;
    for (size_t offset = 0; offset < text.size();) {
        out.push_back(nextCodePoint(text, offset));
    }
    return out;
}

constexpr const char* kFont = R"({"format": "animal-font", "version": 1, "image": "ui.png", "cellWidth": 12,
    "cellHeight": 16, "baseline": 13, "columns": 4, "missing": 0, "characters": " A中。",
    "advances": [12, 4, 8, 12, 12]})";

TEST(utf8_decodes_and_replaces_malformed_bytes) {
    CHECK(decode("A\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80") == U"A\u00E9\u4E2D\U0001F600");
    CHECK(decode("\xFF" "B") == U"\uFFFDB");     // not a lead byte
    CHECK(decode("\xE4\xB8") == U"\uFFFD\uFFFD"); // truncated: one replacement per skipped byte
    CHECK(decode("\xE4" "AB") == U"\uFFFDAB");    // continuation byte missing
}

TEST(font_description_parses_and_resolves_its_image) {
    std::string error;
    const auto desc = parseBitmapFont(kFont, "data/fonts/ui.font.json", &error);
    CHECK(desc.has_value());
    CHECK(desc->image == "data/fonts/ui.png");
    CHECK(desc->characters == std::vector<char32_t>({U' ', U'A', U'中', U'。'}));
    CHECK(desc->advances.size() == 5 && desc->baseline == 13);

    CHECK(!parseBitmapFont(R"({"format": "animal-font", "version": 1, "image": "ui.png", "cellWidth": 12,
        "cellHeight": 16, "baseline": 13, "columns": 4, "characters": "AB", "advances": [12, 4]})",
                           "ui.font.json", &error));
    CHECK(error.find("advances") != std::string::npos);
    CHECK(!parseBitmapFont(R"({"format": "animal-look", "version": 1})", "ui.font.json", &error));
    CHECK(!parseBitmapFont(R"({"format": "animal-font", "version": 1, "image": "ui.png"})", "ui.font.json", &error));
}

TEST(font_measures_text_and_reports_missing_characters) {
    const auto desc = parseBitmapFont(kFont, "ui.font.json", nullptr);
    CHECK(desc.has_value());
    LoadedTexture sheet;
    sheet.width = 48;
    sheet.height = 32;
    const BitmapFont font(*desc, sheet);
    CHECK(font.width("A 中。") == 8 + 4 + 12 + 12);
    CHECK(font.glyph(U'中').cell == 3);
    CHECK(font.glyph(U'字').cell == 0 && font.glyph(U'字').advance == 12); // the box
    CHECK(font.missing("A字字中B") == U"字B");
    const RectF cell = font.cellUV(5); // second row, second column
    CHECK(cell.x == 12.0f / 48.0f && cell.y == 0.5f && cell.width == 0.25f && cell.height == 0.5f);
}

} // namespace
