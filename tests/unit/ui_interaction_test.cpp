#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/ui/interaction.hpp"
#include "jrpgmaker/ui/ui_tree.hpp"

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
    REQUIRE(
        field.Apply({.type = jrpgmaker::ui::UiEventType::kKeyDown, .text = "Left"}, commands, 7));
    REQUIRE(field.Apply({.type = jrpgmaker::ui::UiEventType::kKeyDown, .text = "Backspace"},
                        commands, 7));
    REQUIRE(field.text() == "a");
    REQUIRE(commands.size() == 1);
    REQUIRE(commands.front().type == jrpgmaker::ui::UiCommandType::kTextChanged);
}

TEST_CASE("ui text field replaces an initial selection and edits through key events",
          "[ui][editor]") {
    jrpgmaker::ui::TextFieldState field;
    field.SetText("旧值");
    field.SelectAll();
    std::vector<jrpgmaker::ui::UiCommand> commands;
    REQUIRE(
        field.Apply({.type = jrpgmaker::ui::UiEventType::kTextInput, .text = "新值"}, commands, 3));
    REQUIRE(field.text() == "新值");
    REQUIRE(
        field.Apply({.type = jrpgmaker::ui::UiEventType::kKeyDown, .text = "Left"}, commands, 3));
    REQUIRE(field.Apply({.type = jrpgmaker::ui::UiEventType::kKeyDown, .text = "Backspace"},
                        commands, 3));
    REQUIRE(field.text() == "值");
}

TEST_CASE("ui tree lays out, hit tests, focuses, and emits commands", "[ui][editor][framework]") {
    jrpgmaker::ui::UiTree tree;
    REQUIRE(tree.SetRoot({1,
                          "editor.root",
                          "panel",
                          {},
                          jrpgmaker::ui::UiLayoutMode::kVertical,
                          0.0f,
                          true,
                          true,
                          {{2,
                            "editor.button",
                            "button",
                            {},
                            jrpgmaker::ui::UiLayoutMode::kAbsolute,
                            40.0f,
                            true,
                            true,
                            {}}}}));
    REQUIRE(tree.Layout({0, 0, 320, 200}));
    const auto* button = tree.Find(2);
    REQUIRE(button != nullptr);
    REQUIRE(button->bounds.width == 320);
    REQUIRE(tree.Layout({10, 20, 640, 480}));
    button = tree.Find(2);
    REQUIRE(button != nullptr);
    REQUIRE(button->bounds.x == 10);
    REQUIRE(button->bounds.y == 20);
    REQUIRE(button->bounds.width == 640);
    const auto commands = tree.Dispatch({jrpgmaker::ui::UiEventType::kPointerDown,
                                         0,
                                         {},
                                         button->bounds.x + 1,
                                         button->bounds.y + 1});
    REQUIRE(commands.size() == 1);
    REQUIRE(commands.front().type == jrpgmaker::ui::UiCommandType::kActivate);
    REQUIRE(commands.front().widget_id == 2);
    REQUIRE(tree.focus().focused_widget() == 2);
}

TEST_CASE("ui tree rejects duplicate ids and invalid layout", "[ui][editor][framework]") {
    jrpgmaker::ui::UiTree tree;
    REQUIRE_FALSE(tree.SetRoot(
        {1,
         {},
         {},
         {},
         jrpgmaker::ui::UiLayoutMode::kAbsolute,
         0,
         true,
         true,
         {{1, {}, {}, {}, jrpgmaker::ui::UiLayoutMode::kAbsolute, 0, true, true, {}}}}));
    REQUIRE_FALSE(tree.Layout({0, 0, 0, 100}));
}
