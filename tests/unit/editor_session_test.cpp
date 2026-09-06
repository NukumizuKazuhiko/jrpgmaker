#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "jrpgmaker/editor/editor_session.hpp"
#include "jrpgmaker/editor/editor_workspace_controller.hpp"
#include "jrpgmaker/editor/preview_process.hpp"
#include "jrpgmaker/ui/editor_resources.hpp"

namespace {

std::filesystem::path MakeFixture(std::string_view suffix = {}) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("jrpgmaker_editor_session_fixture" + std::string(suffix));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "assets/data");
    std::filesystem::copy(std::filesystem::path(JRPGMAKER_ASSET_DIR) / "data", root / "assets/data",
                          std::filesystem::copy_options::recursive, error);
    std::filesystem::copy_file(
        std::filesystem::path(JRPGMAKER_ASSET_DIR) / "data/project_demo.json",
        root / "project.json", std::filesystem::copy_options::overwrite_existing, error);
    return root;
}

} // namespace

TEST_CASE("editor workspace controller routes toolbar clicks as structured host requests",
          "[editor][ui context][layout]") {
    const auto resources =
        jrpgmaker::ui::LoadEditorResources(std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_DIR));
    REQUIRE(resources);
    jrpgmaker::editor::EditorWorkspaceController controller(
        {.layout = resources.bundle->layout,
         .form_row_height = resources.bundle->theme.dimensions.at("font.body") +
                            resources.bundle->theme.dimensions.at("space.sm"),
         .diagnostic_row_height = resources.bundle->theme.dimensions.at("font.caption") +
                                  resources.bundle->theme.dimensions.at("space.xs"),
         .menu = {},
         .runtime_executable = {}});
    REQUIRE(controller.Resize(1280, 720));

    const auto toolbar = controller.panel_bounds("workspace.toolbar");
    REQUIRE(toolbar);
    const auto open = controller.PointerDown(toolbar->x + 20, toolbar->y + toolbar->height * 0.5f);
    REQUIRE(open.host_request == jrpgmaker::editor::EditorHostRequest::kOpenProjectDialog);
    REQUIRE_FALSE(open.changed);
}

