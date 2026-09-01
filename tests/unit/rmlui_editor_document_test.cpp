#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <RmlUi/Core/TextInputContext.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "jrpgmaker/editor/editor_user_settings.hpp"
#include "jrpgmaker/editor/editor_window.hpp"
#include "jrpgmaker/editor/rmlui_editor_document.hpp"
#include "jrpgmaker/editor/rmlui_editor_view.hpp"
#include "jrpgmaker/editor/rmlui_input.hpp"
#include "jrpgmaker/rhi/device_factory.hpp"

namespace {

jrpgmaker::ui::EditorLocale TestLocale() {
    jrpgmaker::ui::EditorLocale locale;
    locale.locale = "en";
    locale.strings = {
        {"editor.project.search_placeholder", "Filter"},
        {"editor.project.no_matches", "No matches"},
        {"editor.project.assets_documents", "Assets"},
        {"editor.empty.open_project", "Open a project"},
        {"editor.empty.open_project_hint", "File > Open"},
        {"editor.hierarchy.scene_structure", "Structure"},
        {"editor.hierarchy.cell", "Cell ({x}, {y}) - {walkable}"},
        {"editor.inspector.selection", "Selection"},
        {"editor.navigation.select_map", "Select a map"},
        {"editor.navigation.map", "Navigation Map"},
        {"editor.navigation.no_selection", "No selection"},
        {"editor.navigation.inspector_hint", "Toggle"},
        {"editor.navigation.grid", "Grid"},
        {"editor.navigation.coordinate", "Coordinate"},
        {"editor.navigation.walkable", "Walkable"},
        {"editor.navigation.on", "On"},
        {"editor.navigation.off", "Off"},
        {"editor.navigation.blocked", "Blocked"},
        {"editor.diagnostics.none", "No diagnostics"},
        {"editor.diagnostics.diff", "diff"},
        {"editor.inspector.select_object", "Select an object"},
        {"editor.status.project_ready", "READY"},
        {"editor.status.project_invalid", "INVALID"},
        {"editor.status.saved", "SAVED"},
        {"editor.status.dirty_short", "DIRTY"},
        {"editor.status.revision", "rev"},
        {"editor.status.selection", "selection"},
        {"editor.status.preview_running", "PREVIEW"},
        {"editor.status.editor_ready", "EDITOR"},
        {"editor.preview.process", "Preview process"},
        {"editor.preview.stderr", "Preview error"},
        {"editor.action.open", "Open"},
        {"editor.action.save", "Save"},
        {"editor.action.refresh", "Refresh"},
        {"editor.action.run_preview", "Run"},
        {"editor.action.stop_preview", "Stop"},
        {"editor.menu.file", "File"},
        {"editor.menu.project", "Project"},
        {"editor.menu.project_overview", "Overview"},
        {"editor.menu.general", "General"},
        {"editor.menu.rendering", "Rendering"},
        {"editor.menu.camera", "Camera"},
        {"editor.menu.input", "Input"},
        {"editor.menu.localization", "Localization"},
        {"editor.menu.assets", "Assets"},
        {"editor.menu.all_resources", "All"},
        {"editor.menu.rendering_resources", "Rendering"},
        {"editor.menu.plugin_data", "Plugins"},
        {"editor.menu.window", "Window"},
        {"editor.menu.layout", "Layout"},
        {"editor.menu.reset_layout", "Reset Default Layout"},
        {"editor.menu.maximize_focused_panel", "Maximize Focused Panel"},
        {"editor.menu.restore_focused_panel", "Restore Focused Panel"},
        {"editor.menu.run", "Run"},
        {"editor.workspace.project", "Project"},
        {"editor.workspace.hierarchy", "Hierarchy"},
        {"editor.workspace.left_dock", "Left workspace"},
        {"editor.workspace.scene", "Scene"},
        {"editor.workspace.inspector", "Inspector"},
        {"editor.workspace.diagnostics", "Diagnostics"},
        {"editor.document.navigation", "Navigation"},
        {"editor.project.category.scene", "Scene Data"},
    };
    return locale;
}

jrpgmaker::editor::EditorSessionState StateWithNavigation() {
    jrpgmaker::editor::EditorSessionState state;
    state.open = true;
    state.dirty = true;
    state.revision = 3;
    state.selection =
        jrpgmaker::editor::SelectionTarget{"core.navigation", "/walkable/1", "navigation.cell"};
    state.navigation = jrpgmaker::editor::NavigationProjection{
        .document_id = "core.navigation",
        .width = 2,
        .height = 1,
        .cells = {{0, 0, true, false, {}}, {1, 0, false, true, {}}},
        .selected = 1,
    };
    state.tabs.tabs.push_back({"core.navigation", "navigation.json", "editor.document.navigation",
                               true, true, true, 0, "editor.project.category.scene",
                               "core.navigation"});
    state.diff.changes.push_back({"core.navigation", "/walkable/1", "true", "false"});
    return state;
}

class FakeTextInputContext final : public Rml::TextInputContext {
public:
    bool GetBoundingBox(Rml::Rectanglef&) const override { return false; }

    void GetSelectionRange(int& start, int& end) const override {
        start = selection_start;
        end = selection_end;
    }

    void SetSelectionRange(int start, int end) override {
        selection_start = start;
        selection_end = end;
    }

    void SetCursorPosition(int position) override {
        selection_start = position;
        selection_end = position;
    }

    void SetText(Rml::StringView value, int start, int end) override {
        replaced_text = std::string(value.begin(), value.end());
        replaced_start = start;
        replaced_end = end;
    }

    void SetCompositionRange(int start, int end) override {
        composition_start = start;
        composition_end = end;
    }

    void CommitComposition(Rml::StringView value) override {
        committed_text = std::string(value.begin(), value.end());
    }

    int selection_start = 4;
    int selection_end = 4;
    int replaced_start = -1;
    int replaced_end = -1;
    int composition_start = -1;
    int composition_end = -1;
    std::string replaced_text;
    std::string committed_text;
};

} // namespace

