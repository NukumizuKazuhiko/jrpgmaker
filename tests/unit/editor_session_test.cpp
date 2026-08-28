#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "jrpgmaker/editor/editor_session.hpp"

namespace {

std::filesystem::path MakeFixture() {
    const auto root = std::filesystem::temp_directory_path() / "jrpgmaker_editor_session_fixture";
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
