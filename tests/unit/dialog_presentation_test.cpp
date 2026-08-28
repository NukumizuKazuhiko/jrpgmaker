#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/ui/dialog.hpp"

namespace {

jrpgmaker::domain::LocalizationTable Localization() {
    return {.schema = 1,
            .locale = "en",
            .entries = {{"dialog.prompt", "Choose"}, {"dialog.yes", "Yes"}, {"dialog.no", "No"}}};
}

} // namespace

TEST_CASE("dialog presentation resolves choices and wraps selection", "[ui][dialog]") {
    jrpgmaker::ui::DialogPresentation presentation;
    const jrpgmaker::domain::DialogRequested request{
        .event_id = "event",
        .speaker = "npc",
        .text_key = "dialog.prompt",
        .options = {{.text_key = "dialog.yes", .instructions = {}},
                    {.text_key = "dialog.no", .instructions = {}}},
    };

    REQUIRE(presentation.Show(request, Localization()));
    REQUIRE(presentation.snapshot().visible);
    REQUIRE(presentation.snapshot().text == "Choose");
    REQUIRE(presentation.snapshot().options == std::vector<std::string>{"Yes", "No"});
    REQUIRE(presentation.selected_option() == 0);

    presentation.SelectPrevious();
    REQUIRE(presentation.selected_option() == 1);
    presentation.SelectNext();
    REQUIRE(presentation.selected_option() == 0);
    presentation.Hide();
    REQUIRE_FALSE(presentation.snapshot().visible);
    REQUIRE_FALSE(presentation.selected_option().has_value());
}

TEST_CASE("dialog presentation rejects missing localization without replacing state",
          "[ui][dialog]") {
    jrpgmaker::ui::DialogPresentation presentation;
    const jrpgmaker::domain::DialogRequested valid{
        .event_id = "event", .speaker = "npc", .text_key = "dialog.prompt", .options = {}};
    REQUIRE(presentation.Show(valid, Localization()));

    auto invalid = valid;
    invalid.text_key = "missing";
    const auto result = presentation.Show(invalid, Localization());
    REQUIRE_FALSE(result);
    REQUIRE(result.error == "dialog text key is missing from localization: missing");
    REQUIRE(presentation.snapshot().text == "Choose");
}

TEST_CASE("dialog presentation preserves localized CJK text", "[ui][dialog][cjk]") {
    const jrpgmaker::domain::LocalizationTable localization{
        .schema = 1,
        .locale = "ja",
        .entries = {{"dialog.prompt", "手伝ってくれますか？"},
                    {"dialog.yes", "はい"},
                    {"dialog.no", "不用，谢谢"}},
    };
    const jrpgmaker::domain::DialogRequested request{
        .event_id = "event",
        .speaker = "alice",
        .text_key = "dialog.prompt",
        .options = {{.text_key = "dialog.yes", .instructions = {}},
                    {.text_key = "dialog.no", .instructions = {}}},
    };

    jrpgmaker::ui::DialogPresentation presentation;
    REQUIRE(presentation.Show(request, localization));
    REQUIRE(presentation.snapshot().text == "手伝ってくれますか？");
    REQUIRE(presentation.snapshot().options == std::vector<std::string>{"はい", "不用，谢谢"});
}