TEST_CASE("RmlUi editor document exposes shared navigation selection and command targets",
          "[editor][rmlui][selection]") {
    const auto state = StateWithNavigation();
    const auto markup = jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(),
                                                                    "editor_workspace.rcss", "nav");
    REQUIRE(markup.find("data-command=\"navigation.select\" data-argument=\"1\"") !=
            std::string::npos);
    REQUIRE(markup.find("data-command=\"navigation.toggle\"") != std::string::npos);
    REQUIRE(markup.find("Coordinate") != std::string::npos);
    REQUIRE(markup.find("(1, 0)") != std::string::npos);
    REQUIRE(markup.find("data-command=\"project.filter\"") != std::string::npos);
    REQUIRE(markup.find("data-command=\"document.select\" data-argument=\"core.navigation\"") !=
            std::string::npos);
    REQUIRE(markup.find("diff") != std::string::npos);
}

TEST_CASE("RmlUi hierarchy is an accessible tree with every projected navigation cell",
          "[editor][rmlui][hierarchy][tree]") {
    const auto state = StateWithNavigation();
    const auto markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(), "editor_workspace.rcss");

    REQUIRE(markup.find("id=\"hierarchy-tree\"") != std::string::npos);
    REQUIRE(markup.find("role=\"tree\"") != std::string::npos);
    REQUIRE(markup.find("id=\"hierarchy-map\" class=\"tree-row root-row\" tabindex=\"-1\" "
                        "role=\"treeitem\" aria-expanded=\"true\" "
                        "data-command=\"hierarchy.toggle_navigation\"") != std::string::npos);
    REQUIRE(markup.find("id=\"hierarchy-children\" class=\"tree-children\" role=\"group\"") !=
            std::string::npos);
    REQUIRE(markup.find("id=\"hierarchy-cell-0\" class=\"tree-row child-row\" tabindex=\"-1\" "
                        "role=\"treeitem\" aria-selected=\"false\" "
                        "data-command=\"navigation.select\" data-argument=\"0\"") !=
            std::string::npos);
    REQUIRE(markup.find("id=\"hierarchy-cell-1\" class=\"tree-row selected-row child-row\" "
                        "tabindex=\"0\" role=\"treeitem\" aria-selected=\"true\" "
                        "data-command=\"navigation.select\" data-argument=\"1\"") !=
            std::string::npos);
    REQUIRE(markup.find("Cell (0, 0) - On") != std::string::npos);
    REQUIRE(markup.find("Cell (1, 0) - Blocked") != std::string::npos);
}

TEST_CASE("RmlUi hierarchy projects all cells up to the shared 2048-cell bound",
          "[editor][rmlui][hierarchy][bound]") {
    auto state = StateWithNavigation();
    state.navigation->selected.reset();
    state.navigation->cells.clear();
    state.navigation->cells.reserve(2050);
    for (int index = 0; index < 2050; ++index)
        state.navigation->cells.push_back({index, 0, index % 2 == 0, false, {}});

    const auto markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(), "editor_workspace.rcss");

    REQUIRE(markup.find("id=\"hierarchy-cell-0\"") != std::string::npos);
    REQUIRE(markup.find("id=\"hierarchy-cell-2047\"") != std::string::npos);
    REQUIRE(markup.find("id=\"hierarchy-cell-2048\"") == std::string::npos);
    REQUIRE(markup.find("Cell (2047, 0) - Blocked") != std::string::npos);
    REQUIRE(markup.find("tabindex=\"0\" role=\"treeitem\" aria-selected=\"false\"") ==
            std::string::npos);
    REQUIRE(markup.find("id=\"hierarchy-map\" class=\"tree-row root-row\" tabindex=\"0\"") !=
            std::string::npos);
}

TEST_CASE("RmlUi editor document gives interactive projections stable focus identities",
          "[editor][rmlui][focus]") {
    const auto state = StateWithNavigation();
    const auto markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(), "editor_workspace.rcss");

    REQUIRE(markup.find("id=\"document-row-0\"") != std::string::npos);
    REQUIRE(markup.find("id=\"hierarchy-map\"") != std::string::npos);
    REQUIRE(markup.find("id=\"hierarchy-cell-1\"") != std::string::npos);
    REQUIRE(markup.find("id=\"navigation-cell-1\"") != std::string::npos);
    REQUIRE(markup.find("id=\"inspector-walkable\"") != std::string::npos);
    REQUIRE(markup.find("id=\"navigation-cell-1\" class=\"grid-cell selected blocked\" "
                        "tabindex=\"0\" autofocus") != std::string::npos);
}

TEST_CASE("RmlUi editor document exposes keyboard activation roles", "[editor][rmlui][focus]") {
    const auto state = StateWithNavigation();
    const auto markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(), "editor_workspace.rcss");

    REQUIRE(markup.find("id=\"navigation-cell-1\" class=\"grid-cell selected blocked\" "
                        "tabindex=\"0\" autofocus role=\"button\"") != std::string::npos);
    REQUIRE(markup.find("id=\"inspector-walkable\" class=\"toggle off\" tabindex=\"-1\" "
                        "role=\"checkbox\"") != std::string::npos);
}

TEST_CASE("RmlUi editor document exposes Unity-style splitter semantics",
          "[editor][rmlui][splitter]") {
    const auto state = StateWithNavigation();
    const auto markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(), "editor_workspace.rcss");

    REQUIRE(markup.find("id=\"splitter-left-center\"") != std::string::npos);
    REQUIRE(markup.find("id=\"splitter-scene-inspector\"") != std::string::npos);
    REQUIRE(markup.find("id=\"splitter-diagnostics\"") != std::string::npos);
    REQUIRE(markup.find("role=\"separator\"") != std::string::npos);
    REQUIRE(markup.find("aria-orientation=\"vertical\"") != std::string::npos);
    REQUIRE(markup.find("aria-orientation=\"horizontal\"") != std::string::npos);
    REQUIRE(markup.find("aria-valuenow=\"0.22\"") != std::string::npos);
    REQUIRE(markup.find("aria-valuenow=\"0.80\"") != std::string::npos);
    REQUIRE(markup.find("aria-valuenow=\"0.86\"") != std::string::npos);
}

