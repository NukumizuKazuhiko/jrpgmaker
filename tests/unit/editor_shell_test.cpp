#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/editor/editor_shell.hpp"
#include "jrpgmaker/ui/draw_list.hpp"

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
    layout.root = {.type = "SplitPane",
                   .id = "root",
                   .label_key = "editor.window.title",
                   .recipe = "panel",
                   .bounds = {},
                   .children = {{.type = "WorkspaceTree",
                                 .id = "tree",
                                 .label_key = "",
                                 .recipe = "",
                                 .bounds = {},
                                 .children = {}}}};
    const auto projection = jrpgmaker::editor::BuildShellProjection(layout);
    REQUIRE(projection.has_value());
    REQUIRE(projection->layout_id == "editor.workspace");
    REQUIRE(projection->nodes.size() == 2);
    REQUIRE(projection->nodes[0].children == std::vector<std::size_t>{1});
    REQUIRE(projection->nodes[0].label_key == "editor.window.title");
}

TEST_CASE("editor shell projection produces draw primitives from layout bounds", "[editor]") {
    jrpgmaker::ui::EditorLayout layout;
    layout.id = "editor.workspace";
    layout.root = {.type = "Panel",
                   .id = "root",
                   .label_key = "editor.window.title",
                   .recipe = "panel",
                   .bounds = {0.0f, 0.0f, 100.0f, 50.0f},
                   .children = {}};
    const auto projection = jrpgmaker::editor::BuildShellProjection(layout);
    REQUIRE(projection.has_value());
    const auto draw_list = jrpgmaker::editor::BuildShellDrawList(*projection);
    REQUIRE(draw_list.size() == 2);
    REQUIRE(std::holds_alternative<jrpgmaker::ui::DrawRect>(draw_list.primitives()[0]));
    REQUIRE(std::holds_alternative<jrpgmaker::ui::DrawText>(draw_list.primitives()[1]));
}

TEST_CASE("ui draw list is backend agnostic and bounded", "[ui][editor]") {
    jrpgmaker::ui::DrawList draw_list;
    REQUIRE(draw_list.Add(jrpgmaker::ui::DrawRect{{0.0f, 0.0f, 10.0f, 10.0f}, "panel"}));
    REQUIRE(draw_list.Add(jrpgmaker::ui::DrawText{{1.0f, 1.0f, 8.0f, 8.0f}, "editor.window.title"}));
    REQUIRE(draw_list.size() == 2);
    REQUIRE(std::holds_alternative<jrpgmaker::ui::DrawRect>(draw_list.primitives().front()));
}
