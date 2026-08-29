#include "jrpgmaker/ui/interaction.hpp"

#include <algorithm>
#include <utility>

namespace jrpgmaker::ui {
namespace {

bool IsContinuation(unsigned char value) {
    return (value & 0xC0u) == 0x80u;
}

std::size_t PreviousCodepoint(const std::string& value, std::size_t offset) {
    if (offset == 0)
        return 0;
    --offset;
    while (offset > 0 && IsContinuation(static_cast<unsigned char>(value[offset])))
        --offset;
    return offset;
}

std::size_t NextCodepoint(const std::string& value, std::size_t offset) {
    if (offset >= value.size())
        return value.size();
    ++offset;
    while (offset < value.size() && IsContinuation(static_cast<unsigned char>(value[offset])))
        ++offset;
    return offset;
}

} // namespace

bool UiContext::RegisterFocusable(std::uint64_t widget_id, std::size_t tab_index) {
    if (widget_id == 0 || focusables_.size() >= kMaxFocusable || IsFocusable(widget_id))
        return false;
    const auto previous_first = focusables_.empty() ? 0 : focusables_.front().widget_id;
    const auto previous_focus = focused_widget_;
    focusables_.push_back({widget_id, tab_index});
    std::stable_sort(focusables_.begin(), focusables_.end(),
                     [](const FocusEntry& left, const FocusEntry& right) {
                         if (left.tab_index != right.tab_index)
                             return left.tab_index < right.tab_index;
                         return left.widget_id < right.widget_id;
                     });
    if (previous_focus == 0 || previous_focus == previous_first)
        focused_widget_ = focusables_.front().widget_id;
    return true;
}

void UiContext::ClearFocusables() {
    focusables_.clear();
    focused_widget_ = 0;
}

bool UiContext::SetFocus(std::uint64_t widget_id) {
    if (!IsFocusable(widget_id))
        return false;
    focused_widget_ = widget_id;
    return true;
}

bool UiContext::MoveFocus(int direction) {
    if (focusables_.empty() || direction == 0)
        return false;
    const auto current =
        std::find_if(focusables_.begin(), focusables_.end(), [this](const FocusEntry& entry) {
            return entry.widget_id == focused_widget_;
        });
    std::size_t index =
        current == focusables_.end() ? 0 : static_cast<std::size_t>(current - focusables_.begin());
    const auto count = focusables_.size();
    index = direction > 0 ? (index + 1) % count : (index + count - 1) % count;
    focused_widget_ = focusables_[index].widget_id;
    return true;
}

bool UiContext::IsFocusable(std::uint64_t widget_id) const {
    return std::any_of(
        focusables_.begin(), focusables_.end(),
        [widget_id](const FocusEntry& entry) { return entry.widget_id == widget_id; });
}

void TextFieldState::Emit(UiCommandType type, std::vector<UiCommand>& commands,
                          std::uint64_t widget_id, std::string value) const {
    commands.push_back({type, widget_id, std::move(value)});
}

void TextFieldState::SetText(std::string value) {
    if (value.size() > kMaxBytes)
        value.resize(kMaxBytes);
    text_ = std::move(value);
    caret_ = text_.size();
    selection_start_ = caret_;
    selection_end_ = caret_;
    composing_ = false;
}

void TextFieldState::SelectAll() {
    selection_start_ = 0;
    selection_end_ = text_.size();
    caret_ = selection_end_;
}

bool TextFieldState::Apply(const UiEvent& event, std::vector<UiCommand>& commands,
                           std::uint64_t widget_id) {
    switch (event.type) {
    case UiEventType::kTextInput:
        if (event.text.empty() || text_.size() > kMaxBytes - event.text.size())
            return false;
        if (selection_start_ != selection_end_) {
            text_.erase(selection_start_, selection_end_ - selection_start_);
            caret_ = selection_start_;
        }
        text_.insert(caret_, event.text);
        caret_ += event.text.size();
        selection_start_ = caret_;
        selection_end_ = caret_;
        Emit(UiCommandType::kTextChanged, commands, widget_id, text_);
        return true;
    case UiEventType::kTextComposition:
        composing_ = !event.text.empty();
        return true;
    case UiEventType::kKeyDown:
        if (event.text == "Left") {
            caret_ = selection_start_ != selection_end_ ? selection_start_
                                                        : PreviousCodepoint(text_, caret_);
            selection_start_ = caret_;
            selection_end_ = caret_;
            return true;
        }
        if (event.text == "Right") {
            caret_ =
                selection_start_ != selection_end_ ? selection_end_ : NextCodepoint(text_, caret_);
            selection_start_ = caret_;
            selection_end_ = caret_;
            return true;
        }
        if (event.text == "Backspace" && selection_start_ != selection_end_) {
            text_.erase(selection_start_, selection_end_ - selection_start_);
            caret_ = selection_start_;
            selection_end_ = caret_;
            Emit(UiCommandType::kTextChanged, commands, widget_id, text_);
            return true;
        }
        if (event.text == "Backspace" && caret_ > 0) {
            const auto previous = PreviousCodepoint(text_, caret_);
            text_.erase(previous, caret_ - previous);
            caret_ = previous;
            selection_start_ = caret_;
            selection_end_ = caret_;
            Emit(UiCommandType::kTextChanged, commands, widget_id, text_);
            return true;
        }
        return false;
    case UiEventType::kConfirm:
        Emit(UiCommandType::kCommitted, commands, widget_id, text_);
        return true;
    case UiEventType::kCancel:
        Emit(UiCommandType::kCancelled, commands, widget_id);
        return true;
    default:
        return false;
    }
}

} // namespace jrpgmaker::ui
