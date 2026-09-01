#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/editor/editor_user_settings.hpp"

namespace {

class SettingsFixture final {
public:
    SettingsFixture()
        : root_(std::filesystem::temp_directory_path() / "jrpgmaker_editor_user_settings_test") {
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
    }

    ~SettingsFixture() { std::filesystem::remove_all(root_); }

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

    void Write(const std::filesystem::path& path, std::string contents) const {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        REQUIRE(file.is_open());
        file << contents;
    }

private:
    std::filesystem::path root_;
};

bool HasCode(const std::vector<jrpgmaker::editor::EditorUserSettingsDiagnostic>& diagnostics,
             std::string_view code) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [code](const auto& diagnostic) { return diagnostic.code == code; });
}

} // namespace

TEST_CASE("editor user settings use defaults when preference file is absent",
          "[editor][settings]") {
    SettingsFixture fixture;
    const auto result = jrpgmaker::editor::LoadEditorUserSettings(fixture.root() / "missing.json");

    REQUIRE(result.settings == jrpgmaker::editor::EditorUserSettings{});
    REQUIRE(result.used_defaults);
    REQUIRE(HasCode(result.diagnostics, "editor.settings.file_missing"));
}

TEST_CASE("editor user settings round trip stable splitter ids", "[editor][settings]") {
    SettingsFixture fixture;
    const auto path = fixture.root() / "settings.json";
    const jrpgmaker::editor::EditorUserSettings expected{
        .splitter_left_center = 0.31f,
        .splitter_scene_inspector = 0.74f,
        .splitter_diagnostics = 0.68f,
    };

    const auto saved = jrpgmaker::editor::SaveEditorUserSettings(path, expected);
    REQUIRE(saved.ok);
    const auto loaded = jrpgmaker::editor::LoadEditorUserSettings(path);

    REQUIRE_FALSE(loaded.used_defaults);
    REQUIRE(loaded.diagnostics.empty());
    REQUIRE(loaded.settings.splitter_left_center == Catch::Approx(0.31f));
    REQUIRE(loaded.settings.splitter_scene_inspector == Catch::Approx(0.74f));
    REQUIRE(loaded.settings.splitter_diagnostics == Catch::Approx(0.68f));
}

TEST_CASE("editor user settings reject malformed values and fall back atomically",
          "[editor][settings]") {
    const std::vector<std::pair<std::string, std::string>> invalid_documents = {
        {"bad-json", "{"},
        {"non-number",
         R"({"schema":1,"splitters":{"splitter-left-center":"wide","splitter-scene-inspector":0.8,"splitter-diagnostics":0.86}})"},
        {"out-of-range",
         R"({"schema":1,"splitters":{"splitter-left-center":0.9,"splitter-scene-inspector":0.8,"splitter-diagnostics":0.86}})"},
        {"nan",
         R"({"schema":1,"splitters":{"splitter-left-center":NaN,"splitter-scene-inspector":0.8,"splitter-diagnostics":0.86}})"},
        {"missing",
         R"({"schema":1,"splitters":{"splitter-left-center":0.22,"splitter-scene-inspector":0.8}})"},
    };

    for (const auto& [name, document] : invalid_documents) {
        SettingsFixture fixture;
        const auto path = fixture.root() / (name + ".json");
        fixture.Write(path, document);
        const auto result = jrpgmaker::editor::LoadEditorUserSettings(path);

        INFO(name);
        REQUIRE(result.used_defaults);
        REQUIRE(result.settings == jrpgmaker::editor::EditorUserSettings{});
        REQUIRE_FALSE(result.diagnostics.empty());
    }
}

TEST_CASE("editor user settings ignore unknown fields with a structured diagnostic",
          "[editor][settings]") {
    SettingsFixture fixture;
    const auto path = fixture.root() / "unknown.json";
    fixture.Write(
        path,
        R"({"schema":1,"future":true,"splitters":{"splitter-left-center":0.22,"splitter-scene-inspector":0.8,"splitter-diagnostics":0.86,"future-splitter":0.5}})");

    const auto result = jrpgmaker::editor::LoadEditorUserSettings(path);

    REQUIRE_FALSE(result.used_defaults);
    REQUIRE(result.settings == jrpgmaker::editor::EditorUserSettings{});
    REQUIRE(HasCode(result.diagnostics, "editor.settings.field_unknown"));
}

TEST_CASE("editor user settings preserve old file when temporary replacement fails",
          "[editor][settings]") {
    SettingsFixture fixture;
    const auto path = fixture.root() / "settings.json";
    const jrpgmaker::editor::EditorUserSettings original{
        .splitter_left_center = 0.29f,
        .splitter_scene_inspector = 0.76f,
        .splitter_diagnostics = 0.71f,
    };
    REQUIRE(jrpgmaker::editor::SaveEditorUserSettings(path, original).ok);
    std::filesystem::create_directory(path.string() + ".tmp");

    const auto result =
        jrpgmaker::editor::SaveEditorUserSettings(path, jrpgmaker::editor::EditorUserSettings{});

    REQUIRE_FALSE(result.ok);
    const auto loaded = jrpgmaker::editor::LoadEditorUserSettings(path);
    REQUIRE_FALSE(loaded.used_defaults);
    REQUIRE(loaded.settings.splitter_left_center == Catch::Approx(0.29f));
    REQUIRE(loaded.settings.splitter_scene_inspector == Catch::Approx(0.76f));
    REQUIRE(loaded.settings.splitter_diagnostics == Catch::Approx(0.71f));
}

