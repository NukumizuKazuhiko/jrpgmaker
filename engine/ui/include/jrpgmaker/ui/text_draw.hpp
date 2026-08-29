#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "jrpgmaker/ui/draw_list.hpp"
#include "jrpgmaker/ui/editor_resources.hpp"
#include "jrpgmaker/ui/glyph_atlas.hpp"

namespace jrpgmaker::ui {

struct TextDrawDiagnostic {
    std::string code;
    std::size_t primitive_index = 0;
};

struct TextDrawResult {
    DrawList draw_list;
    std::vector<TextDrawDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const { return diagnostics.empty(); }
};

// Resolves localized DrawText keys into positioned atlas quads. The caller
// owns the font and atlas; this function only emits CPU draw primitives.
[[nodiscard]] TextDrawResult BuildTextDrawList(const DrawList& source,
                                               const EditorLocale& locale, Font& font,
                                               GlyphAtlas& atlas, std::uint32_t pixel_height);

// Uses `font` first, then the borrowed fallback fonts in order for codepoints
// missing from the primary face. The caller owns every font and must keep them
// alive for the duration of the call.
[[nodiscard]] TextDrawResult BuildTextDrawList(const DrawList& source,
                                               const EditorLocale& locale, Font& font,
                                               const std::vector<Font*>& fallback_fonts,
                                               GlyphAtlas& atlas, std::uint32_t pixel_height);

} // namespace jrpgmaker::ui
