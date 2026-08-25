#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "jrpgmaker/ui/text.hpp"

using jrpgmaker::ui::LineBreaker;
using jrpgmaker::ui::ShapedGlyph;
using jrpgmaker::ui::TextLine;
using jrpgmaker::ui::TextRun;

namespace {

// Builds a TextRun from a UTF-8 string where every codepoint becomes one glyph
// with the given advance. Clusters point at each codepoint's UTF-8 byte offset,
// matching HarfBuzz semantics so LineBreaker can resolve kinsoku classes.
TextRun MakeRun(const std::string& text, float advance = 16.0f) {
    TextRun run;
    std::size_t offset = 0;
    while (offset < text.size()) {
        ShapedGlyph glyph;
        glyph.cluster = static_cast<std::uint32_t>(offset);
        glyph.advance_x = advance;
        run.width += advance;
        run.glyphs.push_back(glyph);
        // Advance past one UTF-8 codepoint.
        const auto lead = static_cast<unsigned char>(text[offset]);
        offset += lead < 0x80 ? 1 : (lead < 0xE0 ? 2 : (lead < 0xF0 ? 3 : 4));
    }
    return run;
}

std::vector<std::string> LineTexts(const std::string& source, const TextRun& run,
                                   const std::vector<TextLine>& lines) {
    std::vector<std::string> result;
    for (const TextLine& line : lines) {
        const std::size_t first_glyph = line.first_glyph;
        const std::size_t last_glyph = first_glyph + line.glyph_count - 1;
        const std::size_t start_byte = run.glyphs[first_glyph].cluster;
        const std::size_t end_byte = run.glyphs[last_glyph].cluster;
        std::size_t off = end_byte;
        const auto lead = static_cast<unsigned char>(source[off]);
        off += lead < 0x80 ? 1 : (lead < 0xE0 ? 2 : (lead < 0xF0 ? 3 : 4));
        result.push_back(source.substr(start_byte, off - start_byte));
    }
    return result;
}

} // namespace

TEST_CASE("line breaker fits text into fixed-width lines", "[ui][layout]") {
    const std::string text = "こんにちは世界"; // 7 CJK glyphs at 16px = 112px
    const TextRun run = MakeRun(text);
    LineBreaker breaker;
    const auto layout = breaker.Break(run, text, 64.0f, 20.0f, 16.0f);

    REQUIRE(layout.lines.size() == 2);
    REQUIRE(layout.lines[0].glyph_count == 4);
    REQUIRE(layout.lines[1].glyph_count == 3);
    REQUIRE(layout.lines[0].width == Catch::Approx(64.0f));
    REQUIRE(layout.lines[1].width == Catch::Approx(48.0f));
    REQUIRE(layout.lines[0].height == 20.0f);
    REQUIRE(layout.lines[0].baseline == 16.0f);
    REQUIRE(layout.width == Catch::Approx(64.0f));
    REQUIRE(layout.height == Catch::Approx(40.0f));
}

TEST_CASE("line breaker does not start a line with a closing bracket (kinsoku leading)",
          "[ui][layout]") {
    // width 64 fits 4 CJK glyphs (16px each). Greedy fill gives あいうえ; the
    // next glyph is 」 (leading-prohibited) so it hangs onto line 1, which then
    // overflows to 80px. Line 2 starts with お.
    const std::string text = "あいうえ」お";
    const TextRun run = MakeRun(text);
    LineBreaker breaker;
    const auto layout = breaker.Break(run, text, 64.0f, 20.0f, 16.0f);

    REQUIRE(layout.lines.size() == 2);
    REQUIRE(layout.lines[0].glyph_count == 5);              // あいうえ」
    REQUIRE(layout.lines[1].glyph_count == 1);              // お
    REQUIRE(layout.lines[0].width == Catch::Approx(80.0f)); // hung, overflows

    const auto texts = LineTexts(text, run, layout.lines);
    REQUIRE(texts[0] == "あいうえ」");
    REQUIRE(texts[1] == "お");
}

TEST_CASE("line breaker does not end a line with an opening bracket (kinsoku trailing)",
          "[ui][layout]") {
    // width 80 fits 5 glyphs. Greedy fill lands on あいうえ「 (80px), but a
    // line must not END with 「, so the break steps back: line 1 = あいうえ,
    // line 2 = 「お.
    const std::string text = "あいうえ「お";
    const TextRun run = MakeRun(text);
    LineBreaker breaker;
    const auto layout = breaker.Break(run, text, 80.0f, 20.0f, 16.0f);

    REQUIRE(layout.lines.size() == 2);
    REQUIRE(layout.lines[0].glyph_count == 4); // あいうえ
    REQUIRE(layout.lines[1].glyph_count == 2); // 「お
    REQUIRE(layout.lines[0].width == Catch::Approx(64.0f));

    const auto texts = LineTexts(text, run, layout.lines);
    REQUIRE(texts[0] == "あいうえ");
    REQUIRE(texts[1] == "「お");
}

TEST_CASE("line breaker handles a single glyph wider than the line", "[ui][layout]") {
    const std::string text = "広";
    const TextRun run = MakeRun(text, 100.0f); // one glyph, 100px, line width 64
    LineBreaker breaker;
    const auto layout = breaker.Break(run, text, 64.0f, 20.0f, 16.0f);

    REQUIRE(layout.lines.size() == 1);
    REQUIRE(layout.lines[0].glyph_count == 1);
}

TEST_CASE("line breaker returns no lines for empty run", "[ui][layout]") {
    const TextRun run;
    LineBreaker breaker;
    const auto layout = breaker.Break(run, "", 64.0f, 20.0f, 16.0f);
    REQUIRE(layout.lines.empty());
    REQUIRE(layout.width == 0.0f);
    REQUIRE(layout.height == 0.0f);
}

TEST_CASE("line breaker breaks ASCII without kinsoku treatment", "[ui][layout]") {
    // ASCII 'a' 'b' space 'c' 'd', advance 10, width 25 fits 2 glyphs. Spaces
    // are not kinsoku characters, so greedy fill breaks at the width limit.
    const std::string text = "ab cd";
    const TextRun run = MakeRun(text, 10.0f);
    LineBreaker breaker;
    const auto layout = breaker.Break(run, text, 25.0f, 20.0f, 16.0f);

    REQUIRE(layout.lines.size() == 3);
    REQUIRE(layout.lines[0].glyph_count == 2); // "ab"
    REQUIRE(layout.lines[1].glyph_count == 2); // " c"
    REQUIRE(layout.lines[2].glyph_count == 1); // "d"
}