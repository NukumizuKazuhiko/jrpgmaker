#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace jrpgmaker::ui {

// Pure-CPU text layout layer (P3 subtask: FreeType+HarfBuzz CJK typesetting).
// Produces structured glyph/line data only; rasterization is a later render/ui
// concern (docs/01: ui owns "FreeType+HarfBuzz CJK text layout").
//
// Unit conventions:
//   - ShapedGlyph advances/offsets and TextLine/TextLayout widths are in
//     pixels at the requested pixel height (HarfBuzz 26.6 fixed-point / 64).
//   - Font::ascender/descender/line_gap are FreeType font units relative to
//     units_per_em (caller scales them to pixels when needed).
// The layout engine applies CJK kinsoku (line-break rules) so hanging/leading
// punctuation never dangles at line edges.

// A single shaped glyph with its pen-relative placement.
struct ShapedGlyph {
    std::uint32_t glyph_id = 0;
    std::uint32_t cluster = 0; // index into the source UTF-8 string
    float advance_x = 0.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
};

// One shaped text run: a homogenous sequence of glyphs from one font at one
// scale, in reading order, with total advance width.
struct TextRun {
    std::vector<ShapedGlyph> glyphs;
    float width = 0.0f;
};

// One laid-out line: a contiguous slice of a TextRun after line breaking.
struct TextLine {
    std::size_t first_glyph = 0;
    std::size_t glyph_count = 0;
    float width = 0.0f;
    float height = 0.0f;
    float baseline = 0.0f; // distance from line top to baseline
};

// Laid-out paragraph: a list of lines plus total bounds.
struct TextLayout {
    std::vector<TextLine> lines;
    float width = 0.0f;
    float height = 0.0f;
};

// FreeType-backed font. Loads a font file (TTF/OTF/TTC; face_index selects a
// face in a collection) and reports per-em metrics. Not copyable; move-only.
class Font {
public:
    Font();
    ~Font();

    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
    Font(Font&&) noexcept;
    Font& operator=(Font&&) noexcept;

    // Loads `font_path`. Returns false (with error) if the file is unreadable
    // or not a supported font. face_index 0 is the default face.
    bool Load(const std::string& font_path, unsigned face_index = 0);

    // Height of the em square in font units (FreeType units_per_EM).
    std::uint32_t units_per_em() const;
    // Ascender/descender in font units, scaled by FreeType's face metrics.
    float ascender() const;
    float descender() const;
    float line_gap() const;

    // Loads and caches the glyph for `codepoint` at the given pixel size.
    // Returns false if the glyph is missing.
    bool LoadGlyph(std::uint32_t codepoint, std::uint32_t pixel_height);

    // Advances/bitmap info for the last LoadGlyph result. Width/height/bearing
    // are in pixels; advance is scaled to 1/64 px.
    int glyph_width() const;
    int glyph_height() const;
    int glyph_bearing_x() const;
    int glyph_bearing_y() const;
    std::int64_t glyph_advance_x() const;

    // Opaque access to the underlying FreeType face, used internally by
    // TextShaper to build a HarfBuzz font. Returns nullptr when unloaded.
    void* native_face() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// HarfBuzz-backed shaper. Shapes a UTF-8 string with a font into a TextRun
// (glyph ids, advances, offsets, clusters). Unshaped runs (e.g. glyphs missing
// from the font) still yield glyphs with identity advances.
class TextShaper {
public:
    TextShaper();
    ~TextShaper();

    TextShaper(const TextShaper&) = delete;
    TextShaper& operator=(const TextShaper&) = delete;
    TextShaper(TextShaper&&) noexcept;
    TextShaper& operator=(TextShaper&&) noexcept;

    // Shapes `text` (UTF-8) with `font` at `pixel_height`. The font must have
    // been loaded. Returns a TextRun; empty text yields an empty run.
    TextRun Shape(const Font& font, const std::string& text, std::uint32_t pixel_height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// CJK kinsoku line breaking. Splits a shaped run into lines that fit
// `max_line_width` while enforcing:
//   - leading-prohibition: line must not start with a hanging mark
//     (closing brackets, punctuation such as ideographic comma/full stop)
//   - trailing-prohibition: line must not end with an opening bracket
// Break points are chosen at the last cluster that fits; a single cluster
// wider than the line still starts a new line (no infinite loop).
//
// `source_utf8` is the original UTF-8 text the run was shaped from; glyph
// clusters are byte indices into it, which the breaker decodes to resolve
// kinsoku classes.
class LineBreaker {
public:
    // Splits `run` into lines of at most `max_line_width`. Metrics (height,
    // baseline) are computed from `line_height` and `baseline`.
    TextLayout Break(const TextRun& run, const std::string& source_utf8, float max_line_width,
                     float line_height, float baseline);
};

} // namespace jrpgmaker::ui