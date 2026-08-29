#include "jrpgmaker/ui/glyph_atlas.hpp"

#include <algorithm>
#include <stdexcept>

namespace jrpgmaker::ui {

namespace {

std::size_t CheckedPixelCount(std::uint32_t width, std::uint32_t height, std::size_t max_glyphs) {
    if (width == 0 || height == 0 || max_glyphs == 0 || width > 4096 || height > 4096)
        throw std::invalid_argument("ui glyph atlas dimensions and capacity are out of range");
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

} // namespace

GlyphAtlas::GlyphAtlas(std::uint32_t width, std::uint32_t height, std::size_t max_glyphs)
    : width_(width), height_(height), max_glyphs_(max_glyphs),
      pixels_(CheckedPixelCount(width, height, max_glyphs), 0) {}

std::size_t GlyphAtlas::KeyHash::operator()(const Key& key) const {
    const auto first = std::hash<std::uint32_t>{}(key.codepoint);
    const auto second = std::hash<std::uint32_t>{}(key.pixel_height);
    return first ^ (second + static_cast<std::size_t>(0x9e3779b9u) + (first << 6u) + (first >> 2u));
}

std::optional<GlyphAtlasEntry> GlyphAtlas::Add(Font& font, std::uint32_t codepoint,
                                               std::uint32_t pixel_height) {
    const Key key{codepoint, pixel_height};
    if (const auto found = entries_.find(key); found != entries_.end())
        return found->second;
    if (entries_.size() >= max_glyphs_ || pixel_height == 0 ||
        !font.LoadGlyph(codepoint, pixel_height))
        return std::nullopt;

    const auto glyph_width = static_cast<std::uint32_t>(font.glyph_width());
    const auto glyph_height = static_cast<std::uint32_t>(font.glyph_height());
    if (glyph_width == 0 || glyph_height == 0 || glyph_width + kPadding > width_ ||
        glyph_height + kPadding > height_)
        return std::nullopt;
    if (cursor_x_ + glyph_width + kPadding > width_) {
        cursor_x_ = 0;
        cursor_y_ += row_height_ + kPadding;
        row_height_ = 0;
    }
    if (cursor_y_ + glyph_height + kPadding > height_)
        return std::nullopt;

    const auto pitch =
        static_cast<std::size_t>(font.glyph_pitch() < 0 ? -font.glyph_pitch() : font.glyph_pitch());
    if (pitch < glyph_width || font.glyph_bitmap().size() < pitch * glyph_height)
        return std::nullopt;
    for (std::uint32_t row = 0; row < glyph_height; ++row) {
        const auto source = font.glyph_bitmap().data() + static_cast<std::size_t>(row) * pitch;
        auto destination =
            pixels_.data() + (static_cast<std::size_t>(cursor_y_ + row) * width_ + cursor_x_);
        std::copy_n(source, glyph_width, destination);
    }

    const GlyphAtlasEntry entry{
        codepoint,
        pixel_height,
        cursor_x_,
        cursor_y_,
        glyph_width,
        glyph_height,
        font.glyph_bearing_x(),
        font.glyph_bearing_y(),
        static_cast<float>(font.glyph_advance_x()) / 64.0f,
        static_cast<float>(cursor_x_) / static_cast<float>(width_),
        static_cast<float>(cursor_y_) / static_cast<float>(height_),
        static_cast<float>(cursor_x_ + glyph_width) / static_cast<float>(width_),
        static_cast<float>(cursor_y_ + glyph_height) / static_cast<float>(height_)};
    entries_.emplace(key, entry);
    cursor_x_ += glyph_width + kPadding;
    row_height_ = std::max(row_height_, glyph_height);
    return entry;
}

std::optional<GlyphAtlasEntry> GlyphAtlas::Find(std::uint32_t codepoint,
                                                std::uint32_t pixel_height) const {
    const auto found = entries_.find(Key{codepoint, pixel_height});
    if (found == entries_.end())
        return std::nullopt;
    return found->second;
}

} // namespace jrpgmaker::ui