TEST_CASE("RmlUi editor document exposes a shared accessible Project Hierarchy dock",
          "[editor][rmlui][dock]") {
    const auto state = StateWithNavigation();
    jrpgmaker::editor::EditorUserSettings settings;
    settings.active_left_panel = jrpgmaker::editor::kPanelHierarchyId;
    const auto markup = jrpgmaker::editor::BuildRmlUiEditorDocument(
        &state, TestLocale(), "editor_workspace.rcss", {}, settings);

    REQUIRE(markup.find("id=\"left-dock\"") != std::string::npos);
    REQUIRE(markup.find("id=\"left-dock-tabs\"") != std::string::npos);
    REQUIRE(markup.find("role=\"tablist\"") != std::string::npos);
    REQUIRE(markup.find("id=\"left-dock-tab-project\"") != std::string::npos);
    REQUIRE(markup.find("id=\"left-dock-tab-hierarchy\"") != std::string::npos);
    REQUIRE(markup.find("role=\"tab\"") != std::string::npos);
    REQUIRE(markup.find("aria-selected=\"false\"") != std::string::npos);
    REQUIRE(markup.find("aria-selected=\"true\"") != std::string::npos);
    REQUIRE(markup.find("tabindex=\"-1\"") != std::string::npos);
    REQUIRE(markup.find("data-command=\"dock.activate\"") != std::string::npos);
    REQUIRE(markup.find("data-argument=\"workspace.project\"") != std::string::npos);
    REQUIRE(markup.find("data-argument=\"workspace.hierarchy\"") != std::string::npos);
    REQUIRE(markup.find("id=\"left-dock\" class=\"left-dock\"") != std::string::npos);

    settings.project_visible = false;
    settings.hierarchy_visible = true;
    const auto hierarchy_only = jrpgmaker::editor::BuildRmlUiEditorDocument(
        &state, TestLocale(), "editor_workspace.rcss", {}, settings);
    REQUIRE(hierarchy_only.find("id=\"left-dock-tab-project\"") == std::string::npos);
    REQUIRE(hierarchy_only.find("id=\"left-dock-tab-hierarchy\"") != std::string::npos);

    settings.hierarchy_visible = false;
    const auto no_left_dock = jrpgmaker::editor::BuildRmlUiEditorDocument(
        &state, TestLocale(), "editor_workspace.rcss", {}, settings);
    REQUIRE(no_left_dock.find("id=\"left-dock\"") == std::string::npos);
}

TEST_CASE("RmlUi editor view activates left dock tabs and keeps one page full height",
          "[editor][rmlui][dock][input][layout]") {
#if defined(_WIN32)
    constexpr auto backend = jrpgmaker::rhi::Backend::kD3D12;
#else
    constexpr auto backend = jrpgmaker::rhi::Backend::kVulkan;
#endif
    auto device = jrpgmaker::rhi::CreateDevice(backend);
    REQUIRE(device != nullptr);
    const auto state = StateWithNavigation();
    jrpgmaker::editor::EditorUserSettings settings;
    settings.active_left_panel = jrpgmaker::editor::kPanelHierarchyId;
    const auto markup = jrpgmaker::editor::BuildRmlUiEditorDocument(
        &state, TestLocale(), "editor_workspace.rcss", {}, settings);
    std::vector<std::string> commands;
    jrpgmaker::editor::RmlUiEditorView view;
    REQUIRE(view.Initialize(
        *device, {},
        [&](const jrpgmaker::editor::RmlUiCommand& command) {
            commands.push_back(command.name + ":" + command.argument);
        },
        400, 300));
    view.ImportSettings(settings);
    const auto source_url =
        (std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_DIR) / "rml/editor_workspace.rml")
            .generic_string();
    auto rml_source_url = source_url;
    std::replace(rml_source_url.begin(), rml_source_url.end(), ':', '|');
    REQUIRE(view.SetMarkup(markup, rml_source_url));

    const auto project_before = view.panel_layout("workspace.project");
    const auto hierarchy_before = view.panel_layout("workspace.hierarchy");
    REQUIRE(project_before.has_value());
    REQUIRE(hierarchy_before.has_value());
    REQUIRE_FALSE(project_before->visible);
    REQUIRE(hierarchy_before->visible);
    REQUIRE(hierarchy_before->height > 150.0f);
    const auto tabs_before = view.panel_layout("left-dock-tabs");
    REQUIRE(tabs_before.has_value());
    REQUIRE(tabs_before->x == Catch::Approx(0.0f));
    REQUIRE(tabs_before->y == Catch::Approx(68.0f));
    REQUIRE(tabs_before->height == Catch::Approx(30.0f));
    const auto project_tab = view.panel_layout("left-dock-tab-project");
    const auto hierarchy_tab = view.panel_layout("left-dock-tab-hierarchy");
    REQUIRE(project_tab.has_value());
    REQUIRE(hierarchy_tab.has_value());
    REQUIRE(project_tab->x < hierarchy_tab->x);
    INFO("project tab x=" << project_tab->x << " width=" << project_tab->width
                          << "; hierarchy tab x=" << hierarchy_tab->x << " width="
                          << hierarchy_tab->width << "; tabs width=" << tabs_before->width);

    (void) view.ProcessMouseMove(40.0f, 82.0f, {});
    (void) view.ProcessMouseButtonDown(0, {});
    (void) view.ProcessMouseButtonUp(0, {});
    REQUIRE(commands.size() == 1);
    REQUIRE(commands.back() == "dock.activate:workspace.project");
    REQUIRE(view.ExportSettings().active_left_panel == jrpgmaker::editor::kPanelProjectId);
    REQUIRE(view.panel_layout("workspace.project")->visible);
    REQUIRE_FALSE(view.panel_layout("workspace.hierarchy")->visible);
    REQUIRE(view.panel_layout("workspace.project")->height > 150.0f);
}

