#include "jrpgmaker/editor/rmlui_input.hpp"
#include "jrpgmaker/editor/rmlui_editor_view.hpp"

#include <cmath>
#include <limits>

#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/TextInputContext.h>

namespace jrpgmaker::editor {

void RmlUiTextInputHandler::OnActivate(Rml::TextInputContext* input_context) {
    context_ = input_context;
    composition_start_ = 0;
    composition_end_ = 0;
}

void RmlUiTextInputHandler::OnDeactivate(Rml::TextInputContext* input_context) {
    if (context_ != input_context)
        return;
    context_ = nullptr;
    composition_start_ = 0;
    composition_end_ = 0;
}

void RmlUiTextInputHandler::OnDestroy(Rml::TextInputContext* input_context) {
    OnDeactivate(input_context);
}

void RmlUiTextInputHandler::HandleEdit(std::string_view text, int selection_start,
                                       int selection_length) {
    if (context_ == nullptr)
        return;

    const Rml::String composition(text.begin(), text.end());
    const int composition_length = static_cast<int>(Rml::StringUtilities::LengthUTF8(composition));
    const bool composing = composition_start_ != composition_end_;
    if (!composing)
        context_->GetSelectionRange(composition_start_, composition_end_);

    if (composing || composition_length > 0)
        context_->SetText(composition, composition_start_, composition_end_);

    composition_end_ = composition_start_ + composition_length;
    context_->SetCompositionRange(composition_start_, composition_end_);

    if (composition_length > 0 && selection_start >= 0 && selection_length >= 0) {
        context_->SetSelectionRange(composition_start_ + selection_start,
                                    composition_start_ + selection_start + selection_length);
    } else if (composing) {
        context_->SetCursorPosition(composition_end_);
    }

    // SDL sends an empty editing event when the active composition is being
    // committed; the following text-input event carries the committed text.
    if (composing && composition_length == 0)
        context_->CommitComposition(Rml::StringView());
}

RmlUiInputPoint ToRmlUiInputPoint(float x, float y, float ratio) {
    if (!(ratio > 0.0f) || !std::isfinite(ratio))
        ratio = 1.0f;
    const auto to_pixel = [ratio](float value) {
        const double scaled = static_cast<double>(value) * static_cast<double>(ratio);
        if (!std::isfinite(scaled))
            return 0;
        if (scaled <= static_cast<double>(std::numeric_limits<int>::min()))
            return std::numeric_limits<int>::min();
        if (scaled >= static_cast<double>(std::numeric_limits<int>::max()))
            return std::numeric_limits<int>::max();
        return static_cast<int>(std::lround(scaled));
    };
    return {to_pixel(x), to_pixel(y)};
}

} // namespace jrpgmaker::editor
