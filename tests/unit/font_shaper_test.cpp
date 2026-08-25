#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

#include "jrpgmaker/ui/text.hpp"

using jrpgmaker::ui::Font;
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