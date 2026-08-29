#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace jrpgmaker::ui {

enum class UiEventType : std::uint8_t {
    kPointerMove,
    kPointerDown,
    kPointerUp,
    kKeyDown,
    kTextInput,
    kTextComposition,
    kFocusNext,
    kFocusPrevious,
    kCancel,
    kConfirm,
};

struct UiEvent {
    UiEventType type = UiEventType::kPointerMove;
    std::uint64_t widget_id = 0;
    std::string text;
    float x = 0.0f;
    float y = 0.0f;
};

enum class UiCommandType : std::uint8_t {
    kActivate,
    kTextChanged,
    kCommitted,
    kCancelled,
};

struct UiCommand {
    UiCommandType type = UiCommandType::kActivate;
    std::uint64_t widget_id = 0;
    std::string text;
};

class UiContext final {
public:
    static constexpr std::size_t kMaxFocusable = 1024;

    [[nodiscard]] bool RegisterFocusable(std::uint64_t widget_id, std::size_t tab_index);
    void ClearFocusables();
    [[nodiscard]] bool SetFocus(std::uint64_t widget_id);
    [[nodiscard]] bool MoveFocus(int direction);
    [[nodiscard]] std::uint64_t focused_widget() const { return focused_widget_; }
    [[nodiscard]] bool IsFocusable(std::uint64_t widget_id) const;

private:
    struct FocusEntry {
        std::uint64_t widget_id = 0;
        std::size_t tab_index = 0;
    };
    std::vector<FocusEntry> focusables_;
    std::uint64_t focused_widget_ = 0;
};

class TextFieldState final {
public:
    static constexpr std::size_t kMaxBytes = 64u * 1024u;

    [[nodiscard]] bool Apply(const UiEvent& event, std::vector<UiCommand>& commands,
                             std::uint64_t widget_id);
    [[nodiscard]] const std::string& text() const { return text_; }
    [[nodiscard]] std::size_t caret() const { return caret_; }
    [[nodiscard]] std::size_t selection_start() const { return selection_start_; }
    [[nodiscard]] std::size_t selection_end() const { return selection_end_; }
    [[nodiscard]] bool composing() const { return composing_; }
    void SetText(std::string value);
    void SelectAll();

private:
    void Emit(UiCommandType type, std::vector<UiCommand>& commands, std::uint64_t widget_id,
              std::string value = {}) const;
    std::string text_;
    std::size_t caret_ = 0;
    std::size_t selection_start_ = 0;
    std::size_t selection_end_ = 0;
    bool composing_ = false;
};

} // namespace jrpgmaker::ui
