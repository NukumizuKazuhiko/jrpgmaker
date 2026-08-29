#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "jrpgmaker/editor/editor_session.hpp"
#include "jrpgmaker/editor/preview_process.hpp"

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