TEST_CASE("editor workspace controller routes nested project settings to real documents",
          "[editor][menu][selection]") {
    const auto root = MakeFixture("_menu_settings");
    const auto resources =
        jrpgmaker::ui::LoadEditorResources(std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_DIR));
    REQUIRE(resources);
    jrpgmaker::editor::EditorWorkspaceController controller(
        {.layout = resources.bundle->layout, .menu = {}, .runtime_executable = {}});
    REQUIRE(controller.Resize(1280, 720));
    REQUIRE(controller.OpenProject(root));

    REQUIRE(controller.PointerDown(120, 14).changed);
    REQUIRE(controller.menu_open());
    REQUIRE(controller.PointerDown(120, 28 + 28 + 14).changed);
    REQUIRE(controller.menu_open());
    REQUIRE(controller.PointerDown(360, 56 + 28 + 14).changed);
    REQUIRE_FALSE(controller.menu_open());
    REQUIRE(controller.state() != nullptr);
    REQUIRE(controller.state()->form.document_id == "core.material");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor workspace controller filters project resources through the shared input seam",
          "[editor][project][ui text]") {
    const auto root = MakeFixture("_project_filter");
    const auto resources =
        jrpgmaker::ui::LoadEditorResources(std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_DIR));
    REQUIRE(resources);
    jrpgmaker::editor::EditorWorkspaceController controller(
        {.layout = resources.bundle->layout, .menu = {}, .runtime_executable = {}});
    REQUIRE(controller.Resize(1280, 720));
    REQUIRE(controller.OpenProject(root));
    const auto project = controller.panel_bounds("workspace.project");
    REQUIRE(project);

    REQUIRE(controller.PointerDown(project->x + 20, project->y + 12).changed);
    REQUIRE(controller.ApplyText("camera"));
    const auto filtered = controller.BuildDrawList();
    const auto search = std::find_if(
        filtered.primitives().begin(), filtered.primitives().end(), [](const auto& primitive) {
            const auto* text = std::get_if<jrpgmaker::ui::DrawText>(&primitive);
            return text != nullptr && text->text_key == "editor.project.search";
        });
    REQUIRE(search != filtered.primitives().end());
    REQUIRE(std::get<jrpgmaker::ui::DrawText>(*search).arguments.at("value") == "camera");
    REQUIRE(controller.PointerDown(project->x + 20, project->y + 2 * 24.0f + 12).changed);
    REQUIRE(controller.state()->form.document_id == "core.camera");

    REQUIRE(controller.PointerDown(project->x + 20, project->y + 12).changed);
    REQUIRE(controller.ApplyTextKey("Backspace"));
    const auto restored = controller.BuildDrawList();
    const auto restored_search = std::find_if(
        restored.primitives().begin(), restored.primitives().end(), [](const auto& primitive) {
            const auto* text = std::get_if<jrpgmaker::ui::DrawText>(&primitive);
            return text != nullptr && text->text_key == "editor.project.search";
        });
    REQUIRE(restored_search != restored.primitives().end());
    REQUIRE(std::get<jrpgmaker::ui::DrawText>(*restored_search).arguments.at("value").empty());
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE(
    "editor workspace controller keeps project scene hierarchy and inspector selection shared",
    "[editor][selection][navigation][ui context]") {
    const auto root = MakeFixture("_workspace_controller");
    const auto resources =
        jrpgmaker::ui::LoadEditorResources(std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_DIR));
    REQUIRE(resources);
    jrpgmaker::editor::EditorWorkspaceController controller({.layout = resources.bundle->layout,
                                                             .form_row_height = 24,
                                                             .diagnostic_row_height = 16,
                                                             .menu = {},
                                                             .runtime_executable = {}});
    REQUIRE(controller.Resize(1280, 720));
    REQUIRE(controller.OpenProject(root));

    const auto project = controller.panel_bounds("workspace.project");
    REQUIRE(project);
    const auto navigation_document =
        controller.PointerDown(project->x + 20, project->y + 24.0f * 5.0f + 12.0f);
    REQUIRE(navigation_document.changed);
    REQUIRE(controller.state() != nullptr);
    REQUIRE(controller.state()->navigation);

    const auto scene = controller.panel_bounds("workspace.scene");
    REQUIRE(scene);
    auto navigation = *controller.state()->navigation;
    jrpgmaker::editor::LayoutNavigation(navigation, *scene);
    const auto& first_cell = navigation.cells.front().bounds;
    const auto selected = controller.PointerDown(first_cell.x + first_cell.width * 0.5f,
                                                 first_cell.y + first_cell.height * 0.5f);
    REQUIRE(selected.changed);
    REQUIRE(controller.state()->selection == jrpgmaker::editor::SelectionTarget{"core.navigation",
                                                                                "/walkable/0",
                                                                                "navigation.cell"});

    const bool before = controller.state()->navigation->cells[0].walkable;
    const auto toggled = controller.Dispatch(jrpgmaker::editor::EditorAction::kToggle);
    REQUIRE(toggled.changed);
    REQUIRE(controller.state()->navigation->cells[0].walkable == !before);
    REQUIRE(controller.state()->dirty);
    REQUIRE_FALSE(controller.state()->diff.changes.empty());

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor workspace controller bounds large grids without dropping status",
          "[editor][navigation][ui context]") {
    const auto root = MakeFixture("_large_navigation");
    {
        std::ifstream input(root / "assets/data/navigation_demo.json");
        nlohmann::json navigation;
        input >> navigation;
        navigation["width"] = 80;
        navigation["height"] = 80;
        navigation["walkable"] = std::vector<bool>(6400, true);
        std::ofstream output(root / "assets/data/navigation_demo.json", std::ios::trunc);
        output << navigation.dump(2);
    }

    const auto resources =
        jrpgmaker::ui::LoadEditorResources(std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_DIR));
    REQUIRE(resources);
    jrpgmaker::editor::EditorWorkspaceController controller({.layout = resources.bundle->layout,
                                                             .form_row_height = 24,
                                                             .diagnostic_row_height = 16,
                                                             .menu = {},
                                                             .runtime_executable = {}});
    REQUIRE(controller.Resize(1280, 720));
    REQUIRE(controller.OpenProject(root));
    const auto project = controller.panel_bounds("workspace.project");
    REQUIRE(project);
    REQUIRE(controller.PointerDown(project->x + 20, project->y + 24.0f * 5.0f + 12.0f).changed);

    const auto draw_list = controller.BuildDrawList();
    REQUIRE(draw_list.size() < jrpgmaker::ui::DrawList::kMaxPrimitives);
    const auto status = std::find_if(
        draw_list.primitives().begin(), draw_list.primitives().end(), [](const auto& primitive) {
            const auto* text = std::get_if<jrpgmaker::ui::DrawText>(&primitive);
            return text != nullptr && text->text_key == "editor.status.clean";
        });
    REQUIRE(status != draw_list.primitives().end());

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor session applies selected adapter field and commits through workspace",
          "[editor]") {
    const auto root = MakeFixture();
    jrpgmaker::editor::EditorSession session(root);
    REQUIRE(session.Open());
    REQUIRE_FALSE(session.state().form.fields.empty());
    REQUIRE(session.ApplySelected("project.session"));
    REQUIRE(session.state().dirty);
    REQUIRE(session.Save());
    REQUIRE_FALSE(session.state().dirty);
    std::ifstream input(root / "project.json");
    nlohmann::json manifest;
    input >> manifest;
    REQUIRE(manifest["id"] == "project.session");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor session can switch projects through the path open contract", "[editor]") {
    const auto first = MakeFixture("_first");
    const auto second = MakeFixture("_second");
    jrpgmaker::editor::EditorSession session(first);
    REQUIRE(session.Open());
    REQUIRE(session.ApplySelected("project.first"));
    REQUIRE(session.Open(second));
    REQUIRE(session.state().open);
    REQUIRE(session.state().form.fields.front().value == "project.demo");
    std::error_code error;
    std::filesystem::remove_all(first, error);
    std::filesystem::remove_all(second, error);
}

