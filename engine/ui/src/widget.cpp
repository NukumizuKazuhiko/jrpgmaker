#include "jrpgmaker/ui/widget.hpp"

#include <algorithm>

namespace jrpgmaker::ui {

std::array<Rect, 9> SliceNine(const Rect& outer, const NineSlice& slice) {
    // Clamp the slice insets so the center region never gets a negative extent:
    // when the target is smaller than the two insets on an axis, the center
    // collapses to zero and the side regions share the remaining space.
    const float left = std::min(slice.left, outer.width);
    const float right = std::min(slice.right, outer.width - left);
    const float top = std::min(slice.top, outer.height);
    const float bottom = std::min(slice.bottom, outer.height - top);

    const float center_width = outer.width - left - right;
    const float center_height = outer.height - top - bottom;
    const float center_x = outer.x + left;
    const float center_y = outer.y + top;
    const float right_x = center_x + center_width;
    const float bottom_y = center_y + center_height;

    // Row-major layout matching a 3x3 source texture:
    //   0 top-left, 1 top-center, 2 top-right
    //   3 middle-left, 4 middle-center, 5 middle-right
    //   6 bottom-left, 7 bottom-center, 8 bottom-right
    std::array<Rect, 9> regions{};
    regions[0] = Rect{outer.x, outer.y, left, top};
    regions[1] = Rect{center_x, outer.y, center_width, top};
    regions[2] = Rect{right_x, outer.y, right, top};
    regions[3] = Rect{outer.x, center_y, left, center_height};
    regions[4] = Rect{center_x, center_y, center_width, center_height};
    regions[5] = Rect{right_x, center_y, right, center_height};
    regions[6] = Rect{outer.x, bottom_y, left, bottom};
    regions[7] = Rect{center_x, bottom_y, center_width, bottom};
    regions[8] = Rect{right_x, bottom_y, right, bottom};
    return regions;
}

void Widget::AddChild(std::unique_ptr<Widget> child) {
    if (child == nullptr) {
        return;
    }
    child->parent_ = this;
    children_.push_back(std::move(child));
}

Widget::Widget(std::uint32_t id) : id_(id) {}

Panel::Panel(std::uint32_t id, NineSlice slice, Padding padding)
    : Widget(id), slice_(slice), padding_(padding) {}

Rect Panel::Layout(const Rect& available) {
    set_rect(available);
    content_ = Rect{available.x + padding_.left, available.y + padding_.top,
                    available.width - padding_.left - padding_.right,
                    available.height - padding_.top - padding_.bottom};
    for (const auto& child : children()) {
        if (child->visible()) {
            child->Layout(content_);
        }
    }
    return available;
}

TextBlock::TextBlock(std::uint32_t id, const Font* font, std::uint32_t pixel_height)
    : Widget(id), font_(font), pixel_height_(pixel_height) {}

void TextBlock::SetText(std::string text) {
    text_ = std::move(text);
}

Rect TextBlock::Layout(const Rect& available) {
    if (font_ == nullptr || text_.empty()) {
        const Rect empty{available.x, available.y, 0.0f, 0.0f};
        set_rect(empty);
        return empty;
    }

    const float scale =
        static_cast<float>(pixel_height_) / static_cast<float>(font_->units_per_em());
    const float line_height = (font_->ascender() - font_->descender() + font_->line_gap()) * scale;
    const float baseline = font_->ascender() * scale;

    const TextRun run = shaper_.Shape(*font_, text_, pixel_height_);
    layout_ = breaker_.Break(run, text_, available.width, line_height, baseline);

    const Rect result{available.x, available.y, available.width, layout_.height};
    set_rect(result);
    return result;
}

List::List(std::uint32_t id, float spacing) : Widget(id), spacing_(spacing) {}

Rect List::Layout(const Rect& available) {
    float cursor_y = available.y;
    for (const auto& child : children()) {
        if (!child->visible()) {
            continue;
        }
        const Rect child_area{available.x, cursor_y, available.width, 0.0f};
        const Rect occupied = child->Layout(child_area);
        cursor_y += occupied.height + spacing_;
    }

    const float total_height = cursor_y - available.y - spacing_;
    const Rect result{available.x, available.y, available.width,
                      total_height > 0.0f ? total_height : 0.0f};
    set_rect(result);
    return result;
}

} // namespace jrpgmaker::ui