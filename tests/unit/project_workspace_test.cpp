#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

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