TEST_CASE("editor session routes string text input through the typed edit contract", "[editor]") {
    const auto root = MakeFixture();
    jrpgmaker::editor::EditorSession session(root);
    REQUIRE(session.Open());
    REQUIRE(session.ApplySelectedText("project.text"));
    REQUIRE(session.state().form.fields.front().value == "project.text");
    REQUIRE(session.Save());
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor session switches to an adapter-backed document", "[editor]") {
    const auto root = MakeFixture();
    jrpgmaker::editor::EditorSession session(root);
    REQUIRE(session.Open());
    REQUIRE(session.SelectDocument("core.navigation"));
    REQUIRE(session.state().form.document_id == "core.navigation");
    REQUIRE(session.state().form.fields.front().path == "/width");
    REQUIRE(session.SelectField(2));
    auto walkable = session.state().form.fields[2].value;
    walkable[0] = false;
    REQUIRE(session.ApplySelected(walkable));
    REQUIRE(session.Save());
    std::ifstream input(root / "assets/data/navigation_demo.json");
    nlohmann::json navigation;
    input >> navigation;
    REQUIRE(navigation["walkable"][0] == false);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor navigation cell selection edits and survives atomic reopen",
          "[editor][selection][navigation]") {
    const auto root = MakeFixture("_navigation_selection");
    jrpgmaker::editor::EditorSession session(root);
    REQUIRE(session.Open());
    REQUIRE(session.SelectDocument("core.navigation"));
    REQUIRE(session.state().navigation);
    REQUIRE(session.SelectNavigationCell(0));
    REQUIRE(session.state().selection == jrpgmaker::editor::SelectionTarget{
                                             "core.navigation", "/walkable/0", "navigation.cell"});
    const bool original = session.state().navigation->cells[0].walkable;
    REQUIRE(session.ToggleSelectedNavigationWalkable());
    REQUIRE(session.state().navigation->cells[0].walkable == !original);
    REQUIRE(session.state().dirty);
    REQUIRE_FALSE(session.state().diff.changes.empty());
    REQUIRE(session.Save());
    REQUIRE(session.state().diff.changes.empty());

    jrpgmaker::editor::EditorSession reopened(root);
    REQUIRE(reopened.Open());
    REQUIRE(reopened.SelectDocument("core.navigation"));
    REQUIRE(reopened.state().navigation->cells[0].walkable == !original);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor session preserves plugin load diagnostics across navigation edit and save",
          "[editor][plugin][p13]") {
    const auto root = MakeFixture("_plugin_diagnostics_persistence");
    std::error_code error;
    const auto write_manifest = [&root](std::string_view id, std::string_view data_roots) {
        const auto plugin_root = root / "plugins" / std::string(id);
        std::filesystem::create_directories(plugin_root);
        std::ofstream manifest(plugin_root / "plugin.json");
        manifest << "{\"schema\":1,\"id\":\"" << id
                 << "\",\"type\":\"battle\",\"version\":1,"
                    "\"engine_contract\":1,\"data_roots\":"
                 << data_roots << ",\"capabilities\":[]}\n";
    };
    write_manifest("sample.unlit", "[]");
    write_manifest("sample.style", "[]");
    write_manifest("sample.instant", "[\"plugins/sample_instant/data\"]");
    write_manifest("sample.turn_based", "[\"plugins/sample_turn_based/data\"]");
    std::filesystem::create_directories(root / "plugins/sample_instant/data");
    std::filesystem::create_directories(root / "plugins/sample_turn_based/data");
    std::ofstream(root / "plugins/sample_instant/data/encounters_demo.json") << "{}\n";
    std::ofstream(root / "plugins/sample_turn_based/data/encounters_demo.json") << "{}\n";

    const std::vector<std::pair<std::string, std::string>> expected = {
        {"editor.plugin.sidecar_missing",
         (root / "plugins" / "sample.unlit" / "plugin.editor.json").string()},
        {"editor.plugin.sidecar_missing",
         (root / "plugins" / "sample.style" / "plugin.editor.json").string()},
        {"editor.plugin.sidecar_missing",
         (root / "plugins" / "sample.instant" / "plugin.editor.json").string()},
        {"editor.document.read_only", "plugins/sample_instant/data/encounters_demo.json"},
        {"editor.plugin.sidecar_missing",
         (root / "plugins" / "sample.turn_based" / "plugin.editor.json").string()},
        {"editor.document.read_only", "plugins/sample_turn_based/data/encounters_demo.json"},
    };
    jrpgmaker::editor::EditorSession session(root);
    const auto require_diagnostics =
        [&](const std::vector<std::pair<std::string, std::string>>& expected_diagnostics,
            bool dirty, std::size_t changes) {
            std::vector<std::pair<std::string, std::string>> actual;
            for (const auto& diagnostic : session.state().diagnostics)
                actual.emplace_back(diagnostic.code, diagnostic.path);
            const auto describe = [](const auto& diagnostics) {
                std::string result;
                for (const auto& [code, path] : diagnostics)
                    result += code + "@" + path + "|";
                return result;
            };
            INFO("actual=" << describe(actual) << " expected=" << describe(expected_diagnostics));
            REQUIRE(actual == expected_diagnostics);
            REQUIRE(session.state().dirty == dirty);
            REQUIRE(session.state().diff.changes.size() == changes);
        };

    REQUIRE(session.Open());
    require_diagnostics(expected, false, 0);
    REQUIRE(session.SelectDocument("core.navigation"));
    require_diagnostics(expected, false, 0);
    REQUIRE(session.SelectNavigationCell(7));
    REQUIRE(session.ToggleSelectedNavigationWalkable());
    require_diagnostics(expected, true, 1);

    const auto temporary = root / "assets/data/navigation_demo.json.tmp";
    std::ofstream(temporary) << "{}\n";
    REQUIRE_FALSE(session.Save());
    auto expected_save_failure = expected;
    expected_save_failure.emplace_back("project.save.temporary_exists", temporary.string());
    require_diagnostics(expected_save_failure, true, 1);

    error.clear();
    REQUIRE(std::filesystem::remove(temporary, error));
    REQUIRE_FALSE(error);
    REQUIRE(session.Save());
    require_diagnostics(expected, false, 0);

    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor navigation selection ignores text input owned by form fields",
          "[editor][selection][navigation][ui text]") {
    const auto root = MakeFixture("_navigation_text_input");
    jrpgmaker::editor::EditorSession session(root);
    REQUIRE(session.Open());
    REQUIRE(session.SelectDocument("core.navigation"));
    REQUIRE(session.SelectNavigationCell(0));
    const auto selection = session.state().selection;
    const auto startup_diagnostics = session.state().diagnostics;
    REQUIRE_FALSE(session.ApplySelectedText(" "));
    REQUIRE(session.state().diagnostics.size() == startup_diagnostics.size());
    for (std::size_t index = 0; index < startup_diagnostics.size(); ++index) {
        REQUIRE(session.state().diagnostics[index].code == startup_diagnostics[index].code);
        REQUIRE(session.state().diagnostics[index].path == startup_diagnostics[index].path);
    }
    REQUIRE(session.state().selection == selection);

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor navigation selection has stable identity and clears on document switch",
          "[editor][selection][navigation]") {
    const auto root = MakeFixture("_selection_identity");
    jrpgmaker::editor::EditorSession session(root);
    REQUIRE(session.Open());
    REQUIRE(session.SelectDocument("core.navigation"));
    REQUIRE(session.SelectNavigationCell(7));
    const jrpgmaker::editor::SelectionTarget expected{"core.navigation", "/walkable/7",
                                                      "navigation.cell"};
    REQUIRE(session.state().selection == expected);
    REQUIRE_FALSE(session.SelectNavigationCell(session.state().navigation->cells.size()));
    REQUIRE(session.state().selection == expected);
    REQUIRE(session.SelectDocument("project.manifest"));
    REQUIRE(session.state().selection ==
            jrpgmaker::editor::SelectionTarget{"project.manifest", "/", "document"});

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor workspace controller exposes project open failures as structured diagnostics",
          "[editor][diagnostics]") {
    const auto resources =
        jrpgmaker::ui::LoadEditorResources(std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_DIR));
    REQUIRE(resources);
    jrpgmaker::editor::EditorWorkspaceController controller({.layout = resources.bundle->layout,
                                                             .form_row_height = 24,
                                                             .diagnostic_row_height = 16,
                                                             .menu = {},
                                                             .runtime_executable = {}});
    REQUIRE(controller.Resize(1280, 720));
    const auto missing =
        std::filesystem::temp_directory_path() / "jrpgmaker_editor_missing_project_for_diagnostics";
    REQUIRE_FALSE(controller.OpenProject(missing));
    REQUIRE(controller.state() != nullptr);
    REQUIRE_FALSE(controller.state()->diagnostics.empty());

    const auto draw_list = controller.BuildDrawList();
    const auto diagnostic = std::find_if(
        draw_list.primitives().begin(), draw_list.primitives().end(), [](const auto& primitive) {
            const auto* text = std::get_if<jrpgmaker::ui::DrawText>(&primitive);
            return text != nullptr && text->text_key == "editor.diagnostic.code" &&
                   text->arguments.at("code") == "project.file.open";
        });
    REQUIRE(diagnostic != draw_list.primitives().end());
}

