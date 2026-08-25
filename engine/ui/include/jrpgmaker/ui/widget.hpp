#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "jrpgmaker/ui/text.hpp"

namespace jrpgmaker::ui {

// Axis-aligned rectangle in widget-local pixels. The origin is the top-left;
// the widget coordinate system is screen-space (NDC conversion is a render
// concern).
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// Distance from each edge of the outer rect that the slice lines sit at.
struct NineSlice {
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
};

// Inside padding of a panel (the content area the children are laid out into).
struct Padding {
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
};

// Splits `outer` into the nine regions of a 9-slice (row-major: corners 0/2/6/8
// keep their source size, edges 1/3/5/7 stretch along one axis, center 4 fills).
// Pure geometry; the render layer later maps these onto a 3x3 source texture.
std::array<Rect, 9> SliceNine(const Rect& outer, const NineSlice& slice);

// Retained widget tree (pure CPU; the render layer consumes the laid-out rects
// in a later pass). A widget owns its children; `Layout` walks the tree and
// stores the resulting rect in widget-local coordinates (top-left origin).
class Widget {
public:
    explicit Widget(std::uint32_t id);
    virtual ~Widget() = default;

    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    std::uint32_t id() const { return id_; }
    bool visible() const { return visible_; }
    void set_visible(bool visible) { visible_ = visible; }

    const Widget* parent() const { return parent_; }
    void AddChild(std::unique_ptr<Widget> child);

    const std::vector<std::unique_ptr<Widget>>& children() const { return children_; }

    // The rect this widget occupied after the last Layout pass (widget-local).
    const Rect& rect() const { return rect_; }

    // Lays this widget out inside `available` (top-left origin), sets `rect_`,
    // lays out children, and returns the size this widget actually occupies.
    virtual Rect Layout(const Rect& available) = 0;

protected:
    void set_rect(Rect rect) { rect_ = rect; }

private:
    std::uint32_t id_;
    bool visible_ = true;
    Widget* parent_ = nullptr;
    std::vector<std::unique_ptr<Widget>> children_;
    Rect rect_{};
};

// A 9-slice background panel. The background geometry (slice lines) is stored
// for the render pass; layout sizes the widget to `available` and lays children
// into the padded content area (top-left origin inside the widget).
class Panel final : public Widget {
public:
    Panel(std::uint32_t id, NineSlice slice, Padding padding);
    Rect Layout(const Rect& available) override;

    const NineSlice& slice() const { return slice_; }
    const Padding& padding() const { return padding_; }
    // The content rect of the last Layout pass (widget-local, inside padding).
    const Rect& content_rect() const { return content_; }

private:
    NineSlice slice_;
    Padding padding_;
    Rect content_{};
};

// A static text block: shapes + breaks the text with the CJK-capable layout
// engine (TextShaper/LineBreaker) at `pixel_height`, sized to the available
// width. The font is borrowed (not owned) and must outlive the block.
class TextBlock final : public Widget {
public:
    TextBlock(std::uint32_t id, const Font* font, std::uint32_t pixel_height);
    void SetText(std::string text);
    const std::string& text() const { return text_; }
    const TextLayout& text_layout() const { return layout_; }
    Rect Layout(const Rect& available) override;

private:
    const Font* font_;
    std::uint32_t pixel_height_;
    std::string text_;
    TextShaper shaper_;
    LineBreaker breaker_;
    TextLayout layout_{};
};

// A vertical list: lays each child into the full available width at its own
// natural height, stacked with `spacing` between items. The list occupies the
// available width and the total stacked height.
class List final : public Widget {
public:
    List(std::uint32_t id, float spacing);
    Rect Layout(const Rect& available) override;

    float spacing() const { return spacing_; }

private:
    float spacing_;
};

} // namespace jrpgmaker::ui