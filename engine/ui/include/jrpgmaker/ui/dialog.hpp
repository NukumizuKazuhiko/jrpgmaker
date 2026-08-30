#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "jrpgmaker/domain/event_runner.hpp"
#include "jrpgmaker/domain/localization.hpp"

namespace jrpgmaker::ui {

struct DialogPresentationSnapshot {
    bool visible = false;
    std::string speaker;
    std::string text;
    std::vector<std::string> options;
    std::size_t selected_option = 0;
};

struct DialogPresentationResult {
    bool ok = false;
    std::string error;
    explicit operator bool() const { return ok; }
};

// Presentation-only state derived from domain dialog projections. The domain
// owns the branch semantics; this class only resolves localized strings and
// tracks which visible option the player has highlighted.
class DialogPresentation {
public:
    [[nodiscard]] DialogPresentationResult Show(const domain::DialogRequested& request,
                                                const domain::LocalizationTable& localization);
    void Hide();
    void SelectPrevious();
    void SelectNext();

    [[nodiscard]] const DialogPresentationSnapshot& snapshot() const { return snapshot_; }
    [[nodiscard]] std::optional<std::size_t> selected_option() const;

private:
    DialogPresentationSnapshot snapshot_;
};

class InteractionPromptPresentation final {
public:
    [[nodiscard]] DialogPresentationResult Show(std::string_view text_key,
                                                const domain::LocalizationTable& localization);
    void Hide() { text_.clear(); }
    [[nodiscard]] const std::string& text() const { return text_; }

private:
    std::string text_;
};

} // namespace jrpgmaker::ui
