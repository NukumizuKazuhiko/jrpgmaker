#include "jrpgmaker/ui/text_draw.hpp"

#include <algorithm>
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
    return BuildTextDrawList(source, locale, font, {}, atlas, pixel_height);
}

TextDrawResult BuildTextDrawList(const DrawList& source, const EditorLocale& locale, Font& font,
                                 const std::vector<Font*>& fallback_fonts, GlyphAtlas& atlas,
                                 std::uint32_t pixel_height) {
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
            const auto decoration = text->edit;
            const auto selection_start = decoration
                                             ? std::min(decoration->selection_start, value.size())
                                             : 0;
            const auto selection_end = decoration
                                           ? std::min(decoration->selection_end, value.size())
                                           : 0;
            float selection_left = pen_x;
            float selection_right = pen_x;
            float caret_x = pen_x;
            std::size_t offset = 0;
            while (offset < value.size()) {
                std::uint32_t codepoint = 0;
                const auto before = offset;
                if (!DecodeUtf8(value, offset, codepoint)) {
                    result.diagnostics.push_back({"ui.text.utf8_invalid", primitive_index});
                    break;
                }
                if (decoration && before == selection_start)
                    selection_left = pen_x;
                if (decoration && before == selection_end)
                    selection_right = pen_x;
                if (decoration && before == decoration->caret)
                    caret_x = pen_x;
                Font* glyph_font = nullptr;
                if (font.LoadGlyph(codepoint, pixel_height))
                    glyph_font = &font;
                else {
                    for (auto* fallback : fallback_fonts) {
                        if (fallback != nullptr && fallback != &font &&
                            fallback->LoadGlyph(codepoint, pixel_height)) {
                            glyph_font = fallback;
                            break;
                        }
                    }
                }
                if (glyph_font == nullptr) {
                    result.diagnostics.push_back({"ui.text.glyph_missing", primitive_index});
                    continue;
                }
                const float advance = static_cast<float>(glyph_font->glyph_advance_x()) / 64.0f;
                if (glyph_font->glyph_width() > 0 && glyph_font->glyph_height() > 0) {
                    const auto entry = atlas.Add(*glyph_font, codepoint, pixel_height);
                    if (!entry) {
                        result.diagnostics.push_back({"ui.text.atlas_full", primitive_index});
                    } else {
                        const Rect glyph_rect{pen_x + static_cast<float>(entry->bearing_x),
                                              baseline - static_cast<float>(entry->bearing_y),
                                              static_cast<float>(entry->width),
                                              static_cast<float>(entry->height)};
                        const float left = std::max(glyph_rect.x, text->rect.x);
                        const float top = std::max(glyph_rect.y, text->rect.y);
                        const float right = std::min(glyph_rect.x + glyph_rect.width,
                                                     text->rect.x + text->rect.width);
                        const float bottom = std::min(glyph_rect.y + glyph_rect.height,
                                                      text->rect.y + text->rect.height);
                        if (right > left && bottom > top) {
                            const float u_scale = (entry->u1 - entry->u0) / glyph_rect.width;
                            const float v_scale = (entry->v1 - entry->v0) / glyph_rect.height;
                            (void) result.draw_list.Add(DrawGlyph{
                                {left, top, right - left, bottom - top},
                                {entry->u0 + (left - glyph_rect.x) * u_scale,
                                 entry->v0 + (top - glyph_rect.y) * v_scale,
                                 (right - left) * u_scale, (bottom - top) * v_scale},
                                {1.0f, 1.0f, 1.0f, 1.0f}});
                        }
                    }
                }
                pen_x += advance;
                if (offset == before)
                    break;
            }
            if (decoration) {
                if (selection_start == value.size())
                    selection_left = pen_x;
                if (selection_end == value.size())
                    selection_right = pen_x;
                if (decoration->caret >= value.size())
                    caret_x = pen_x;
                if (selection_start != selection_end && selection_right > selection_left &&
                    !decoration->selection_recipe.empty())
                    (void) result.draw_list.Add(DrawRect{{selection_left, text->rect.y,
                                                           selection_right - selection_left,
                                                           text->rect.height},
                                                          decoration->selection_recipe, "normal"});
                if (!decoration->caret_recipe.empty())
                    (void) result.draw_list.Add(DrawRect{{caret_x, text->rect.y, 1.0f,
                                                           text->rect.height},
                                                          decoration->caret_recipe, "normal"});
            }
        } else {
            (void) result.draw_list.Add(primitive);
        }
    }
    return result;
}

} // namespace jrpgmaker::ui