TEST_CASE("RmlUi hierarchy toggles internally and preserves expansion across view lifecycle",
          "[editor][rmlui][hierarchy][layout]") {
#if defined(_WIN32)
    constexpr auto backend = jrpgmaker::rhi::Backend::kD3D12;
#else
    constexpr auto backend = jrpgmaker::rhi::Backend::kVulkan;
#endif
    auto device = jrpgmaker::rhi::CreateDevice(backend);
    REQUIRE(device != nullptr);
    auto state = StateWithNavigation();
    state.navigation->selected.reset();
    for (auto& cell : state.navigation->cells)
        cell.selected = false;
    jrpgmaker::editor::EditorUserSettings settings;
    settings.active_left_panel = jrpgmaker::editor::kPanelHierarchyId;
    const auto markup = jrpgmaker::editor::BuildRmlUiEditorDocument(
        &state, TestLocale(), "editor_workspace.rcss", {}, settings);
    std::vector<std::string> commands;
    jrpgmaker::editor::RmlUiEditorView view;
    REQUIRE(view.Initialize(
        *device, {},
        [&](const jrpgmaker::editor::RmlUiCommand& command) {
            commands.push_back(command.name + ":" + command.argument);
        },
        400, 300));
    view.ImportSettings(settings);
    const auto source_url =
        (std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_DIR) / "rml/editor_workspace.rml")
            .generic_string();
    auto rml_source_url = source_url;
    std::replace(rml_source_url.begin(), rml_source_url.end(), ':', '|');
    REQUIRE(view.SetMarkup(markup, rml_source_url));
    REQUIRE(view.hierarchy_expanded());

    const auto hierarchy = view.panel_layout("workspace.hierarchy");
    REQUIRE(hierarchy.has_value());
    REQUIRE(hierarchy->visible);
    const auto root = view.panel_layout("hierarchy-map");
    REQUIRE(root.has_value());
    REQUIRE(root->visible);
    INFO("hierarchy root x=" << root->x << " y=" << root->y << " width=" << root->width
                             << " height=" << root->height);
    REQUIRE(view.ToggleHierarchy());
    REQUIRE_FALSE(view.hierarchy_expanded());
    REQUIRE(commands.empty());

    REQUIRE(view.Resize(640, 480));
    REQUIRE_FALSE(view.hierarchy_expanded());
    (void) view.ProcessMouseMove(10.0f, 75.0f, {});
    (void) view.ProcessMouseButtonDown(0, {});
    (void) view.ProcessMouseButtonUp(0, {});
    commands.clear();
    REQUIRE(view.ToggleFocusedPanelMaximize());
    REQUIRE(view.panel_maximized());
    REQUIRE_FALSE(view.hierarchy_expanded());
    REQUIRE(view.ToggleFocusedPanelMaximize());
    REQUIRE_FALSE(view.panel_maximized());
    REQUIRE_FALSE(view.hierarchy_expanded());

    REQUIRE(view.SetMarkup(markup, rml_source_url));
    REQUIRE_FALSE(view.hierarchy_expanded());

    REQUIRE(view.ToggleHierarchy());
    REQUIRE(view.hierarchy_expanded());
    REQUIRE(commands.empty());
    REQUIRE(view.ToggleHierarchy());
    REQUIRE_FALSE(view.hierarchy_expanded());
    REQUIRE(commands.empty());
}

TEST_CASE("RmlUi editor Window menu exposes panel checkbox commands", "[editor][rmlui][menu]") {
    const auto state = StateWithNavigation();
    jrpgmaker::editor::EditorUserSettings settings;
    settings.project_visible = false;
    settings.inspector_visible = false;
    const auto markup = jrpgmaker::editor::BuildRmlUiEditorDocument(
        &state, TestLocale(), "editor_workspace.rcss", {}, settings);
    REQUIRE(markup.find("id=\"menu-window-project\"") != std::string::npos);
    REQUIRE(markup.find("role=\"menuitemcheckbox\"") != std::string::npos);
    REQUIRE(markup.find("aria-checked=\"false\"") != std::string::npos);
    REQUIRE(markup.find("data-command=\"panel.toggle\"") != std::string::npos);
    REQUIRE(markup.find("data-argument=\"workspace.project\"") != std::string::npos);
    REQUIRE(markup.find("class=\"menu-check checked\"") != std::string::npos);
}

TEST_CASE("RmlUi editor splitters capture drags and preserve ratios across refresh and resize",
          "[editor][rmlui][splitter][input][layout]") {
#if defined(_WIN32)
    constexpr auto backend = jrpgmaker::rhi::Backend::kD3D12;
#else
    constexpr auto backend = jrpgmaker::rhi::Backend::kVulkan;
#endif
    auto device = jrpgmaker::rhi::CreateDevice(backend);
    REQUIRE(device != nullptr);

    const auto state = StateWithNavigation();
    const auto markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(), "editor_workspace.rcss");
    jrpgmaker::editor::RmlUiEditorView view;
    REQUIRE(view.Initialize(*device, {}, {}, 400, 300));
    const auto source_url =
        (std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_DIR) / "rml/editor_workspace.rml")
            .generic_string();
    auto rml_source_url = source_url;
    std::replace(rml_source_url.begin(), rml_source_url.end(), ':', '|');
    REQUIRE(view.SetMarkup(markup, rml_source_url));
    REQUIRE(view.splitter_ratio("splitter-left-center") == Catch::Approx(0.22f));
    REQUIRE(view.splitter_ratio("splitter-scene-inspector") == Catch::Approx(0.80f));
    REQUIRE(view.splitter_ratio("splitter-diagnostics") == Catch::Approx(0.86f));

    (void) view.ProcessMouseMove(88.0f, 150.0f, {});
    (void) view.ProcessMouseButtonDown(0, {});
    (void) view.ProcessMouseMove(140.0f, 150.0f, {});
    REQUIRE(view.splitter_ratio("splitter-left-center") == Catch::Approx(0.35f));
    (void) view.ProcessMouseButtonUp(0, {});
    (void) view.ProcessMouseMove(390.0f, 150.0f, {});
    REQUIRE(view.splitter_ratio("splitter-left-center") == Catch::Approx(0.35f));

    REQUIRE(view.SetMarkup(markup, rml_source_url));
    REQUIRE(view.splitter_ratio("splitter-left-center") == Catch::Approx(0.35f));
    REQUIRE(view.Resize(800, 600));
    REQUIRE(view.splitter_ratio("splitter-left-center") == Catch::Approx(0.35f));

    (void) view.ProcessMouseMove(640.0f, 300.0f, {});
    (void) view.ProcessMouseButtonDown(0, {});
    (void) view.ProcessMouseMove(500.0f, 300.0f, {});
    REQUIRE(view.splitter_ratio("splitter-scene-inspector") == Catch::Approx(0.625f));
    (void) view.ProcessMouseButtonUp(0, {});

    (void) view.ProcessMouseMove(700.0f, 516.0f, {});
    (void) view.ProcessMouseButtonDown(0, {});
    (void) view.ProcessMouseMove(700.0f, 420.0f, {});
    REQUIRE(view.splitter_ratio("splitter-diagnostics") == Catch::Approx(0.70f));
    (void) view.ProcessMouseButtonUp(0, {});

    REQUIRE(view.SetMarkup(markup, rml_source_url));
    (void) view.ProcessMouseMove(700.0f, 420.0f, {});
    (void) view.ProcessMouseButtonDown(700.0f, 420.0f, 0, {});
    (void) view.ProcessMouseMove(700.0f, 0.0f, {});
    REQUIRE(view.splitter_ratio("splitter-diagnostics") == Catch::Approx(0.60f));
    (void) view.ProcessMouseButtonUp(0, {});
}

