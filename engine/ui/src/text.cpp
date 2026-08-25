#include "jrpgmaker/ui/text.hpp"

#include <cstddef>
#include <string>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>

namespace jrpgmaker::ui {

namespace {

// --- CJK kinsoku tables -----------------------------------------------------

// Characters that must not appear at the START of a line (closing/hanging
// punctuation). Breaking before one forces it onto the previous line.
bool IsLeadingProhibited(std::uint32_t cp) {
    switch (cp) {
    case 0x3001: // ideographic comma
    case 0x3002: // ideographic full stop
    case 0xFF0C: // fullwidth comma
    case 0xFF0E: // fullwidth full stop
    case 0x30FB: // katakana middle dot
    case 0xFF1A: // fullwidth colon
    case 0xFF1B: // fullwidth semicolon
    case 0xFF01: // fullwidth exclamation
    case 0xFF1F: // fullwidth question
    case 0x300D: // right corner bracket
    case 0x300F: // right white corner bracket
    case 0x3009: // right angle bracket
    case 0x300B: // right double angle bracket
    case 0xFF09: // fullwidth right parenthesis
    case 0xFF3D: // fullwidth right square bracket
    case 0xFF5D: // fullwidth right curly bracket
    case 0x201D: // right double quotation mark
    case 0x2019: // right single quotation mark
    case 0x2030: // per mille
    case 0x2033: // double prime
    case 0xFF05: // fullwidth percent
    case 0xFF0A: // fullwidth asterisk
    case 0xFF0B: // fullwidth plus
    case 0xFF0D: // fullwidth hyphen-minus
    case 0xFE50: // small comma
    case 0xFF64: // halfwidth ideographic comma
    case 0xFF65: // halfwidth katakana middle dot
    case 0x3005: // ideographic iteration mark
    case 0x3041: // hiragana small a
    case 0x3043: // hiragana small i
    case 0x3045: // hiragana small u
    case 0x3047: // hiragana small e
    case 0x3049: // hiragana small o
    case 0x3063: // hiragana small tsu
    case 0x3083: // hiragana small ya
    case 0x3085: // hiragana small yu
    case 0x3087: // hiragana small yo
    case 0x308E: // hiragana small wa
    case 0x30A1: // katakana small a
    case 0x30A3: // katakana small i
    case 0x30A5: // katakana small u
    case 0x30A7: // katakana small e
    case 0x30A9: // katakana small o
    case 0x30C3: // katakana small tsu
    case 0x30E3: // katakana small ya
    case 0x30E5: // katakana small yu
    case 0x30E7: // katakana small yo
    case 0x30EE: // katakana small wa
        return true;
    default:
        return false;
    }
}

// Characters that must not appear at the END of a line (opening brackets).
bool IsTrailingProhibited(std::uint32_t cp) {
    switch (cp) {
    case 0xFF08: // fullwidth left parenthesis
    case 0xFF3B: // fullwidth left square bracket
    case 0xFF5B: // fullwidth left curly bracket
    case 0x300C: // left corner bracket
    case 0x300E: // left white corner bracket
    case 0x300A: // left double angle bracket
    case 0x3008: // left angle bracket
    case 0x3010: // left black lenticular bracket
    case 0x201C: // left double quotation mark
    case 0x2018: // left single quotation mark
        return true;
    default:
        return false;
    }
}

// Decodes the first UTF-8 codepoint at `s` (length `len` bytes). Returns
// 0xFFFFFFFF (invalid) if the sequence is malformed.
std::uint32_t DecodeUtf8First(const char* s, std::size_t len) {
    if (len == 0) {
        return 0xFFFFFFFF;
    }
    const auto byte = static_cast<unsigned char>(s[0]);
    if (byte < 0x80) {
        return byte;
    }
    std::size_t remaining = 0;
    std::uint32_t codepoint = 0;
    if ((byte & 0xE0) == 0xC0) {
        remaining = 1;
        codepoint = byte & 0x1F;
    } else if ((byte & 0xF0) == 0xE0) {
        remaining = 2;
        codepoint = byte & 0x0F;
    } else if ((byte & 0xF8) == 0xF0) {
        remaining = 3;
        codepoint = byte & 0x07;
    } else {
        return 0xFFFFFFFF;
    }
    if (len < remaining + 1) {
        return 0xFFFFFFFF;
    }
    for (std::size_t i = 1; i <= remaining; ++i) {
        const auto cont = static_cast<unsigned char>(s[i]);
        if ((cont & 0xC0) != 0x80) {
            return 0xFFFFFFFF;
        }
        codepoint = (codepoint << 6) | (cont & 0x3F);
    }
    return codepoint;
}

// Returns the codepoint at the start of the cluster `cluster` in `source`.
// Clusters are byte indices into the UTF-8 source (HarfBuzz semantics). Falls
// back to 0xFFFFFFFF on out-of-range clusters.
std::uint32_t CodepointAtCluster(const std::string& source, std::uint32_t cluster) {
    if (cluster < source.size()) {
        const std::uint32_t cp = DecodeUtf8First(source.data() + cluster, source.size() - cluster);
        if (cp != 0xFFFFFFFF) {
            return cp;
        }
    }
    return 0xFFFFFFFF;
}

} // namespace

// --- Font (FreeType) ---------------------------------------------------------

struct Font::Impl {
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    std::uint32_t units = 0;
    bool glyph_loaded = false;
    FT_GlyphSlot slot = nullptr;
};

Font::Font() : impl_(std::make_unique<Impl>()) {}
Font::~Font() {
    if (impl_->face != nullptr) {
        FT_Done_Face(impl_->face);
    }
    if (impl_->library != nullptr) {
        FT_Done_FreeType(impl_->library);
    }
}

Font::Font(Font&&) noexcept = default;
Font& Font::operator=(Font&&) noexcept = default;

bool Font::Load(const std::string& font_path, unsigned face_index) {
    if (FT_Init_FreeType(&impl_->library) != 0) {
        return false;
    }
    if (FT_New_Face(impl_->library, font_path.c_str(), static_cast<FT_Long>(face_index),
                    &impl_->face) != 0) {
        FT_Done_FreeType(impl_->library);
        impl_->library = nullptr;
        return false;
    }
    impl_->units = impl_->face->units_per_EM;
    impl_->slot = impl_->face->glyph;
    return true;
}

std::uint32_t Font::units_per_em() const {
    return impl_->units;
}

float Font::ascender() const {
    return impl_->face != nullptr ? static_cast<float>(impl_->face->ascender) : 0.0f;
}
float Font::descender() const {
    return impl_->face != nullptr ? static_cast<float>(impl_->face->descender) : 0.0f;
}
float Font::line_gap() const {
    return impl_->face != nullptr ? static_cast<float>(impl_->face->height) : 0.0f;
}

bool Font::LoadGlyph(std::uint32_t codepoint, std::uint32_t pixel_height) {
    if (impl_->face == nullptr) {
        return false;
    }
    if (FT_Set_Pixel_Sizes(impl_->face, 0, static_cast<FT_UInt>(pixel_height)) != 0) {
        return false;
    }
    const FT_UInt glyph_index = FT_Get_Char_Index(impl_->face, static_cast<FT_ULong>(codepoint));
    if (glyph_index == 0) {
        impl_->glyph_loaded = false;
        return false;
    }
    if (FT_Load_Glyph(impl_->face, glyph_index, FT_LOAD_DEFAULT) != 0) {
        impl_->glyph_loaded = false;
        return false;
    }
    impl_->glyph_loaded = true;
    return true;
}

int Font::glyph_width() const {
    return impl_->glyph_loaded && impl_->slot != nullptr ? impl_->slot->bitmap.width : 0;
}
int Font::glyph_height() const {
    return impl_->glyph_loaded && impl_->slot != nullptr ? impl_->slot->bitmap.rows : 0;
}
int Font::glyph_bearing_x() const {
    return impl_->glyph_loaded && impl_->slot != nullptr ? impl_->slot->bitmap_left : 0;
}
int Font::glyph_bearing_y() const {
    return impl_->glyph_loaded && impl_->slot != nullptr ? impl_->slot->bitmap_top : 0;
}
std::int64_t Font::glyph_advance_x() const {
    return impl_->glyph_loaded && impl_->slot != nullptr ? impl_->slot->advance.x : 0;
}

void* Font::native_face() const {
    return impl_->face;
}

// --- TextShaper (HarfBuzz) ---------------------------------------------------

struct TextShaper::Impl {
    hb_buffer_t* buffer = nullptr;
    Impl() : buffer(hb_buffer_create()) {}
    ~Impl() { hb_buffer_destroy(buffer); }
};

TextShaper::TextShaper() : impl_(std::make_unique<Impl>()) {}
TextShaper::~TextShaper() = default;
TextShaper::TextShaper(TextShaper&&) noexcept = default;
TextShaper& TextShaper::operator=(TextShaper&&) noexcept = default;

TextRun TextShaper::Shape(const Font& font, const std::string& text, std::uint32_t pixel_height) {
    TextRun run;
    if (text.empty()) {
        return run;
    }

    FT_Face face = static_cast<FT_Face>(font.native_face());
    if (face == nullptr) {
        return run;
    }
    if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixel_height)) != 0) {
        return run;
    }

    hb_font_t* hb_font = hb_ft_font_create_referenced(face);
    if (hb_font == nullptr) {
        return run;
    }

    hb_buffer_t* buffer = impl_->buffer;
    hb_buffer_reset(buffer);
    hb_buffer_add_utf8(buffer, text.c_str(), static_cast<int>(text.size()), 0,
                       static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(hb_font, buffer, nullptr, 0);

    const unsigned count = hb_buffer_get_length(buffer);
    run.glyphs.reserve(count);
    const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer, nullptr);
    const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buffer, nullptr);
    for (unsigned i = 0; i < count; ++i) {
        ShapedGlyph glyph;
        glyph.glyph_id = info[i].codepoint;
        glyph.cluster = info[i].cluster;
        glyph.advance_x = static_cast<float>(pos[i].x_advance) / 64.0f;
        glyph.offset_x = static_cast<float>(pos[i].x_offset) / 64.0f;
        glyph.offset_y = static_cast<float>(pos[i].y_offset) / 64.0f;
        run.width += glyph.advance_x;
        run.glyphs.push_back(glyph);
    }

    hb_font_destroy(hb_font);
    return run;
}

