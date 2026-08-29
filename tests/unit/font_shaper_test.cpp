#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

#include "jrpgmaker/ui/glyph_atlas.hpp"
#include "jrpgmaker/ui/text.hpp"
#include "jrpgmaker/ui/text_draw.hpp"

using jrpgmaker::ui::Font;
using jrpgmaker::ui::GlyphAtlas;
using jrpgmaker::ui::TextRun;
using jrpgmaker::ui::TextShaper;

namespace {

// Font paths known to exist on each supported dev platform. Tests that need a
// real font skip when none is present (CI installs fonts-noto-cjk on Linux;
// Windows/macOS ship CJK fonts). The Linux path is the Debian/Ubuntu Noto CJK
// Regular face.
std::filesystem::path FindCjkFont() {
#if defined(_WIN32)
    const std::filesystem::path windows = "C:/Windows/Fonts/msgothic.ttc";
    if (std::filesystem::exists(windows)) {
        return windows;
    }
#elif defined(__APPLE__)
    // ASCII-named CJK faces shipped with macOS (Hiragino/STHeiti).
    for (const char* candidate :
         {"/System/Library/Fonts/STHeiti Light.ttc", "/System/Library/Fonts/STHeiti Medium.ttc"}) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
#else
    for (const char* candidate : {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                                  "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
                                  "/usr/share/fonts/opentype/noto/NotoSansCJKjp-Regular.otf"}) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
#endif
    return {};
}

} // namespace

TEST_CASE("font loads a CJK font and reports em metrics", "[ui][font]") {
    const auto font_path = FindCjkFont();
    if (font_path.empty()) {
        SKIP("no CJK system font found on this host");
    }

    Font font;
    REQUIRE(font.Load(font_path.string()));
    REQUIRE(font.units_per_em() > 0);
    REQUIRE(font.ascender() > 0.0f);
    REQUIRE(font.descender() < 0.0f);
    REQUIRE(font.LoadGlyph(0x4E16u, 24u));
    REQUIRE(font.glyph_width() > 0);
    REQUIRE(font.glyph_height() > 0);
    REQUIRE_FALSE(font.glyph_bitmap().empty());
    REQUIRE(font.glyph_pitch() != 0);
}

TEST_CASE("shaper produces glyphs for a CJK string", "[ui][font][shaper]") {
    const auto font_path = FindCjkFont();
    if (font_path.empty()) {
        SKIP("no CJK system font found on this host");
    }

    Font font;
    REQUIRE(font.Load(font_path.string()));
    TextShaper shaper;
    const std::string text = "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf"
                             "\xe4\xb8\x96\xe7\x95\x8c"; // konnichiwa sekai
    const TextRun run = shaper.Shape(font, text, 24u);

    REQUIRE_FALSE(run.glyphs.empty());
    REQUIRE(run.width > 0.0f);
    // Every glyph carries a cluster index into the source and a pen advance.
    for (const auto& glyph : run.glyphs) {
        REQUIRE(glyph.advance_x >= 0.0f);
    }
}

TEST_CASE("shaper returns an empty run for empty text", "[ui][font][shaper]") {
    Font font;
    TextShaper shaper;
    const TextRun run = shaper.Shape(font, "", 24u);
    REQUIRE(run.glyphs.empty());
    REQUIRE(run.width == 0.0f);
}

TEST_CASE("shaper keeps glyph order and cluster monotonicity for CJK", "[ui][font][shaper]") {
    const auto font_path = FindCjkFont();
    if (font_path.empty()) {
        SKIP("no CJK system font found on this host");
    }

    Font font;
    REQUIRE(font.Load(font_path.string()));
    TextShaper shaper;
    const std::string text = "\xe5\x86\x92\xe9\x99\xba\xe8\x80\x85"; // boukensha
    const TextRun run = shaper.Shape(font, text, 24u);

    REQUIRE(run.glyphs.size() >= 3);
    std::uint32_t previous_cluster = 0;
    for (const auto& glyph : run.glyphs) {
        REQUIRE(glyph.cluster >= previous_cluster);
        previous_cluster = glyph.cluster;
    }
}

TEST_CASE("glyph atlas packs rendered glyphs with deterministic UVs", "[ui][font][atlas]") {
    const auto font_path = FindCjkFont();
    if (font_path.empty()) {
        SKIP("no CJK system font found on this host");
    }

    Font font;
    REQUIRE(font.Load(font_path.string()));
    GlyphAtlas atlas(128, 64, 16);
    const auto first = atlas.Add(font, 0x4E16u, 24u);
    REQUIRE(first.has_value());
    REQUIRE(first->width > 0);
    REQUIRE(first->height > 0);
    REQUIRE(first->u0 < first->u1);
    REQUIRE(first->v0 < first->v1);
    bool has_ink = false;
    for (std::uint32_t row = 0; row < first->height; ++row)
        for (std::uint32_t column = 0; column < first->width; ++column)
            has_ink =
                has_ink || atlas.pixels()[static_cast<std::size_t>(first->y + row) * atlas.width() +
                                          first->x + column] > 0;
    REQUIRE(has_ink);
    const auto duplicate = atlas.Add(font, 0x4E16u, 24u);
    REQUIRE(duplicate.has_value());
    REQUIRE(duplicate->x == first->x);
    REQUIRE(duplicate->y == first->y);
}

TEST_CASE("text draw resolves localized CJK into glyph quads", "[ui][font][text-draw]") {
    const auto font_path = FindCjkFont();
    if (font_path.empty())
        SKIP("no CJK system font found on this host");

    Font font;
    REQUIRE(font.Load(font_path.string()));
    jrpgmaker::ui::EditorLocale locale;
    locale.strings.emplace("editor.title", "世界");
    jrpgmaker::ui::DrawList source;
    REQUIRE(source.Add(jrpgmaker::ui::DrawText{{10.0f, 20.0f, 100.0f, 30.0f}, "editor.title"}));
    GlyphAtlas atlas(128, 64, 16);
    const auto result = jrpgmaker::ui::BuildTextDrawList(source, locale, font, atlas, 24);
    REQUIRE(result.ok());
    REQUIRE(result.draw_list.size() == 2);
    const auto* glyph = std::get_if<jrpgmaker::ui::DrawGlyph>(
        &result.draw_list.primitives().front());
    REQUIRE(glyph != nullptr);
    REQUIRE(glyph->rect.width > 0.0f);
    REQUIRE(glyph->uv.width > 0.0f);
}
