#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "jrpgmaker/project/workspace.hpp"

namespace {

std::filesystem::path MakeFixture() {
    const auto root = std::filesystem::temp_directory_path() / "jrpgmaker_workspace_fixture";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "assets/data");
    std::filesystem::copy(std::filesystem::path(JRPGMAKER_ASSET_DIR) / "data", root / "assets/data",
                          std::filesystem::copy_options::recursive, error);
    std::filesystem::copy_file(std::filesystem::path(JRPGMAKER_ASSET_DIR) / "data/project_demo.json",
                               root / "project.json", std::filesystem::copy_options::overwrite_existing,
                               error);
    return root;
}

} // namespace

TEST_CASE("project workspace opens and diagnoses the committed demo", "[project][editor]") {
    const auto root = MakeFixture();
    jrpgmaker::project::ProjectWorkspace workspace(root);
    const auto opened = workspace.Open();
    REQUIRE(opened);
    const auto diagnosed = workspace.Diagnose(*opened.snapshot);
    REQUIRE(diagnosed);
    REQUIRE(diagnosed.event_count > 0);
    REQUIRE(diagnosed.navigation_width > 0);
    REQUIRE(diagnosed.camera_region_count > 0);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("project workspace discovers map documents from manifest paths", "[project][editor]") {
    const auto root = MakeFixture();
    nlohmann::json manifest;
    {
        std::ifstream input(root / "project.json");
        input >> manifest;
    }
    const auto data = root / "assets/data";
    const std::array<std::pair<const char*, const char*>, 4> paths = {
        std::pair{"navigation", "assets/data/navigation_custom.json"},
        std::pair{"collision", "assets/data/collision_custom.json"},
        std::pair{"camera", "assets/data/camera_custom.json"},
        std::pair{"interaction", "assets/data/interaction_custom.json"}};
    for (const auto& [field, path] : paths) {
        const auto source = data / (std::string(field) + "_demo.json");
        std::filesystem::copy_file(source, root / path,
                                    std::filesystem::copy_options::overwrite_existing);
        manifest[field] = path;
    }
    {
        std::ofstream output(root / "project.json", std::ios::trunc);
        output << manifest.dump(2) << '\n';
    }

    jrpgmaker::project::ProjectWorkspace workspace(root);
    const auto opened = workspace.Open();
    REQUIRE(opened);
    REQUIRE(workspace.Diagnose(*opened.snapshot));

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("project workspace returns bounded structured diagnostics", "[project][editor]") {
    const auto root = std::filesystem::temp_directory_path() / "jrpgmaker_missing_workspace";
    jrpgmaker::project::ProjectWorkspace workspace(root);
    const auto opened = workspace.Open();
    REQUIRE_FALSE(opened);
    REQUIRE_FALSE(opened.diagnostics.empty());
    REQUIRE(opened.diagnostics.front().code == "project.file.open");
}

TEST_CASE("project workspace applies an edit and prepares a stable save plan", "[project][editor]") {
    const auto root = MakeFixture();
    jrpgmaker::project::ProjectWorkspace workspace(root);
    REQUIRE(workspace.Open());
    const auto edit = workspace.Apply({"project.manifest", "/id", "project.edited"});
    REQUIRE(edit);
    REQUIRE(edit.revision == 1);
    REQUIRE(edit.changes.size() == 1);
    REQUIRE(edit.changes.front().field_path == "/id");
    REQUIRE(edit.changes.front().before == "project.demo");
    REQUIRE(edit.changes.front().after == "project.edited");
    const auto plan = workspace.PrepareSave(edit.revision);
    REQUIRE(plan);
    REQUIRE(plan.token->revision == edit.revision);
    REQUIRE(plan.changes.size() == 1);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("project workspace selects and saves an adapter-backed data document", "[project][editor]") {
    const auto root = MakeFixture();
    jrpgmaker::project::ProjectWorkspace workspace(root);
    const auto opened = workspace.Open();
    REQUIRE(opened);
    REQUIRE(workspace.SelectDocument("core.navigation").empty());
    REQUIRE(workspace.CurrentDocumentId() == "core.navigation");
    REQUIRE(workspace.CurrentDocument()["width"] == 5);
    auto walkable = workspace.CurrentDocument()["walkable"];
    walkable[0] = false;
    const auto edit = workspace.Apply({"core.navigation", "/walkable", walkable});
    REQUIRE(edit);
    REQUIRE_FALSE(workspace.SelectDocument("project.manifest").empty());
    const auto plan = workspace.PrepareSave(edit.revision);
    REQUIRE(plan);
    REQUIRE(workspace.Commit(*plan.token));
    std::ifstream input(root / "assets/data/navigation_demo.json");
    nlohmann::json navigation;
    input >> navigation;
    REQUIRE(navigation["walkable"][0] == false);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("project workspace diagnosis validates localization coverage", "[project][editor]") {
    const auto root = MakeFixture();
    nlohmann::json localization;
    {
        std::ifstream input(root / "assets/data/localization_en.json");
        input >> localization;
    }
    localization["strings"].erase("intro.welcome");
    {
        std::ofstream output(root / "assets/data/localization_en.json", std::ios::trunc);
        output << localization.dump(2) << '\n';
    }
    jrpgmaker::project::ProjectWorkspace workspace(root);
    const auto opened = workspace.Open();
    REQUIRE(opened);
    const auto diagnosis = workspace.Diagnose(*opened.snapshot);
    REQUIRE_FALSE(diagnosis);
    REQUIRE_FALSE(diagnosis.diagnostics.empty());
    REQUIRE(diagnosis.diagnostics.back().code == "project.localization.missing_key");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("project workspace rejects stale save and preserves the source file", "[project][editor]") {
    const auto root = MakeFixture();
    jrpgmaker::project::ProjectWorkspace workspace(root);
    REQUIRE(workspace.Open());
    REQUIRE(workspace.Apply({"project.manifest", "/id", "project.edited"}));
    const auto stale = workspace.PrepareSave(0);
    REQUIRE_FALSE(stale);
    REQUIRE(std::ifstream(root / "project.json").good());
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("project workspace commits through a temporary file and backup", "[project][editor]") {
    const auto root = MakeFixture();
    jrpgmaker::project::ProjectWorkspace workspace(root);
    REQUIRE(workspace.Open());
    const auto edit = workspace.Apply({"project.manifest", "/id", "project.committed"});
    REQUIRE(edit);
    const auto plan = workspace.PrepareSave(edit.revision);
    REQUIRE(plan);
    const auto committed = workspace.Commit(*plan.token);
    REQUIRE(committed);
    REQUIRE(committed.backup.filename() == "project.json.bak");
    std::ifstream manifest(root / "project.json");
    nlohmann::json document;
    manifest >> document;
    REQUIRE(document["id"] == "project.committed");
    REQUIRE(std::filesystem::exists(committed.backup));
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("project workspace reports schema one migration as a no-op", "[project][editor]") {
    const auto root = MakeFixture();
    jrpgmaker::project::ProjectWorkspace workspace(root);
    REQUIRE(workspace.Open());
    const auto migration = workspace.Migrate();
    REQUIRE(migration);
    REQUIRE(migration.from_schema == 1);
    REQUIRE(migration.to_schema == 1);
    REQUIRE_FALSE(migration.changed);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("document adapter registry rejects duplicates and validates documents", "[project][editor]") {
    jrpgmaker::project::DocumentAdapterRegistry registry;
    auto adapter = jrpgmaker::project::DocumentAdapter{
        .type_id = "calendar",
        .fields = {{.path = "/id", .value_type = "string", .label_key = "editor.calendar.id",
                    .recipe = "", .required = false, .read_only = false, .choices = {}}},
        .validate = [](const nlohmann::json& document) {
            if (!document.is_object())
                return std::vector<jrpgmaker::project::Diagnostic>{{"document.object_required", "/"}};
            return std::vector<jrpgmaker::project::Diagnostic>{};
        },
        .normalize_edit = {}};
    REQUIRE(registry.Register(adapter));
    REQUIRE_FALSE(registry.Register(adapter));
    REQUIRE(registry.Validate("calendar", nlohmann::json::object()));
    REQUIRE_FALSE(registry.Validate("calendar", nlohmann::json::array()));
    REQUIRE_FALSE(registry.Validate("missing", nlohmann::json::object()));
}

TEST_CASE("document adapter registry validates bounded select choices", "[project][editor]") {
    jrpgmaker::project::DocumentAdapterRegistry registry;
    const auto adapter = jrpgmaker::project::DocumentAdapter{
        .type_id = "select.document",
        .fields = {{.path = "/style",
                    .value_type = "select",
                    .label_key = "editor.style",
                    .recipe = "select",
                    .required = true,
                    .read_only = false,
                    .choices = {"one", "one"}}},
        .validate = [](const nlohmann::json&) {
            return std::vector<jrpgmaker::project::Diagnostic>{};
        },
        .normalize_edit = {}};
    const auto result = registry.Register(adapter);
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(result.diagnostics.empty());
    REQUIRE(result.diagnostics.front().code == "project.adapter.duplicate_choice");
}

TEST_CASE("default document adapters cover the domain workspace documents", "[project][editor]") {
    const auto registry = jrpgmaker::project::CreateDefaultDocumentAdapters();
    REQUIRE(registry.size() == 10);
    REQUIRE(registry.Find("domain.event_script") != nullptr);
    REQUIRE(registry.Find("core.navigation") != nullptr);
    REQUIRE(registry.Find("core.collision") != nullptr);
    REQUIRE(registry.Find("core.camera") != nullptr);
    REQUIRE(registry.Find("domain.interaction") != nullptr);
    REQUIRE(registry.Find("core.material") != nullptr);
    REQUIRE(registry.Find("app.input_actions") != nullptr);
    REQUIRE(registry.Find("domain.localization") != nullptr);
    REQUIRE(registry.Find("project.resources") != nullptr);
}
