#include "jrpgmaker/ui/dialog.hpp"

#include <utility>

namespace jrpgmaker::ui {
namespace {

const std::string* FindText(const domain::LocalizationTable& localization, const std::string& key) {
    const auto found = localization.entries.find(key);
    return found == localization.entries.end() ? nullptr : &found->second;
}

} // namespace

DialogPresentationResult DialogPresentation::Show(const domain::DialogRequested& request,
                                                  const domain::LocalizationTable& localization) {
    const std::string* text = FindText(localization, request.text_key);
    if (text == nullptr) {
        return {.ok = false,
                .error = "dialog text key is missing from localization: " + request.text_key};
    }

    DialogPresentationSnapshot next{.visible = true,
                                    .speaker = request.speaker,
                                    .text = *text,
                                    .options = {},
                                    .selected_option = 0};
    next.options.reserve(request.options.size());
    for (const auto& option : request.options) {
        const std::string* option_text = FindText(localization, option.text_key);
        if (option_text == nullptr) {
            return {.ok = false,
                    .error = "dialog option key is missing from localization: " + option.text_key};
        }
        next.options.push_back(*option_text);
    }
    snapshot_ = std::move(next);
    return {.ok = true, .error = {}};
}

void DialogPresentation::Hide() {
    snapshot_ = {};
}

void DialogPresentation::SelectPrevious() {
    if (snapshot_.options.empty()) {
        return;
    }
    snapshot_.selected_option = snapshot_.selected_option == 0 ? snapshot_.options.size() - 1
                                                               : snapshot_.selected_option - 1;
}

void DialogPresentation::SelectNext() {
    if (snapshot_.options.empty()) {
        return;
    }
    snapshot_.selected_option = (snapshot_.selected_option + 1) % snapshot_.options.size();
}

std::optional<std::size_t> DialogPresentation::selected_option() const {
    if (!snapshot_.visible || snapshot_.options.empty()) {
        return std::nullopt;
    }
    return snapshot_.selected_option;
}

DialogPresentationResult
InteractionPromptPresentation::Show(std::string_view text_key,
                                    const domain::LocalizationTable& localization) {
    const auto found = localization.entries.find(std::string(text_key));
    if (found == localization.entries.end()) {
        return {.ok = false,
                .error = "interaction prompt key is missing from localization: " +
                         std::string(text_key)};
    }
    text_ = found->second;
    return {.ok = true, .error = {}};
}

} // namespace jrpgmaker::ui
