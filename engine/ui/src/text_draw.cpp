#include "jrpgmaker/ui/text_draw.hpp"

#include <cstdint>

namespace jrpgmaker::ui {
namespace {

std::string ResolveText(const std::string& format,
                        const std::unordered_map<std::string, std::string>& arguments) {
    std::string result;
    result.reserve(format.size());
    for (std::size_t index = 0; index < format.size();) {
        if (format[index] != '{') {
            result.push_back(format[index++]);
            continue;
        }
        const auto end = format.find('}', index + 1);
        if (end == std::string::npos || end == index + 1) {
            result.push_back(format[index++]);
            continue;
        }
        const auto argument = arguments.find(format.substr(index + 1, end - index - 1));
        result += argument == arguments.end() ? format.substr(index, end - index + 1)
                                              : argument->second;
        index = end + 1;
    }
    return result;
}

bool DecodeUtf8(const std::string& text, std::size_t& offset, std::uint32_t& codepoint) {
    if (offset >= text.size())
        return false;
    const auto first = static_cast<std::uint8_t>(text[offset++]);
    if (first < 0x80u) {
        codepoint = first;
        return true;
    }
    const std::size_t count = first >= 0xF0u ? 3 : first >= 0xE0u ? 2 : 1;
    if (offset + count > text.size())
        return false;
    codepoint = first & (count == 3 ? 0x07u : count == 2 ? 0x0Fu : 0x1Fu);
    for (std::size_t index = 0; index < count; ++index) {
        const auto byte = static_cast<std::uint8_t>(text[offset++]);
        if ((byte & 0xC0u) != 0x80u)
            return false;
        codepoint = (codepoint << 6u) | (byte & 0x3Fu);
    }
    return codepoint <= 0x10FFFFu && !(codepoint >= 0xD800u && codepoint <= 0xDFFFu);
}

} // namespace

TextDrawResult BuildTextDrawList(const DrawList& source, const EditorLocale& locale, Font& font,
                                 GlyphAtlas& atlas, std::uint32_t pixel_height) {
    TextDrawResult result;
    if (pixel_height == 0) {
        result.diagnostics.push_back({"ui.text.pixel_height_invalid", 0});
        return result;
    }
    for (std::size_t primitive_index = 0; primitive_index < source.primitives().size();
         ++primitive_index) {
        const auto& primitive = source.primitives()[primitive_index];
        if (const auto* text = std::get_if<DrawText>(&primitive)) {
            const auto localized = locale.strings.find(text->text_key);
            if (localized == locale.strings.end()) {
                result.diagnostics.push_back({"ui.text.localization_missing", primitive_index});
                continue;
            }
            const auto value = ResolveText(localized->second, text->arguments);
            float pen_x = text->rect.x;
            const float baseline = text->rect.y + static_cast<float>(pixel_height);
            std::size_t offset = 0;
            while (offset < value.size()) {
                std::uint32_t codepoint = 0;
                const auto before = offset;
                if (!DecodeUtf8(value, offset, codepoint)) {
                    result.diagnostics.push_back({"ui.text.utf8_invalid", primitive_index});
                    break;
                }
                if (!font.LoadGlyph(codepoint, pixel_height)) {
                    result.diagnostics.push_back({"ui.text.glyph_missing", primitive_index});
                    continue;
                }
                const float advance = static_cast<float>(font.glyph_advance_x()) / 64.0f;
                if (font.glyph_width() > 0 && font.glyph_height() > 0) {
                    const auto entry = atlas.Add(font, codepoint, pixel_height);
                    if (!entry) {
                        result.diagnostics.push_back({"ui.text.atlas_full", primitive_index});
                    } else {
                        const Rect glyph_rect{pen_x + static_cast<float>(entry->bearing_x),
                                              baseline - static_cast<float>(entry->bearing_y),
                                              static_cast<float>(entry->width),
                                              static_cast<float>(entry->height)};
                        (void) result.draw_list.Add(
                            DrawGlyph{glyph_rect, {entry->u0, entry->v0, entry->u1 - entry->u0,
                                                   entry->v1 - entry->v0},
                                      {1.0f, 1.0f, 1.0f, 1.0f}});
                    }
                }
                pen_x += advance;
                if (offset == before)
                    break;
            }
        } else {
            (void) result.draw_list.Add(primitive);
        }
    }
    return result;
}

} // namespace jrpgmaker::ui
