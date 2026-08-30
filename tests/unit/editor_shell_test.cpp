#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>

#include "jrpgmaker/editor/editor_shell.hpp"
#include "jrpgmaker/editor/form_projection.hpp"
#include "jrpgmaker/ui/draw_list.hpp"
#include "jrpgmaker/ui/editor_resources.hpp"
#include "jrpgmaker/ui/glyph_atlas.hpp"
#include "jrpgmaker/ui/menu.hpp"
#include "jrpgmaker/ui/text.hpp"
#include "jrpgmaker/ui/text_draw.hpp"

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

TEST_CASE("ui menu opens a submenu and emits a structured command", "[ui][menu]") {
    jrpgmaker::ui::MenuController menu;
    REQUIRE(menu.SetModel(
        {.roots = {{.id = 1,
                    .label_key = "editor.menu.file",
                    .children = {
                        {.id = 2, .label_key = "editor.action.save", .command = "file.save"}}}}}));
    REQUIRE(
        menu.Layout({0, 0, 300, 28}, {.root_width = 100, .row_height = 24, .popup_width = 180}));
    REQUIRE(menu.PointerDown(20, 12).changed);
    REQUIRE(menu.open());
    const auto command = menu.PointerDown(40, 28 + 12);
    REQUIRE(command.commands.size() == 1);
    REQUIRE(command.commands.front().type == jrpgmaker::ui::UiCommandType::kActivate);
    REQUIRE(command.commands.front().text == "file.save");
    REQUIRE_FALSE(menu.open());
}

TEST_CASE("ui menu keyboard navigation and escape obey focus ownership", "[ui][menu][ui context]") {
    jrpgmaker::ui::MenuController menu;
    REQUIRE(menu.SetModel({.roots = {{.id = 1,
                                      .label_key = "editor.menu.project",
                                      .children = {{.id = 2,
                                                    .label_key = "editor.document.camera",
                                                    .command = "document.core.camera"},
                                                   {.id = 3,
                                                    .label_key = "editor.document.input",
                                                    .command = "document.app.input_actions"}}}}}));
    REQUIRE(
        menu.Layout({0, 0, 240, 28}, {.root_width = 120, .row_height = 24, .popup_width = 180}));
    REQUIRE(menu.KeyDown("Alt").changed);
    REQUIRE(menu.KeyDown("Down").changed);
    REQUIRE(menu.KeyDown("Down").changed);
    REQUIRE(menu.BuildDrawList("button", "panel", "button").size() == 6);
    const auto command = menu.KeyDown("Enter");
    REQUIRE(command.commands.size() == 1);
    REQUIRE(command.commands.front().text == "document.app.input_actions");
    REQUIRE_FALSE(menu.KeyDown("Escape").changed);
}

