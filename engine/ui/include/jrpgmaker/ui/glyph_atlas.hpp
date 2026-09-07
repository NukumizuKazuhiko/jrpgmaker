#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "jrpgmaker/ui/text.hpp"

namespace jrpgmaker::ui {

struct GlyphAtlasEntry {
    std::uint32_t codepoint = 0;
    std::uint32_t pixel_height = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    int bearing_x = 0;
    int bearing_y = 0;
    float advance_x = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

// Deterministic, bounded grayscale atlas for UI glyphs. The atlas owns only
// CPU pixels; the render layer owns the eventual sampled GPU texture.
class GlyphAtlas final {
public:
    static constexpr std::uint32_t kPadding = 1;

    GlyphAtlas(std::uint32_t width, std::uint32_t height, std::size_t max_glyphs);

    [[nodiscard]] std::optional<GlyphAtlasEntry> Add(Font& font, std::uint32_t codepoint,
                                                     std::uint32_t pixel_height);
    [[nodiscard]] std::uint32_t width() const { return width_; }
    [[nodiscard]] std::uint32_t height() const { return height_; }
    [[nodiscard]] const std::vector<std::uint8_t>& pixels() const { return pixels_; }
    [[nodiscard]] std::size_t size() const { return entries_.size(); }

    [[nodiscard]] std::optional<GlyphAtlasEntry> Find(std::uint32_t codepoint,
                                                      std::uint32_t pixel_height) const;

private:
    struct Key {
        std::uint32_t codepoint = 0;
        std::uint32_t pixel_height = 0;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const;
    };

    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::size_t max_glyphs_ = 0;
    std::uint32_t cursor_x_ = 0;
    std::uint32_t cursor_y_ = 0;
    std::uint32_t row_height_ = 0;
    std::vector<std::uint8_t> pixels_;
    std::unordered_map<Key, GlyphAtlasEntry, KeyHash> entries_;
};

} // namespace jrpgmaker::ui