// --- LineBreaker (CJK kinsoku) ------------------------------------------------

TextLayout LineBreaker::Break(const TextRun& run, const std::string& source_utf8,
                              float max_line_width, float line_height, float baseline) {
    TextLayout layout;
    if (run.glyphs.empty()) {
        return layout;
    }

    std::size_t line_start = 0;
    while (line_start < run.glyphs.size()) {
        // Greedy fill: extend the line until the next glyph would overflow.
        std::size_t line_end = line_start;
        float line_width = 0.0f;
        while (line_end < run.glyphs.size()) {
            const float next_width = line_width + run.glyphs[line_end].advance_x;
            if (line_width > 0.0f && next_width > max_line_width) {
                break;
            }
            line_width = next_width;
            ++line_end;
        }
        if (line_end == line_start) {
            // Single glyph wider than the line: force it onto its own line
            // (no infinite loop).
            ++line_end;
            line_width = run.glyphs[line_start].advance_x;
        }

        // Trailing-prohibition: a line must not END with an opening bracket.
        // Step the break back over such glyphs (they carry to the next line).
        while (line_end > line_start + 1 && IsTrailingProhibited(CodepointAtCluster(
                                                source_utf8, run.glyphs[line_end - 1].cluster))) {
            line_width -= run.glyphs[line_end - 1].advance_x;
            --line_end;
        }

        // Leading-prohibition: a line must not START with a closing/hanging
        // mark. Hang it onto the current line (tolerate the overflow).
        while (line_end < run.glyphs.size() &&
               IsLeadingProhibited(CodepointAtCluster(source_utf8, run.glyphs[line_end].cluster))) {
            line_width += run.glyphs[line_end].advance_x;
            ++line_end;
        }

        TextLine line;
        line.first_glyph = line_start;
        line.glyph_count = line_end - line_start;
        line.width = line_width;
        line.height = line_height;
        line.baseline = baseline;
        layout.lines.push_back(line);
        layout.width = std::max(layout.width, line.width);
        layout.height += line_height;

        line_start = line_end;
    }
    return layout;
}

} // namespace jrpgmaker::ui