TEST_CASE("RmlUi editor view imports and exports typed user splitter settings",
          "[editor][rmlui][settings]") {
#if defined(_WIN32)
    constexpr auto backend = jrpgmaker::rhi::Backend::kD3D12;
#else
    constexpr auto backend = jrpgmaker::rhi::Backend::kVulkan;
#endif
    auto device = jrpgmaker::rhi::CreateDevice(backend);
    REQUIRE(device != nullptr);
    const auto state = StateWithNavigation();
    const auto markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(), "editor_workspace.rcss");
    jrpgmaker::editor::RmlUiEditorView view;
    REQUIRE(view.Initialize(*device, {}, {}, 400, 300));
    const auto source_url =
        (std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_DIR) / "rml/editor_workspace.rml")
            .generic_string();
    auto rml_source_url = source_url;
    std::replace(rml_source_url.begin(), rml_source_url.end(), ':', '|');
    const jrpgmaker::editor::EditorUserSettings expected{
        .splitter_left_center = 0.30f,
        .splitter_scene_inspector = 0.75f,
        .splitter_diagnostics = 0.65f,
    };

    view.ImportSplitterRatios(expected);
    REQUIRE(view.SetMarkup(markup, rml_source_url));
    const auto exported = view.ExportSplitterRatios();

    REQUIRE(exported.splitter_left_center == Catch::Approx(0.30f));
    REQUIRE(exported.splitter_scene_inspector == Catch::Approx(0.75f));
    REQUIRE(exported.splitter_diagnostics == Catch::Approx(0.65f));

    view.ImportSplitterRatios(jrpgmaker::editor::EditorUserSettings{});
    const auto reset = view.ExportSplitterRatios();
    REQUIRE(reset.splitter_left_center == Catch::Approx(0.22f));
    REQUIRE(reset.splitter_scene_inspector == Catch::Approx(0.80f));
    REQUIRE(reset.splitter_diagnostics == Catch::Approx(0.86f));
}

TEST_CASE("RmlUi editor view toggles panels and exports visibility settings",
          "[editor][rmlui][settings][layout]") {
#if defined(_WIN32)
    constexpr auto backend = jrpgmaker::rhi::Backend::kD3D12;
#else
    constexpr auto backend = jrpgmaker::rhi::Backend::kVulkan;
#endif
    auto device = jrpgmaker::rhi::CreateDevice(backend);
    REQUIRE(device != nullptr);
    const auto state = StateWithNavigation();
    const auto markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(), "editor_workspace.rcss");
    jrpgmaker::editor::RmlUiEditorView view;
    REQUIRE(view.Initialize(*device, {}, {}, 400, 300));
    const auto source_url =
        (std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_DIR) / "rml/editor_workspace.rml")
            .generic_string();
    auto rml_source_url = source_url;
    std::replace(rml_source_url.begin(), rml_source_url.end(), ':', '|');
    REQUIRE(view.SetMarkup(markup, rml_source_url));
    REQUIRE(view.panel_visible("workspace.scene"));
    REQUIRE(view.TogglePanel("workspace.project"));
    REQUIRE_FALSE(view.panel_visible("workspace.project"));
    REQUIRE(view.TogglePanel("workspace.hierarchy"));
    REQUIRE(view.TogglePanel("workspace.inspector"));
    REQUIRE(view.TogglePanel("workspace.diagnostics"));
    const auto hidden = view.ExportSettings();
    REQUIRE_FALSE(hidden.project_visible);
    REQUIRE_FALSE(hidden.hierarchy_visible);
    REQUIRE_FALSE(hidden.inspector_visible);
    REQUIRE_FALSE(hidden.diagnostics_visible);
    REQUIRE_FALSE(view.TogglePanel("workspace.scene"));
}

TEST_CASE("RmlUi editor view maximizes the most recently clicked panel and restores layout",
          "[editor][rmlui][maximize][focus]") {
#if defined(_WIN32)
    constexpr auto backend = jrpgmaker::rhi::Backend::kD3D12;
#else
    constexpr auto backend = jrpgmaker::rhi::Backend::kVulkan;
#endif
    auto device = jrpgmaker::rhi::CreateDevice(backend);
    REQUIRE(device != nullptr);
    auto state = StateWithNavigation();
    const auto markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(), "editor_workspace.rcss");
    jrpgmaker::editor::RmlUiEditorView view;
    REQUIRE(view.Initialize(*device, {}, {}, 400, 300));
    const auto source_url =
        (std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_DIR) / "rml/editor_workspace.rml")
            .generic_string();
    auto rml_source_url = source_url;
    std::replace(rml_source_url.begin(), rml_source_url.end(), ':', '|');
    REQUIRE(view.SetMarkup(markup, rml_source_url));

    const auto original = view.ExportSettings();
    (void) view.ProcessMouseMove(20.0f, 100.0f, {});
    (void) view.ProcessMouseButtonDown(0, {});
    (void) view.ProcessMouseButtonUp(0, {});
    REQUIRE(view.focused_panel() == "workspace.project");
    REQUIRE_FALSE(view.panel_maximized());

    (void) view.ProcessKeyDown("Space", {});
    (void) view.ProcessKeyUp("Space", {});
    REQUIRE_FALSE(view.panel_maximized());

    REQUIRE_FALSE(view.ProcessKeyDown("Space", {false, true}));
    REQUIRE(view.panel_maximized());
    REQUIRE_FALSE(view.ProcessKeyUp("Space", {false, true}));
    REQUIRE(view.maximized_panel() == "workspace.project");
    const auto maximized = view.panel_layout("workspace.project");
    REQUIRE(maximized.has_value());
    REQUIRE(maximized->visible);
    REQUIRE(maximized->x == Catch::Approx(0.0f));
    REQUIRE(maximized->y == Catch::Approx(68.0f));
    REQUIRE(maximized->width == Catch::Approx(400.0f));
    REQUIRE(maximized->height == Catch::Approx(208.0f));
    REQUIRE_FALSE(view.panel_layout("workspace.hierarchy")->visible);
    REQUIRE_FALSE(view.panel_layout("workspace.scene")->visible);
    REQUIRE_FALSE(view.panel_layout("workspace.form")->visible);
    REQUIRE_FALSE(view.panel_layout("workspace.diagnostics")->visible);
    REQUIRE_FALSE(view.panel_layout("splitter-left-center")->visible);
    REQUIRE_FALSE(view.panel_layout("splitter-scene-inspector")->visible);
    REQUIRE_FALSE(view.panel_layout("splitter-diagnostics")->visible);
    REQUIRE(view.panel_layout("statusbar")->visible);

    REQUIRE_FALSE(view.ProcessKeyDown("Space", {false, true}));
    REQUIRE_FALSE(view.ProcessKeyUp("Space", {false, true}));
    REQUIRE_FALSE(view.panel_maximized());
    REQUIRE(view.ExportSettings().project_visible == original.project_visible);
    REQUIRE(view.ExportSettings().hierarchy_visible == original.hierarchy_visible);
    REQUIRE(view.ExportSettings().inspector_visible == original.inspector_visible);
    REQUIRE(view.ExportSettings().diagnostics_visible == original.diagnostics_visible);
    REQUIRE(view.splitter_ratio("splitter-left-center") ==
            Catch::Approx(original.splitter_left_center));
    REQUIRE(view.splitter_ratio("splitter-scene-inspector") ==
            Catch::Approx(original.splitter_scene_inspector));
    REQUIRE(view.splitter_ratio("splitter-diagnostics") ==
            Catch::Approx(original.splitter_diagnostics));
}