TEST_CASE("editor user settings schema v2 round trips panel visibility", "[editor][settings]") {
    SettingsFixture fixture;
    const auto path = fixture.root() / "settings-v2.json";
    const jrpgmaker::editor::EditorUserSettings expected{
        .splitter_left_center = 0.31f,
        .splitter_scene_inspector = 0.74f,
        .splitter_diagnostics = 0.68f,
        .project_visible = false,
        .hierarchy_visible = true,
        .inspector_visible = false,
        .diagnostics_visible = true,
    };
    REQUIRE(jrpgmaker::editor::SaveEditorUserSettings(path, expected).ok);
    const auto loaded = jrpgmaker::editor::LoadEditorUserSettings(path);
    REQUIRE_FALSE(loaded.used_defaults);
    REQUIRE(loaded.settings == expected);
}

TEST_CASE("editor user settings schema v3 round trips active left panel", "[editor][settings]") {
    SettingsFixture fixture;
    const auto path = fixture.root() / "settings-v3.json";
    const jrpgmaker::editor::EditorUserSettings expected{
        .splitter_left_center = 0.31f,
        .splitter_scene_inspector = 0.74f,
        .splitter_diagnostics = 0.68f,
        .project_visible = true,
        .hierarchy_visible = true,
        .inspector_visible = false,
        .diagnostics_visible = true,
        .active_left_panel = jrpgmaker::editor::kPanelHierarchyId,
    };

    REQUIRE(jrpgmaker::editor::SaveEditorUserSettings(path, expected).ok);
    const auto loaded = jrpgmaker::editor::LoadEditorUserSettings(path);
    REQUIRE_FALSE(loaded.used_defaults);
    REQUIRE(loaded.diagnostics.empty());
    REQUIRE(loaded.settings == expected);
}

TEST_CASE("editor user settings migrate schema v2 with a valid active left panel",
          "[editor][settings]") {
    SettingsFixture fixture;
    const auto path = fixture.root() / "settings-v2-active.json";
    fixture.Write(
        path,
        R"({"schema":2,"splitters":{"splitter-left-center":0.31,"splitter-scene-inspector":0.74,"splitter-diagnostics":0.68},"visibility":{"workspace.project":true,"workspace.hierarchy":true,"workspace.inspector":true,"workspace.diagnostics":true}})");
    const auto loaded = jrpgmaker::editor::LoadEditorUserSettings(path);
    REQUIRE_FALSE(loaded.used_defaults);
    REQUIRE(loaded.settings.active_left_panel == jrpgmaker::editor::kPanelProjectId);
    REQUIRE(HasCode(loaded.diagnostics, "editor.settings.schema_migrated"));
}

TEST_CASE("editor user settings reject an invalid active left panel", "[editor][settings]") {
    SettingsFixture fixture;
    const auto path = fixture.root() / "settings-invalid-active.json";
    fixture.Write(
        path,
        R"({"schema":3,"splitters":{"splitter-left-center":0.31,"splitter-scene-inspector":0.74,"splitter-diagnostics":0.68},"visibility":{"workspace.project":true,"workspace.hierarchy":true,"workspace.inspector":true,"workspace.diagnostics":true},"active_left_panel":"workspace.scene"})");
    const auto loaded = jrpgmaker::editor::LoadEditorUserSettings(path);
    REQUIRE(loaded.used_defaults);
    REQUIRE(loaded.settings == jrpgmaker::editor::EditorUserSettings{});
    REQUIRE(HasCode(loaded.diagnostics, "editor.settings.field_invalid"));
}

TEST_CASE("editor user settings migrate schema v1 with default visible panels",
          "[editor][settings]") {
    SettingsFixture fixture;
    const auto path = fixture.root() / "settings-v1.json";
    fixture.Write(
        path,
        R"({"schema":1,"splitters":{"splitter-left-center":0.31,"splitter-scene-inspector":0.74,"splitter-diagnostics":0.68}})");
    const auto loaded = jrpgmaker::editor::LoadEditorUserSettings(path);
    REQUIRE_FALSE(loaded.used_defaults);
    REQUIRE(loaded.settings.splitter_left_center == Catch::Approx(0.31f));
    REQUIRE(loaded.settings.project_visible);
    REQUIRE(loaded.settings.hierarchy_visible);
    REQUIRE(loaded.settings.inspector_visible);
    REQUIRE(loaded.settings.diagnostics_visible);
    REQUIRE(HasCode(loaded.diagnostics, "editor.settings.schema_migrated"));
}

TEST_CASE("editor user settings invalid visibility falls back as one complete default",
          "[editor][settings]") {
    SettingsFixture fixture;
    const auto path = fixture.root() / "settings-invalid-visibility.json";
    fixture.Write(
        path,
        R"({"schema":2,"splitters":{"splitter-left-center":0.31,"splitter-scene-inspector":0.74,"splitter-diagnostics":0.68},"visibility":{"workspace.project":"no","workspace.hierarchy":true,"workspace.inspector":true,"workspace.diagnostics":true}})");
    const auto loaded = jrpgmaker::editor::LoadEditorUserSettings(path);
    REQUIRE(loaded.used_defaults);
    REQUIRE(loaded.settings == jrpgmaker::editor::EditorUserSettings{});
    REQUIRE(HasCode(loaded.diagnostics, "editor.settings.field_type"));
}