TEST_CASE("editor input map is built from the action resource", "[editor]") {
    const jrpgmaker::ui::EditorActionMap resource{.id = "editor.actions",
                                                  .actions = {{"save", {"Ctrl+S"}},
                                                              {"cancel", {"Escape"}},
                                                              {"increment", {"PageUp"}},
                                                              {"decrement", {"PageDown"}},
                                                              {"toggle", {"Space"}},
                                                              {"choice_next", {"Right"}},
                                                              {"choice_previous", {"Left"}}}};
    const auto input = jrpgmaker::editor::BuildInputMap(resource);
    REQUIRE(input.Translate("Ctrl+S", true) == jrpgmaker::editor::EditorAction::kSave);
    REQUIRE(input.Translate("Escape", true) == jrpgmaker::editor::EditorAction::kCancel);
    REQUIRE(input.Translate("PageUp", true) == jrpgmaker::editor::EditorAction::kIncrement);
    REQUIRE(input.Translate("PageDown", true) == jrpgmaker::editor::EditorAction::kDecrement);
    REQUIRE(input.Translate("Space", true) == jrpgmaker::editor::EditorAction::kToggle);
    REQUIRE(input.Translate("Right", true) == jrpgmaker::editor::EditorAction::kChoiceNext);
    REQUIRE(input.Translate("Left", true) == jrpgmaker::editor::EditorAction::kChoicePrevious);
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

TEST_CASE("editor toolbar projects four distinct commands", "[ui][editor][toolbar]") {
    const auto draw_list = jrpgmaker::editor::BuildToolbarDrawList({0, 0, 400, 32}, false, false);
    REQUIRE(draw_list.size() == 8);
    REQUIRE(std::get<jrpgmaker::ui::DrawText>(draw_list.primitives()[1]).text_key ==
            "editor.action.open");
    REQUIRE(std::get<jrpgmaker::ui::DrawText>(draw_list.primitives()[3]).text_key ==
            "editor.action.save");
    REQUIRE(std::get<jrpgmaker::ui::DrawText>(draw_list.primitives()[5]).text_key ==
            "editor.action.run_preview");
    REQUIRE(std::get<jrpgmaker::ui::DrawText>(draw_list.primitives()[7]).text_key ==
            "editor.action.stop_preview");
}

TEST_CASE("project panel projects bounded categories and case insensitive filtering",
          "[editor][project][selection]") {
    const jrpgmaker::editor::DocumentTabsProjection documents{
        .tabs = {{.document_id = "project.manifest",
                  .path = "project.json",
                  .label_key = "editor.document.project",
                  .category_key = "editor.project.category.project"},
                 {.document_id = "core.camera",
                  .path = "assets/data/camera.json",
                  .label_key = "editor.document.camera",
                  .category_key = "editor.project.category.camera"},
                 {.document_id = "app.input_actions",
                  .path = "assets/data/input.json",
                  .label_key = "editor.document.input",
                  .category_key = "editor.project.category.input"}}};
    const auto filtered = jrpgmaker::editor::BuildProjectPanelProjection(documents, "CAMERA");
    REQUIRE(filtered.rows.size() == 3);
    REQUIRE(filtered.rows[0].kind == jrpgmaker::editor::ProjectPanelRowKind::kFilter);
    REQUIRE(filtered.rows[1].kind == jrpgmaker::editor::ProjectPanelRowKind::kCategory);
    REQUIRE(filtered.rows[2].kind == jrpgmaker::editor::ProjectPanelRowKind::kDocument);
    REQUIRE(filtered.rows[2].document_index == 1);
}

TEST_CASE("editor navigation projection reflows and hits bounded cells",
          "[editor][navigation][layout]") {
    jrpgmaker::editor::NavigationProjection navigation;
    navigation.width = 2;
    navigation.height = 2;
    navigation.cells = {{0, 0, true, false, {}},
                        {1, 0, false, false, {}},
                        {0, 1, true, true, {}},
                        {1, 1, true, false, {}}};
    jrpgmaker::editor::LayoutNavigation(navigation, {100, 40, 200, 160});
    REQUIRE(navigation.cells[0].bounds.width > 0.0f);
    REQUIRE(jrpgmaker::editor::HitNavigationCell(navigation, navigation.cells[2].bounds.x + 1,
                                                 navigation.cells[2].bounds.y + 1) == 2);
    REQUIRE_FALSE(jrpgmaker::editor::HitNavigationCell(navigation, 99, 39).has_value());
    REQUIRE(jrpgmaker::editor::BuildNavigationDrawList(navigation).size() == 4);
}

TEST_CASE("editor shell reflows regions with actual window size", "[editor][layout]") {
    jrpgmaker::ui::EditorLayout layout;
    layout.id = "editor.workspace";
    const auto node = [](std::string type, std::string id) {
        jrpgmaker::ui::EditorLayoutNode result;
        result.type = std::move(type);
        result.id = std::move(id);
        return result;
    };
    layout.root.type = "SplitPane";
    layout.root.id = "workspace.root";
    layout.root.children = {node("Toolbar", "workspace.toolbar"),
                            node("WorkspaceTree", "workspace.tree"),
                            node("SceneView", "workspace.scene"),
                            node("Inspector", "workspace.form"),
                            node("DiagnosticPanel", "workspace.diagnostics"),
                            node("StatusBar", "workspace.status")};
    auto shell = jrpgmaker::editor::BuildShellProjection(layout);
    REQUIRE(shell);
    jrpgmaker::editor::ReflowShellProjection(*shell, 900, 600);
    const auto find = [&shell](std::string_view id) -> const jrpgmaker::editor::ShellNode& {
        return *std::find_if(shell->nodes.begin(), shell->nodes.end(),
                             [id](const auto& node) { return node.id == id; });
    };
    REQUIRE(find("workspace.root").bounds.width == 900);
    REQUIRE(find("workspace.scene").bounds.width > 0);
    REQUIRE(find("workspace.scene").bounds.x < find("workspace.form").bounds.x);
}

TEST_CASE("editor status bar projects localized state and revision", "[ui][editor]") {
    const auto draw_list = jrpgmaker::editor::BuildStatusBarDrawList(
        {.open = true, .dirty = true, .revision = 7}, {0, 0, 100, 20}, "panel");
    REQUIRE(draw_list.size() == 2);
    const auto& text = std::get<jrpgmaker::ui::DrawText>(draw_list.primitives()[1]);
    REQUIRE(text.text_key == "editor.status.dirty");
    REQUIRE(text.arguments.at("revision") == "7");
}

TEST_CASE("ui draw list is backend agnostic and bounded", "[ui][editor]") {
    jrpgmaker::ui::DrawList draw_list;
    REQUIRE(draw_list.Add(jrpgmaker::ui::DrawRect{{0.0f, 0.0f, 10.0f, 10.0f}, "panel"}));
    REQUIRE(draw_list.Add(jrpgmaker::ui::DrawText{
        {1.0f, 1.0f, 8.0f, 8.0f}, "editor.window.title", {}, std::nullopt}));
    REQUIRE(draw_list.size() == 2);
    REQUIRE(std::holds_alternative<jrpgmaker::ui::DrawRect>(draw_list.primitives().front()));
}

TEST_CASE("form draw projection emits focused theme states from adapter metadata", "[ui][editor]") {
    const jrpgmaker::editor::FormProjection form{
        .document_id = "project.manifest",
        .fields = {{.path = "/id",
                    .label_key = "editor.project.id",
                    .recipe = "input",
                    .value_type = "string",
                    .value = "demo",
                    .choices = {}},
                   {.path = "/render_style",
                    .label_key = "editor.project.render_style",
                    .recipe = "input",
                    .value_type = "string",
                    .value = "unlit",
                    .choices = {}}}};
    const auto draw_list = jrpgmaker::editor::BuildFormDrawList(form, {10, 20, 100, 80}, 20, 1);
    REQUIRE(draw_list.size() == 6);
    const auto& focused = std::get<jrpgmaker::ui::DrawRect>(draw_list.primitives()[3]);
    REQUIRE(focused.state == "focused");
}

TEST_CASE("form field roles use the committed input recipe", "[ui][editor]") {
    const jrpgmaker::editor::FormProjection form{.document_id = "project.manifest",
                                                 .fields = {{.path = "/id",
                                                             .label_key = "editor.project.id",
                                                             .recipe = "text",
                                                             .value_type = "string",
                                                             .value = "demo",
                                                             .choices = {}}}};
    const auto draw_list = jrpgmaker::editor::BuildFormDrawList(form, {0, 0, 100, 20}, 20, 0);

    REQUIRE(draw_list.size() == 3);
    REQUIRE(std::get<jrpgmaker::ui::DrawRect>(draw_list.primitives()[0]).recipe == "input");
}

TEST_CASE("preview draw projection preserves structured metric and diagnostic keys",
          "[ui][editor]") {
    const jrpgmaker::editor::PreviewProjection valid{
        .valid = true,
        .process_running = false,
        .process_exit_code = 0,
        .process_error = {},
        .standard_output = {},
        .standard_error = {},
        .diagnostics = {},
        .metrics = {{"editor.preview.event_count", "integer", 2}}};
    const auto metric_draw = jrpgmaker::editor::BuildPreviewDrawList(valid, {0, 0, 100, 40}, 20);
    REQUIRE(metric_draw.size() == 3);
    const jrpgmaker::editor::PreviewProjection invalid{
        .valid = false,
        .process_running = false,
        .process_exit_code = 0,
        .process_error = {},
        .standard_output = {},
        .standard_error = {},
        .diagnostics = {{"project.invalid", "project.json"}},
        .metrics = {}};
    const auto diagnostic_draw =
        jrpgmaker::editor::BuildPreviewDrawList(invalid, {0, 0, 100, 40}, 20);
    REQUIRE(diagnostic_draw.size() == 2);
    const auto& diagnostic_text =
        std::get<jrpgmaker::ui::DrawText>(diagnostic_draw.primitives()[1]);
    REQUIRE(diagnostic_text.text_key == "editor.diagnostic.code");
    REQUIRE(diagnostic_text.arguments.at("code") == "project.invalid");
    REQUIRE(diagnostic_text.arguments.at("path") == "project.json");
}

TEST_CASE("document tabs preserve manifest order and dirty active state", "[ui][editor]") {
    const std::vector<jrpgmaker::project::DocumentDescriptor> documents = {
        {"project.manifest", "project.json", "editor.document.project", true, "project.manifest"},
        {"domain.event_script", "events.json", "editor.document.events", true,
         "domain.event_script"},
        {"core.material", "materials.json", "editor.document.material", false, "core.material"}};
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
    REQUIRE(std::get<jrpgmaker::ui::DrawRect>(draw_list.primitives()[2]).state == "pressed");
}

TEST_CASE("document tabs mark every document with pending changes", "[ui][editor]") {
    const std::vector<jrpgmaker::project::DocumentDescriptor> documents = {
        {"project.manifest", "project.json", "editor.document.project", true, "project.manifest"},
        {"core.navigation", "navigation.json", "editor.document.navigation", true,
         "core.navigation"}};
    const std::vector<jrpgmaker::project::Change> changes = {{.document_id = "project.manifest",
                                                              .field_path = "/id",
                                                              .before = "old",
                                                              .after = "new",
                                                              .sequence = 1},
                                                             {.document_id = "core.navigation",
                                                              .field_path = "/width",
                                                              .before = 4,
                                                              .after = 5,
                                                              .sequence = 2}};
    const auto tabs =
        jrpgmaker::editor::BuildDocumentTabsProjection(documents, "core.navigation", changes, {});

    REQUIRE(tabs.tabs[0].dirty);
    REQUIRE(tabs.tabs[1].dirty);
    REQUIRE(tabs.tabs[1].active);
}

TEST_CASE("diagnostics filter and diff projection retain stable document ownership", "[editor]") {
    const std::vector<jrpgmaker::project::DocumentDescriptor> documents = {
        {"project.manifest", "project.json", "editor.document.project", true, "project.manifest"},
        {"core.navigation", "navigation.json", "editor.document.navigation", true,
         "core.navigation"}};
    const std::vector<jrpgmaker::project::Diagnostic> diagnostics = {
        {"navigation.invalid", "navigation.json"}, {"project.invalid", "project.json"}};
    const auto filtered =
        jrpgmaker::editor::BuildDiagnosticPanelProjection(documents, diagnostics, "navigation");
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
    const jrpgmaker::editor::PreviewProjection preview{.valid = true,
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
    const jrpgmaker::editor::PreviewProjection preview{.valid = true,
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

TEST_CASE("workspace preview metrics resolve through committed locale and text draw",
          "[ui][editor]") {
    const auto resources =
        jrpgmaker::ui::LoadEditorResources(std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_DIR));
    REQUIRE(resources);

    const jrpgmaker::project::DiagnosticSet diagnosis{.diagnostics = {},
                                                      .event_count = 2,
                                                      .interaction_count = 3,
                                                      .collision_count = 4,
                                                      .navigation_width = 16,
                                                      .navigation_height = 12,
                                                      .camera_region_count = 5};
    const auto preview = jrpgmaker::editor::BuildWorkspacePreview(diagnosis);
    const auto source = jrpgmaker::editor::BuildPreviewDrawList(preview, {0, 0, 320, 180}, 30);

    jrpgmaker::ui::Font font;
    REQUIRE(font.Load(
        (std::filesystem::path(JRPGMAKER_ASSET_DIR) / "fonts/NotoSansCJK-Regular.ttc").string()));
    jrpgmaker::ui::GlyphAtlas atlas(512, 512, 256);
    const auto text =
        jrpgmaker::ui::BuildTextDrawList(source, resources.bundle->locale, font, atlas, 20);

    REQUIRE(text.ok());
    REQUIRE(text.diagnostics.empty());
}