TEST_CASE("RmlUi editor maximize menu is localized and reflects transient availability",
          "[editor][rmlui][maximize][menu]") {
    auto state = StateWithNavigation();
    const auto normal = jrpgmaker::editor::BuildRmlUiEditorDocument(
        &state, TestLocale(), "editor_workspace.rcss", {}, {}, "workspace.scene", false);
    REQUIRE(normal.find("data-command=\"window.toggle_maximize\"") != std::string::npos);
    REQUIRE(normal.find("data-enabled=\"true\"") != std::string::npos);
    REQUIRE(normal.find("Maximize Focused Panel") != std::string::npos);
    const auto maximized = jrpgmaker::editor::BuildRmlUiEditorDocument(
        &state, TestLocale(), "editor_workspace.rcss", {}, {}, "workspace.scene", true);
    REQUIRE(maximized.find("data-command=\"window.toggle_maximize\"") != std::string::npos);
    REQUIRE(maximized.find("Restore Focused Panel") != std::string::npos);
    const auto unavailable = jrpgmaker::editor::BuildRmlUiEditorDocument(
        &state, TestLocale(), "editor_workspace.rcss", {}, {}, {}, false);
    REQUIRE(unavailable.find("data-command=\"window.toggle_maximize\"") != std::string::npos);
    REQUIRE(unavailable.find("data-enabled=\"false\"") != std::string::npos);
}

TEST_CASE("RmlUi editor toolbar exposes interaction state styles", "[editor][rmlui][toolbar]") {
    const auto stylesheet_path =
        std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_DIR) / "rml" / "editor_workspace.rcss";
    std::ifstream stylesheet_file(stylesheet_path);
    REQUIRE(stylesheet_file.is_open());
    const std::string stylesheet((std::istreambuf_iterator<char>(stylesheet_file)), {});

    REQUIRE(stylesheet.find("#toolbar > .toolbar-button:focus") != std::string::npos);
    REQUIRE(stylesheet.find("#toolbar > .toolbar-button:active") != std::string::npos);
    REQUIRE(stylesheet.find("#toolbar > .toolbar-button.disabled") != std::string::npos);
}

TEST_CASE("RmlUi hierarchy stylesheet keeps tree items as bounded rows",
          "[editor][rmlui][hierarchy]") {
    const auto stylesheet_path =
        std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_DIR) / "rml" / "editor_workspace.rcss";
    std::ifstream stylesheet_file(stylesheet_path);
    REQUIRE(stylesheet_file.is_open());
    const std::string stylesheet((std::istreambuf_iterator<char>(stylesheet_file)), {});

    REQUIRE(stylesheet.find(".hierarchy-list { height: 88%; overflow-y: auto; }") !=
            std::string::npos);
    REQUIRE(stylesheet.find(".hierarchy-tree, .tree-children { display: block; width: 100%; }") !=
            std::string::npos);
    REQUIRE(stylesheet.find(".tree-row { display: block; width: 100%;") != std::string::npos);
}

TEST_CASE("RmlUi editor disables unavailable toolbar buttons", "[editor][rmlui][toolbar]") {
    const auto state = StateWithNavigation();
    const auto markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(), "editor_workspace.rcss");

    REQUIRE(markup.find("class=\"toolbar-button disabled\" data-enabled=\"false\" disabled "
                        "data-command=\"run.stop\"") != std::string::npos);
}

TEST_CASE("RmlUi editor view preserves command focus across document refresh",
          "[editor][rmlui][focus]") {
#if defined(_WIN32)
    constexpr auto backend = jrpgmaker::rhi::Backend::kD3D12;
#else
    constexpr auto backend = jrpgmaker::rhi::Backend::kVulkan;
#endif
    auto device = jrpgmaker::rhi::CreateDevice(backend);
    REQUIRE(device != nullptr);

    jrpgmaker::editor::RmlUiEditorView view;
    std::vector<std::string> commands;
    const auto markup = R"(<rml><head><style>
body { width: 100%; height: 100%; margin: 0; }
#filter { position: absolute; left: 0; top: 0; width: 200px; height: 30px; }
#canvas { position: absolute; left: 0; top: 0; width: 300px; height: 200px; overflow: hidden; }
#cell { position: absolute; left: 10px; top: 10px; width: 100px; height: 40px; background-color: #343635; }
</style></head><body><input id="filter" tabindex="0" /><div id="canvas"><div id="cell" tabindex="0" autofocus role="button" data-command="navigation.toggle"></div></div></body></rml>)";
    REQUIRE(view.Initialize(
        *device, {},
        [&](const jrpgmaker::editor::RmlUiCommand& command) {
            commands.emplace_back(command.name);
        },
        320, 240));
    REQUIRE(view.SetMarkup(markup, "focus-test.rml"));
    (void) view.ProcessMouseMove(30.0f, 30.0f, {});
    (void) view.ProcessMouseButtonDown(0, {});
    (void) view.ProcessMouseButtonUp(0, {});
    REQUIRE(view.SetMarkup(markup, "focus-test.rml"));
    (void) view.ProcessKeyDown("Space", {});

    REQUIRE(commands.size() == 2);
    REQUIRE(commands[0] == "navigation.toggle");
    REQUIRE(commands[1] == "navigation.toggle");
}

