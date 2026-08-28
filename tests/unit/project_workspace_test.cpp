#include <catch2/catch_test_macros.hpp>

#include <filesystem>

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
