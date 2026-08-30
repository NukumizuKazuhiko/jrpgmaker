#include "jrpgmaker/ui/runtime_overlay.hpp"

#include <cmath>
#include <string>

namespace jrpgmaker::ui {
namespace {

constexpr float kHorizontalPadding = 24.0f;
constexpr float kBottomPadding = 24.0f;
constexpr float kLineSpacing = 4.0f;

bool AddText(RuntimeOverlayProjection& projection, Rect rect, std::string text) {
    if (text.empty())
        return true;
    return projection.draw_list.Add(
        DrawResolvedText{.rect = rect, .text = std::move(text), .edit = std::nullopt});
}

} // namespace

RuntimeOverlayProjection BuildRuntimeOverlayDrawList(std::string_view prompt_text,
                                                     const DialogPresentationSnapshot& dialog,
                                                     Rect viewport, float line_height) {
    RuntimeOverlayProjection projection;
    if (!std::isfinite(viewport.x) || !std::isfinite(viewport.y) ||
        !std::isfinite(viewport.width) || !std::isfinite(viewport.height) ||
        !std::isfinite(line_height) || viewport.width <= 2.0f * kHorizontalPadding ||
        viewport.height <= line_height + kBottomPadding || line_height <= 0.0f) {
        projection.diagnostics.push_back({"ui.runtime_overlay.layout_invalid"});
        return projection;
    }

    const float width = viewport.width - 2.0f * kHorizontalPadding;
    float y = viewport.y + viewport.height - kBottomPadding - line_height;
    if (!dialog.visible) {
        if (!prompt_text.empty() &&
            !AddText(projection, {viewport.x + kHorizontalPadding, y, width, line_height},
                     std::string(prompt_text))) {
            projection.diagnostics.push_back({"ui.runtime_overlay.primitive_limit"});
        }
        return projection;
    }

    const auto add_bottom_up = [&](std::string value) {
        const bool added = AddText(
            projection, {viewport.x + kHorizontalPadding, y, width, line_height}, std::move(value));
        y -= line_height + kLineSpacing;
        return added;
    };

    for (std::size_t index = dialog.options.size(); index > 0; --index) {
        const auto option_index = index - 1;
        const std::string prefix = option_index == dialog.selected_option ? "> " : "  ";
        if (!add_bottom_up(prefix + dialog.options[option_index])) {
            projection.diagnostics.push_back({"ui.runtime_overlay.primitive_limit"});
            return projection;
        }
    }
    if (!add_bottom_up(dialog.text)) {
        projection.diagnostics.push_back({"ui.runtime_overlay.primitive_limit"});
        return projection;
    }
    if (!dialog.speaker.empty() && !add_bottom_up(dialog.speaker + ":"))
        projection.diagnostics.push_back({"ui.runtime_overlay.primitive_limit"});
    return projection;
}

} // namespace jrpgmaker::ui