TEST_CASE("RmlUi editor document exposes persistent menu interaction roles",
          "[editor][rmlui][menu]") {
    auto state = StateWithNavigation();
    state.tabs.tabs.push_back({"core.camera", "camera.json", "editor.document.camera", true, false,
                               true, 0, "editor.project.category.scene", "core.camera"});
    const auto markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(), "editor_workspace.rcss");

    REQUIRE(markup.find("class=\"menu-root\" data-menu-role=\"root\"") != std::string::npos);
    REQUIRE(markup.find("class=\"menu-label\" tabindex=\"0\"") != std::string::npos);
    REQUIRE(markup.find("class=\"menu-item\" tabindex=\"-1\"") != std::string::npos);
    REQUIRE(markup.find("class=\"menu-item submenu-item\" data-menu-role=\"submenu\"") !=
            std::string::npos);
    REQUIRE(markup.find("data-menu-role=\"submenu\" tabindex=\"0\"") != std::string::npos);
}

TEST_CASE("RmlUi window menu exposes a keyboard-addressable default layout command",
          "[editor][rmlui][menu][layout]") {
    const auto state = StateWithNavigation();
    const auto markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(), "editor_workspace.rcss");

    const auto submenu =
        markup.find("class=\"menu-item submenu-item\" data-menu-role=\"submenu\" tabindex=\"0\" "
                    "id=\"menu-window-layout\"");
    REQUIRE(submenu != std::string::npos);
    REQUIRE(markup.find("data-command=\"layout.reset_default\"", submenu) != std::string::npos);
    REQUIRE(markup.find(">Layout<", submenu) != std::string::npos);
    REQUIRE(markup.find(">Reset Default Layout<", submenu) != std::string::npos);
}

TEST_CASE("RmlUi editor menus expose only documents present in the shared projection",
          "[editor][rmlui][menu]") {
    auto state = StateWithNavigation();
    constexpr std::string_view material_menu_item =
        "class=\"menu-item\" tabindex=\"-1\" data-enabled=\"true\" "
        "data-command=\"document.select\" data-argument=\"core.material\"";

    auto markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(), "editor_workspace.rcss");
    REQUIRE(markup.find("data-argument=\"core.material\"") == std::string::npos);
    REQUIRE(markup.find("data-argument=\"core.camera\"") == std::string::npos);
    REQUIRE(markup.find("data-argument=\"app.input_actions\"") == std::string::npos);
    REQUIRE(markup.find("data-argument=\"domain.localization\"") == std::string::npos);
    REQUIRE(markup.find("id=\"menu-project-settings\"") == std::string::npos);

    state.tabs.tabs.push_back({"core.material", "material.json", "editor.document.material", true,
                               false, false, 0, "editor.project.category.rendering",
                               "core.material"});
    markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(), "editor_workspace.rcss");
    REQUIRE(markup.find(material_menu_item) == std::string::npos);

    state.tabs.tabs.back().editable = true;
    state.tabs.tabs.push_back({"core.camera", "camera.json", "editor.document.camera", true, false,
                               true, 0, "editor.project.category.scene", "core.camera"});
    markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(), "editor_workspace.rcss");
    REQUIRE(markup.find(material_menu_item) != std::string::npos);
    REQUIRE(markup.find("data-argument=\"core.camera\"") != std::string::npos);
    REQUIRE(markup.find("id=\"menu-project-settings\"") != std::string::npos);
}

TEST_CASE("RmlUi editor marks invalid plugin documents as read-only and locatable",
          "[editor][rmlui][plugin][read-only]") {
    auto state = StateWithNavigation();
    state.tabs.tabs.push_back({"plugin:readonly:sample.invalid:plugin_data/example.json",
                               "plugin_data/example.json", "plugin.sample.invalid.document", false,
                               false, false, 1, "editor.project.category.plugins",
                               "plugin.read_only.document"});
    state.diagnostics.push_back({"editor.document.read_only", "plugin_data/example.json"});
    const auto markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(), "editor_workspace.rcss");

    REQUIRE(markup.find("data-editable=\"false\" aria-readonly=\"true\"") != std::string::npos);
    REQUIRE(markup.find("editor.document.read_only") != std::string::npos);
    REQUIRE(markup.find("plugin_data/example.json") != std::string::npos);
}

TEST_CASE("RmlUi editor document renders an open-project empty state", "[editor][rmlui]") {
    const auto markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(nullptr, TestLocale(), "editor_workspace.rcss");
    REQUIRE(markup.find("Open a project") != std::string::npos);
    REQUIRE(markup.find("File &gt; Open") != std::string::npos);
    REQUIRE(markup.find("class=\"empty-project-open\"") != std::string::npos);
    REQUIRE(markup.find("role=\"button\"") != std::string::npos);
    REQUIRE(markup.find("tabindex=\"0\"") != std::string::npos);
    REQUIRE(markup.find("data-command=\"file.open\"") != std::string::npos);
    REQUIRE(markup.find("Open</button>") != std::string::npos);
}

TEST_CASE("RmlUi empty-project action supports mouse and keyboard activation",
          "[editor][rmlui][input]") {
#if defined(_WIN32)
    constexpr auto backend = jrpgmaker::rhi::Backend::kD3D12;
#else
    constexpr auto backend = jrpgmaker::rhi::Backend::kVulkan;
#endif
    auto device = jrpgmaker::rhi::CreateDevice(backend);
    REQUIRE(device != nullptr);

    jrpgmaker::editor::RmlUiEditorView view;
    std::vector<std::string> commands;
    const auto markup = R"(<rml><head><style>
body { width: 100%; height: 100%; margin: 0; }
#open { position: absolute; left: 10px; top: 10px; width: 120px; height: 40px; }
</style></head><body><button id="open" type="button" tabindex="0" role="button" data-command="file.open">Open</button></body></rml>)";
    REQUIRE(view.Initialize(
        *device, {},
        [&](const jrpgmaker::editor::RmlUiCommand& command) {
            commands.emplace_back(command.name);
        },
        320, 240));
    REQUIRE(view.SetMarkup(markup, "empty-project-input-test.rml"));

    (void) view.ProcessMouseMove(40.0f, 30.0f, {});
    (void) view.ProcessMouseButtonDown(0, {});
    (void) view.ProcessMouseButtonUp(0, {});
    REQUIRE(commands.size() == 1);
    REQUIRE(commands.back() == "file.open");

    REQUIRE(view.SetMarkup(markup, "empty-project-input-test.rml"));
    (void) view.ProcessKeyDown("Tab", {});
    (void) view.ProcessKeyDown("Enter", {});
    (void) view.ProcessKeyDown("Space", {});
    REQUIRE(commands.size() == 3);
    REQUIRE(commands[1] == "file.open");
    REQUIRE(commands[2] == "file.open");
}