TEST_CASE("editor workspace controller redraws after a rejected document command",
          "[editor][diagnostics][menu]") {
    const auto root = MakeFixture("_document_command_failure");
    std::error_code error;
    const auto resources =
        jrpgmaker::ui::LoadEditorResources(std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_DIR));
    REQUIRE(resources);
    jrpgmaker::editor::EditorWorkspaceController controller({.layout = resources.bundle->layout,
                                                             .form_row_height = 24,
                                                             .diagnostic_row_height = 16,
                                                             .menu = {},
                                                             .runtime_executable = {}});
    REQUIRE(controller.Resize(1280, 720));
    REQUIRE(controller.OpenProject(root));
    std::filesystem::remove(root / "assets/data/material_demo.json", error);
    REQUIRE_FALSE(error);

    const auto result = controller.DispatchCommand("document.select", "core.material");
    REQUIRE(result.changed);
    REQUIRE(controller.state() != nullptr);
    REQUIRE_FALSE(controller.state()->diagnostics.empty());

    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor session discovers and edits a plugin sidecar document",
          "[editor][plugin][p13][budget]") {
    const auto root = MakeFixture("_plugin");
    std::error_code error;
    std::filesystem::create_directories(root / "plugins/sample.unlit/editor");
    std::filesystem::create_directories(root / "plugin_data");
    std::filesystem::create_directories(root / "overflow_data");
    {
        std::ofstream manifest(root / "plugins/sample.unlit/plugin.json");
        manifest << R"json({"schema":1,"id":"sample.unlit","type":"render_style",
                            "version":1,"engine_contract":1,
                            "data_roots":["plugin_data","overflow_data"],
                            "capabilities":[]})json";
        std::ofstream sidecar(root / "plugins/sample.unlit/plugin.editor.json");
        sidecar << R"json({"schema":1,"plugin_id":"sample.unlit","editor_contract":1,
                          "documents":[{"type_id":"sample.unlit.document.v1",
                            "roots":["plugin_data","overflow_data"],
                            "descriptor":"editor/document.json"}],
                          "locales":{"en":"editor/en.json"},"icons":"editor/icons.json"})json";
        std::ofstream descriptor(root / "plugins/sample.unlit/editor/document.json");
        descriptor << R"json({"schema":1,"type_id":"sample.unlit.document.v1","fields":[
                              {"path":"/name","value_type":"string","role":"text",
                               "label_key":"plugin.sample.unlit.name","recipe":"input"}]})json";
        std::ofstream locale(root / "plugins/sample.unlit/editor/en.json");
        locale << "{}";
        std::ofstream icons(root / "plugins/sample.unlit/editor/icons.json");
        icons << "{}";
        for (int index = 0; index < 129; ++index)
            std::ofstream(root / "overflow_data" / ("ignored_" + std::to_string(index) + ".txt"));
        std::ofstream document(root / "plugin_data/example.json");
        document << R"json({"schema":1,"name":"before"})json";
    }

    jrpgmaker::editor::EditorSession session(root);
    REQUIRE(session.Open());
    REQUIRE(std::any_of(session.state().diagnostics.begin(), session.state().diagnostics.end(),
                        [](const auto& diagnostic) {
                            return diagnostic.code == "editor.document_root.entry_budget" &&
                                   diagnostic.path == "overflow_data";
                        }));
    const auto tab = std::find_if(
        session.state().tabs.tabs.begin(), session.state().tabs.tabs.end(), [](const auto& item) {
            return item.document_id == "plugin:sample.unlit.document.v1:plugin_data/example.json";
        });
    REQUIRE(tab != session.state().tabs.tabs.end());
    const auto document_id = tab->document_id;
    REQUIRE(session.SelectDocument(document_id));
    REQUIRE(session.state().form.document_id == document_id);
    REQUIRE(session.ApplySelected("after"));
    REQUIRE(session.Save());

    nlohmann::json saved;
    std::ifstream input(root / "plugin_data/example.json");
    input >> saved;
    REQUIRE(saved["name"] == "after");
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor session keeps plugin documents read-only when the descriptor is missing",
          "[editor][plugin][p13][read-only]") {
    const auto root = MakeFixture("_plugin_read_only_descriptor");
    std::error_code error;
    std::filesystem::create_directories(root / "plugins/sample.invalid");
    std::filesystem::create_directories(root / "plugin_data");
    {
        std::ifstream input(root / "project.json");
        nlohmann::json project;
        input >> project;
        project["plugins"].push_back("sample.invalid");
        std::ofstream output(root / "project.json");
        output << project.dump(2);
        std::ofstream manifest(root / "plugins/sample.invalid/plugin.json");
        manifest << R"json({"schema":1,"id":"sample.invalid","type":"render_style",
                            "version":1,"engine_contract":1,"data_roots":["plugin_data"],
                            "capabilities":[]})json";
        std::ofstream sidecar(root / "plugins/sample.invalid/plugin.editor.json");
        sidecar << R"json({"schema":1,"plugin_id":"sample.invalid","editor_contract":1,
                          "documents":[{"type_id":"sample.invalid.document.v1",
                            "roots":["plugin_data"],"descriptor":"editor/missing.json"}],
                          "locales":{},"icons":"editor/icons.json"})json";
        std::ofstream document(root / "plugin_data/example.json");
        document << R"json({"schema":1,"name":"before"})json";
    }

    jrpgmaker::editor::EditorSession session(root);
    REQUIRE(session.Open());
    const auto tab =
        std::find_if(session.state().tabs.tabs.begin(), session.state().tabs.tabs.end(),
                     [](const auto& item) { return item.path == "plugin_data/example.json"; });
    REQUIRE(tab != session.state().tabs.tabs.end());
    REQUIRE_FALSE(tab->editable);
    const auto document_id = tab->document_id;
    REQUIRE(std::any_of(session.state().diagnostic_panel.items.begin(),
                        session.state().diagnostic_panel.items.end(),
                        [&document_id](const auto& item) {
                            return item.document_id == document_id &&
                                   item.diagnostic.code == "editor.document.read_only";
                        }));

    REQUIRE(session.SelectDocument(document_id));
    REQUIRE_FALSE(session.ApplySelected("after"));
    REQUIRE(std::any_of(session.state().diagnostics.begin(), session.state().diagnostics.end(),
                        [](const auto& diagnostic) {
                            return diagnostic.code == "project.edit.document_read_only";
                        }));

    std::ifstream input(root / "plugin_data/example.json");
    nlohmann::json saved;
    input >> saved;
    REQUIRE(saved["name"] == "before");
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor session keeps plugin documents read-only when the sidecar is missing",
          "[editor][plugin][p13][read-only][budget]") {
    const auto root = MakeFixture("_plugin_read_only_sidecar");
    std::error_code error;
    std::filesystem::create_directories(root / "plugins/sample.missing");
    std::filesystem::create_directories(root / "plugin_data");
    {
        std::ifstream input(root / "project.json");
        nlohmann::json project;
        input >> project;
        project["plugins"].push_back("sample.missing");
        std::ofstream output(root / "project.json");
        output << project.dump(2);
        std::ofstream manifest(root / "plugins/sample.missing/plugin.json");
        manifest << R"json({"schema":1,"id":"sample.missing","type":"render_style",
                            "version":1,"engine_contract":1,"data_roots":["plugin_data"],
                            "capabilities":[]})json";
        std::ofstream document(root / "plugin_data/example.json");
        document << R"json({"schema":1,"name":"before"})json";
        auto depth = root / "plugin_data";
        for (int index = 0; index < 17; ++index) {
            depth /= "nested" + std::to_string(index);
            std::filesystem::create_directory(depth, error);
        }
    }

    jrpgmaker::editor::EditorSession session(root);
    REQUIRE(session.Open());
    REQUIRE(std::any_of(session.state().diagnostics.begin(), session.state().diagnostics.end(),
                        [](const auto& diagnostic) {
                            return diagnostic.code == "editor.document_root.depth_budget" &&
                                   diagnostic.path == "plugin_data";
                        }));
    const auto tab =
        std::find_if(session.state().tabs.tabs.begin(), session.state().tabs.tabs.end(),
                     [](const auto& item) { return item.path == "plugin_data/example.json"; });
    REQUIRE(tab != session.state().tabs.tabs.end());
    REQUIRE_FALSE(tab->editable);
    REQUIRE(std::any_of(
        session.state().diagnostics.begin(), session.state().diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.code == "editor.plugin.sidecar_missing"; }));
    REQUIRE(session.SelectDocument(tab->document_id));
    REQUIRE_FALSE(session.ApplySelectedText("after"));
    REQUIRE(std::any_of(session.state().diagnostics.begin(), session.state().diagnostics.end(),
                        [](const auto& diagnostic) {
                            return diagnostic.code == "project.edit.document_read_only";
                        }));
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor session keeps plugin documents read-only when the descriptor is invalid",
          "[editor][plugin][p13][read-only]") {
    const auto root = MakeFixture("_plugin_read_only_invalid_descriptor");
    std::error_code error;
    std::filesystem::create_directories(root / "plugins/sample.invalid/editor");
    std::filesystem::create_directories(root / "plugin_data");
    {
        std::ifstream input(root / "project.json");
        nlohmann::json project;
        input >> project;
        project["plugins"].push_back("sample.invalid");
        std::ofstream output(root / "project.json");
        output << project.dump(2);
        std::ofstream manifest(root / "plugins/sample.invalid/plugin.json");
        manifest << R"json({"schema":1,"id":"sample.invalid","type":"render_style",
                            "version":1,"engine_contract":1,"data_roots":["plugin_data"],
                            "capabilities":[]})json";
        std::ofstream sidecar(root / "plugins/sample.invalid/plugin.editor.json");
        sidecar << R"json({"schema":1,"plugin_id":"sample.invalid","editor_contract":1,
                          "documents":[{"type_id":"sample.invalid.document.v1",
                            "roots":["plugin_data"],"descriptor":"editor/document.json"}],
                          "locales":{},"icons":"editor/icons.json"})json";
        std::ofstream descriptor(root / "plugins/sample.invalid/editor/document.json");
        descriptor << R"json({"schema":1,"type_id":"sample.invalid.document.v1","fields":[]})json";
        std::ofstream icons(root / "plugins/sample.invalid/editor/icons.json");
        icons << "{}";
        std::ofstream document(root / "plugin_data/example.json");
        document << R"json({"schema":1,"name":"before"})json";
    }

    jrpgmaker::editor::EditorSession session(root);
    REQUIRE(session.Open());
    const auto tab =
        std::find_if(session.state().tabs.tabs.begin(), session.state().tabs.tabs.end(),
                     [](const auto& item) { return item.path == "plugin_data/example.json"; });
    REQUIRE(tab != session.state().tabs.tabs.end());
    REQUIRE_FALSE(tab->editable);
    REQUIRE(std::any_of(
        session.state().diagnostics.begin(), session.state().diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.code == "editor_descriptor.fields"; }));
    REQUIRE(session.SelectDocument(tab->document_id));
    REQUIRE_FALSE(session.ApplySelected("after"));
    REQUIRE(std::any_of(session.state().diagnostics.begin(), session.state().diagnostics.end(),
                        [](const auto& diagnostic) {
                            return diagnostic.code == "project.edit.document_read_only";
                        }));
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor session atomically adjusts navigation dimensions", "[editor]") {
    const auto root = MakeFixture();
    jrpgmaker::editor::EditorSession session(root);
    REQUIRE(session.Open());
    REQUIRE(session.SelectDocument("core.navigation"));
    REQUIRE(session.SelectField(0));
    const auto original = session.state().form.fields[0].value.get<std::int64_t>();
    REQUIRE(session.AdjustSelectedInteger(1));
    REQUIRE(session.state().form.fields[0].value == original + 1);
    REQUIRE(session.state().diff.changes.size() == 2);
    REQUIRE(session.AdjustSelectedInteger(-1));
    REQUIRE(session.state().form.fields[0].value == original);
    REQUIRE_FALSE(session.AdjustSelectedInteger(2));
    REQUIRE(session.Save());
    std::ifstream input(root / "assets/data/navigation_demo.json");
    nlohmann::json navigation;
    input >> navigation;
    REQUIRE(navigation["width"] == original);
    REQUIRE(navigation["walkable"].size() == 25);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor session commits integer text through the adapter contract", "[editor]") {
    const auto root = MakeFixture();
    jrpgmaker::editor::EditorSession session(root);
    REQUIRE(session.Open());
    REQUIRE(session.SelectDocument("core.navigation"));
    REQUIRE(session.SelectField(0));
    REQUIRE(session.ApplySelectedText("6"));
    REQUIRE(session.state().form.fields[0].value == 6);
    REQUIRE(session.state().form.fields[2].value.size() == 30);
    REQUIRE(session.Save());
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor session appends text after the initial field selection is replaced", "[editor]") {
    const auto root = MakeFixture();
    jrpgmaker::editor::EditorSession session(root);
    REQUIRE(session.Open());
    REQUIRE(session.ApplySelectedText("project"));
    REQUIRE(session.ApplySelectedText(".text"));
    REQUIRE(session.state().form.fields.front().value == "project.text");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor session forwards IME composition without committing project data", "[editor]") {
    const auto root = MakeFixture();
    jrpgmaker::editor::EditorSession session(root);
    REQUIRE(session.Open());
    REQUIRE(session.ApplySelectedComposition("かな"));
    REQUIRE(session.state().form.fields.front().value == "project.demo");
    REQUIRE_FALSE(session.state().dirty);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor session treats an unchanged save as successful", "[editor]") {
    const auto root = MakeFixture();
    jrpgmaker::editor::EditorSession session(root);
    REQUIRE(session.Open());
    REQUIRE(session.Save());
    REQUIRE_FALSE(session.state().dirty);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor session field navigation wraps deterministically", "[editor]") {
    const auto root = MakeFixture();
    jrpgmaker::editor::EditorSession session(root);
    REQUIRE(session.Open());
    REQUIRE(session.state().selected_field == 0);
    REQUIRE(session.SelectPrevious());
    REQUIRE(session.state().selected_field == session.state().form.fields.size() - 1);
    REQUIRE(session.SelectNext());
    REQUIRE(session.state().selected_field == 0);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor session selects a valid field from a layout hit test", "[editor]") {
    const auto root = MakeFixture();
    jrpgmaker::editor::EditorSession session(root);
    REQUIRE(session.Open());
    REQUIRE(session.SelectField(1));
    REQUIRE(session.state().selected_field == 1);
    REQUIRE_FALSE(session.SelectField(session.state().form.fields.size()));
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor session cycles an adapter-provided select field", "[editor]") {
    const auto root = MakeFixture();
    auto adapters = jrpgmaker::project::CreateDefaultDocumentAdapters();
    auto* manifest = adapters.Find("project.manifest");
    REQUIRE(manifest != nullptr);
    REQUIRE(manifest->fields.size() >= 3);
    manifest->fields[2].value_type = "select";
    manifest->fields[2].choices = {"sample.instant", "sample.turn_based"};
    jrpgmaker::editor::EditorSession session(root, std::move(adapters));
    REQUIRE(session.Open());
    REQUIRE(session.SelectField(2));
    REQUIRE(session.CycleSelectedChoice(1));
    REQUIRE(session.state().form.fields[2].value == "sample.turn_based");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor preview process rejects unsafe launch inputs", "[editor]") {
    jrpgmaker::editor::PreviewProcess process;
    REQUIRE_FALSE(process.Start({}, std::filesystem::temp_directory_path()));
    REQUIRE_FALSE(process.state().running);
    REQUIRE(process.state().error == "editor.preview.executable_or_project_invalid");
}

TEST_CASE("editor session stop preview synchronizes the projected process state",
          "[editor][preview]") {
    const auto root = MakeFixture("_preview_stop");
    jrpgmaker::editor::EditorSession session(root);
    REQUIRE(session.Open());
    REQUIRE(session.StartPreview(std::filesystem::path(JRPGMAKER_RUNTIME_EXECUTABLE)));
    REQUIRE(session.state().preview.process_running);

    session.StopPreview();

    REQUIRE_FALSE(session.state().preview.process_running);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}
