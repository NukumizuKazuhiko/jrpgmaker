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
    REQUIRE(input.Translate("S", true, true, false, false) ==
            jrpgmaker::editor::EditorAction::kSave);
    REQUIRE_FALSE(input.Translate("S", true, false, false, false).has_value());
}

TEST_CASE("editor input map is built from the action resource", "[editor]") {
    const jrpgmaker::ui::EditorActionMap resource{
        .id = "editor.actions",
        .actions = {{"save", {"Ctrl+S"}},
                    {"cancel", {"Escape"}},
                    {"increment", {"PageUp"}},
                    {"decrement", {"PageDown"}}}};
    const auto input = jrpgmaker::editor::BuildInputMap(resource);
    REQUIRE(input.Translate("Ctrl+S", true) == jrpgmaker::editor::EditorAction::kSave);
    REQUIRE(input.Translate("Escape", true) == jrpgmaker::editor::EditorAction::kCancel);
    REQUIRE(input.Translate("PageUp", true) == jrpgmaker::editor::EditorAction::kIncrement);
    REQUIRE(input.Translate("PageDown", true) == jrpgmaker::editor::EditorAction::kDecrement);
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
    REQUIRE(draw_list.Add(
        jrpgmaker::ui::DrawText{{1.0f, 1.0f, 8.0f, 8.0f}, "editor.window.title", {}}));
    REQUIRE(draw_list.size() == 2);
    REQUIRE(std::holds_alternative<jrpgmaker::ui::DrawRect>(draw_list.primitives().front()));
}

TEST_CASE("form draw projection emits focused theme states from adapter metadata", "[ui][editor]") {
    const jrpgmaker::editor::FormProjection form{
        .document_id = "project.manifest",
        .fields = {{.path = "/id", .label_key = "editor.project.id", .recipe = "input",
                    .value_type = "string", .value = "demo"},
                   {.path = "/render_style", .label_key = "editor.project.render_style",
                    .recipe = "input", .value_type = "string", .value = "unlit"}}};
    const auto draw_list = jrpgmaker::editor::BuildFormDrawList(form, {10, 20, 100, 80}, 20, 1);
    REQUIRE(draw_list.size() == 6);
    const auto& focused = std::get<jrpgmaker::ui::DrawRect>(draw_list.primitives()[3]);
    REQUIRE(focused.state == "focused");
}

TEST_CASE("preview draw projection preserves structured metric and diagnostic keys", "[ui][editor]") {
    const jrpgmaker::editor::PreviewProjection valid{
        .valid = true, .process_running = false, .process_exit_code = 0, .process_error = {},
        .standard_output = {}, .standard_error = {},
        .diagnostics = {},
        .metrics = {{"editor.preview.event_count", "integer", 2}}};
    const auto metric_draw = jrpgmaker::editor::BuildPreviewDrawList(valid, {0, 0, 100, 40}, 20);
    REQUIRE(metric_draw.size() == 3);
    const jrpgmaker::editor::PreviewProjection invalid{
        .valid = false, .process_running = false, .process_exit_code = 0, .process_error = {},
        .standard_output = {}, .standard_error = {},
        .diagnostics = {{"project.invalid", "project.json"}},
        .metrics = {}};
    const auto diagnostic_draw =
        jrpgmaker::editor::BuildPreviewDrawList(invalid, {0, 0, 100, 40}, 20);
    REQUIRE(diagnostic_draw.size() == 2);
    REQUIRE(std::get<jrpgmaker::ui::DrawText>(diagnostic_draw.primitives()[1]).text_key ==
            "editor.diagnostic.code");
}

TEST_CASE("document tabs preserve manifest order and dirty active state", "[ui][editor]") {
    const std::vector<jrpgmaker::project::DocumentDescriptor> documents = {
        {"project.manifest", "project.json", "editor.document.project", true},
        {"domain.event_script", "events.json", "editor.document.events", true},
        {"core.material", "materials.json", "editor.document.material", false}};
    const auto tabs = jrpgmaker::editor::BuildDocumentTabsProjection(
        documents, "domain.event_script", true, {{"project.bad", "events.json"}});
    REQUIRE(tabs.tabs.size() == 3);
    REQUIRE(tabs.tabs[0].document_id == "project.manifest");
    REQUIRE_FALSE(tabs.tabs[0].active);
    REQUIRE(tabs.tabs[1].active);
    REQUIRE(tabs.tabs[1].dirty);
    REQUIRE(tabs.tabs[1].diagnostic_count == 1);
    REQUIRE_FALSE(tabs.tabs[2].editable);

    const auto draw_list = jrpgmaker::editor::BuildDocumentTabsDrawList(tabs, {0, 0, 300, 30});
    REQUIRE(draw_list.size() == 6);
    REQUIRE(std::get<jrpgmaker::ui::DrawRect>(draw_list.primitives()[2]).state == "active_dirty");
}

TEST_CASE("diagnostics filter and diff projection retain stable document ownership", "[editor]") {
    const std::vector<jrpgmaker::project::DocumentDescriptor> documents = {
        {"project.manifest", "project.json", "editor.document.project", true},
        {"core.navigation", "navigation.json", "editor.document.navigation", true}};
    const std::vector<jrpgmaker::project::Diagnostic> diagnostics = {
        {"navigation.invalid", "navigation.json"}, {"project.invalid", "project.json"}};
    const auto filtered = jrpgmaker::editor::BuildDiagnosticPanelProjection(
        documents, diagnostics, "navigation");
    REQUIRE(filtered.items.size() == 1);
    REQUIRE(filtered.items[0].document_id == "core.navigation");
    REQUIRE(filtered.items[0].diagnostic.code == "navigation.invalid");

    const std::vector<jrpgmaker::project::Change> changes = {
        {"project.manifest", "/id", "a", "b", 2},
        {"project.manifest", "/render_style", "x", "y", 1}};
    const auto diff = jrpgmaker::editor::BuildDiffProjection(changes);
    REQUIRE(diff.changes[0].sequence == 1);
    REQUIRE(diff.changes[1].sequence == 2);
}

TEST_CASE("preview draw projection exposes bounded process status", "[ui][editor]") {
    const jrpgmaker::editor::PreviewProjection preview{
        .valid = true,
        .process_running = false,
        .process_exit_code = 7,
        .process_error = {},
        .standard_output = {},
        .standard_error = {},
        .diagnostics = {},
        .metrics = {}};
    const auto draw_list = jrpgmaker::editor::BuildPreviewDrawList(preview, {0, 0, 100, 40}, 20);
    REQUIRE(draw_list.size() == 2);
    REQUIRE(std::get<jrpgmaker::ui::DrawText>(draw_list.primitives()[1]).text_key ==
            "editor.preview.process");
}

TEST_CASE("preview draw projection exposes bounded process logs", "[ui][editor]") {
    const jrpgmaker::editor::PreviewProjection preview{
        .valid = true,
        .process_running = false,
        .process_exit_code = 0,
        .process_error = {},
        .standard_output = "stdout",
        .standard_error = "stderr",
        .diagnostics = {},
        .metrics = {}};
    const auto draw_list = jrpgmaker::editor::BuildPreviewDrawList(preview, {0, 0, 100, 80}, 20);
    REQUIRE(draw_list.size() == 6);
    REQUIRE(std::get<jrpgmaker::ui::DrawText>(draw_list.primitives()[3]).text_key ==
            "editor.preview.stdout");
    REQUIRE(std::get<jrpgmaker::ui::DrawText>(draw_list.primitives()[5]).text_key ==
            "editor.preview.stderr");
}