TEST_CASE("RmlUi editor document exposes preview failures in diagnostics",
          "[editor][rmlui][preview]") {
    auto state = StateWithNavigation();
    state.preview.process_error = "editor.preview.process_start_failed";
    state.preview.standard_error = "runtime failed to initialise";

    const auto markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(), "editor_workspace.rcss");

    REQUIRE(markup.find("Preview process") != std::string::npos);
    REQUIRE(markup.find("editor.preview.process_start_failed") != std::string::npos);
    REQUIRE(markup.find("Preview error") != std::string::npos);
    REQUIRE(markup.find("runtime failed to initialise") != std::string::npos);
}

TEST_CASE("RmlUi editor document exposes nonzero preview exit codes in diagnostics",
          "[editor][rmlui][preview]") {
    auto state = StateWithNavigation();
    state.diff.changes.clear();
    state.preview.process_exit_code = 17;

    const auto markup =
        jrpgmaker::editor::BuildRmlUiEditorDocument(&state, TestLocale(), "editor_workspace.rcss");

    REQUIRE(markup.find("Preview process") != std::string::npos);
    REQUIRE(markup.find("17") != std::string::npos);
    REQUIRE(markup.find("class=\"diagnostic-count\">1") != std::string::npos);
}

TEST_CASE("RmlUi input mapping scales logical points to physical pixels", "[editor][rmlui][dpi]") {
    const auto point = jrpgmaker::editor::ToRmlUiInputPoint(13.25f, 8.5f, 2.0f);
    REQUIRE(point.x == 27);
    REQUIRE(point.y == 17);
}

TEST_CASE("RmlUi text input handler preserves IME composition ranges", "[editor][rmlui][ui text]") {
    jrpgmaker::editor::RmlUiTextInputHandler handler;
    FakeTextInputContext context;
    handler.OnActivate(&context);

    handler.HandleEdit("かな", 1, 1);
    REQUIRE(context.replaced_text == "かな");
    REQUIRE(context.replaced_start == 4);
    REQUIRE(context.replaced_end == 4);
    REQUIRE(context.composition_start == 4);
    REQUIRE(context.composition_end == 6);
    REQUIRE(context.selection_start == 5);
    REQUIRE(context.selection_end == 6);

    handler.HandleEdit("", 0, 0);
    REQUIRE(context.replaced_text.empty());
    REQUIRE(context.replaced_start == 4);
    REQUIRE(context.replaced_end == 6);
    REQUIRE(context.composition_start == 4);
    REQUIRE(context.composition_end == 4);
    REQUIRE(context.committed_text.empty());
}

TEST_CASE("Editor window requests a high density back buffer", "[editor][rmlui][dpi]") {
    const auto flags = jrpgmaker::editor::EditorWindowFlags(
#if defined(_WIN32)
        false
#else
        true
#endif
    );

    REQUIRE((flags & SDL_WINDOW_RESIZABLE) != 0);
    REQUIRE((flags & SDL_WINDOW_HIGH_PIXEL_DENSITY) != 0);
#if !defined(_WIN32)
    REQUIRE((flags & SDL_WINDOW_VULKAN) != 0);
#endif
}

TEST_CASE("Editor host reflows the workspace when its pixel size changes",
          "[editor][rmlui][layout]") {
    const auto source_path = std::filesystem::path(JRPGMAKER_EDITOR_SOURCE_DIR) / "main.cpp";
    std::ifstream source_file(source_path);
    REQUIRE(source_file.is_open());
    const std::string source((std::istreambuf_iterator<char>(source_file)), {});

    const auto resize_event = source.find("event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED");
    REQUIRE(resize_event != std::string::npos);
    const auto resize_event_end = source.find("break;", resize_event);
    REQUIRE(resize_event_end != std::string::npos);
    const auto resize_branch = source.substr(resize_event, resize_event_end - resize_event);
    REQUIRE(resize_branch.find("controller.Resize(") != std::string::npos);
    REQUIRE(resize_branch.find("running = false;") != std::string::npos);
}

TEST_CASE("Editor host owns the preference path and persists typed splitter layout",
          "[editor][rmlui][settings]") {
    const auto source_path = std::filesystem::path(JRPGMAKER_EDITOR_SOURCE_DIR) / "main.cpp";
    std::ifstream source_file(source_path);
    REQUIRE(source_file.is_open());
    const std::string source((std::istreambuf_iterator<char>(source_file)), {});

    REQUIRE(source.find("SDL_GetPrefPath") != std::string::npos);
    REQUIRE(source.find("LoadEditorUserSettings") != std::string::npos);
    REQUIRE(source.find("ImportSplitterRatios") != std::string::npos);
    REQUIRE(source.find("ExportSplitterRatios") != std::string::npos);
    REQUIRE(source.find("SaveEditorUserSettings") != std::string::npos);
    REQUIRE(source.find("persist_user_settings") != std::string::npos);
}

TEST_CASE("RmlUi editor samples generated text textures linearly", "[editor][rmlui][text]") {
    const auto source_path =
        std::filesystem::path(JRPGMAKER_EDITOR_SOURCE_DIR) / "src/rmlui_editor_view.cpp";
    std::ifstream source_file(source_path);
    REQUIRE(source_file.is_open());
    const std::string source((std::istreambuf_iterator<char>(source_file)), {});

    const auto create_sampler = source.find("bool CreateSampler()");
    REQUIRE(create_sampler != std::string::npos);
    const auto create_sampler_end = source.find("Rml::CompiledGeometryHandle", create_sampler);
    REQUIRE(create_sampler_end != std::string::npos);
    const auto create_sampler_body =
        source.substr(create_sampler, create_sampler_end - create_sampler);
    REQUIRE(create_sampler_body.find("rhi::SamplerFilter::kLinear") != std::string::npos);
}
