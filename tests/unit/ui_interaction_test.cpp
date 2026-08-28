#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/ui/interaction.hpp"

TEST_CASE("ui context orders focus by tab index and wraps", "[ui][editor]") {
    jrpgmaker::ui::UiContext context;
    REQUIRE(context.RegisterFocusable(20, 2));
    REQUIRE(context.RegisterFocusable(10, 1));
    REQUIRE(context.focused_widget() == 10);
    REQUIRE(context.MoveFocus(1));
    REQUIRE(context.focused_widget() == 20);
    REQUIRE(context.MoveFocus(1));
    REQUIRE(context.focused_widget() == 10);
}

TEST_CASE("ui text field preserves UTF-8 codepoint edits and emits commands", "[ui][editor]") {
    jrpgmaker::ui::TextFieldState field;
    field.SetText("甲a");
    std::vector<jrpgmaker::ui::UiCommand> commands;
    REQUIRE(field.Apply({.type = jrpgmaker::ui::UiEventType::kKeyDown, .text = "Left"}, commands, 7));
    REQUIRE(field.Apply({.type = jrpgmaker::ui::UiEventType::kKeyDown, .text = "Backspace"}, commands, 7));
    REQUIRE(field.text() == "a");
    REQUIRE(commands.size() == 1);
    REQUIRE(commands.front().type == jrpgmaker::ui::UiCommandType::kTextChanged);
}
