#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/editor/editor_shell.hpp"

TEST_CASE("editor input map translates only pressed configured keys", "[editor]") {
    jrpgmaker::editor::InputMap input;
    REQUIRE(input.Add({"Ctrl+S", jrpgmaker::editor::EditorAction::kSave}));
    REQUIRE_FALSE(input.Add({"Ctrl+S", jrpgmaker::editor::EditorAction::kOpen}));
    REQUIRE(input.Translate("Ctrl+S", true) == jrpgmaker::editor::EditorAction::kSave);
    REQUIRE_FALSE(input.Translate("Ctrl+S", false).has_value());
    REQUIRE_FALSE(input.Translate("Ctrl+O", true).has_value());
}

TEST_CASE("editor input map is built from the action resource", "[editor]") {
    const jrpgmaker::ui::EditorActionMap resource{
        .id = "editor.actions",
        .actions = {{"save", {"Ctrl+S"}}, {"cancel", {"Escape"}}}};
    const auto input = jrpgmaker::editor::BuildInputMap(resource);
    REQUIRE(input.Translate("Ctrl+S", true) == jrpgmaker::editor::EditorAction::kSave);
    REQUIRE(input.Translate("Escape", true) == jrpgmaker::editor::EditorAction::kCancel);
}

TEST_CASE("editor shell projection preserves layout hierarchy and metadata", "[editor]") {
    jrpgmaker::ui::EditorLayout layout;
    layout.id = "editor.workspace";
    layout.root = {.type = "SplitPane", .id = "root", .label_key = "editor.window.title",
                   .recipe = "panel", .children = {{.type = "WorkspaceTree", .id = "tree"}}};
    const auto projection = jrpgmaker::editor::BuildShellProjection(layout);
    REQUIRE(projection.has_value());
    REQUIRE(projection->layout_id == "editor.workspace");
    REQUIRE(projection->nodes.size() == 2);
    REQUIRE(projection->nodes[0].children == std::vector<std::size_t>{1});
    REQUIRE(projection->nodes[0].label_key == "editor.window.title");
}
