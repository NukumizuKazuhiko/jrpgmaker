#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/editor/editor_plugin_registry.hpp"
#include "jrpgmaker/plugins/register.hpp"

namespace {

class NoopPlugin final : public jrpgmaker::plugin::IPlugin {};

std::filesystem::path MakeRegistryFixture(std::string_view suffix) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("jrpgmaker_editor_plugin_registry_" + std::string(suffix));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "plugins/sample_unlit", error);
    nlohmann::json project{{"schema", 1},
                           {"id", "project.registry"},
                           {"render_style", "sample.unlit"},
                           {"plugins", nlohmann::json::array({"sample.unlit"})},
                           {"data_roots", nlohmann::json::array()}};
    std::ofstream(root / "project.json") << project.dump(2);
    nlohmann::json manifest{{"schema", 1},
                            {"id", "sample.unlit"},
                            {"type", "render_style"},
                            {"version", 1},
                            {"engine_contract", 1},
                            {"data_roots", nlohmann::json::array()},
                            {"capabilities", nlohmann::json::array()}};
    std::ofstream(root / "plugins/sample_unlit/plugin.json") << manifest.dump(2);
    return root;
}

std::vector<jrpgmaker::editor::EditorPluginFactoryBinding> UnlitFactory() {
    return {{.id = "sample.unlit",
             .manifest_path = "plugins/sample_unlit/plugin.json",
             .factory = [] { return std::make_unique<NoopPlugin>(); }}};
}

void RequireOnlyDiagnostic(const jrpgmaker::editor::EditorPluginRegistryAssembly& assembled,
                           std::string_view code, std::string_view path) {
    REQUIRE_FALSE(assembled);
    REQUIRE(assembled.diagnostics.size() == 1);
    REQUIRE(assembled.diagnostics.front().code == code);
    REQUIRE(assembled.diagnostics.front().path == path);
}

} // namespace

TEST_CASE("editor plugin assembly registers only plugins declared by the project",
          "[editor][plugin][host]") {
    const auto root = MakeRegistryFixture("declared_only");
    const auto factories = jrpgmaker::plugins::CompiledSamplePlugins();

    const auto assembled = jrpgmaker::editor::AssembleEditorPluginRegistry(root, factories);

    REQUIRE(assembled);
    REQUIRE(assembled.diagnostics.empty());
    REQUIRE(assembled.registry->size() == 1);
    REQUIRE(assembled.registry->FindManifest("sample.unlit").has_value());
    REQUIRE_FALSE(assembled.registry->FindManifest("sample.style").has_value());
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor plugin assembly reports a missing declared manifest structurally",
          "[editor][plugin][host]") {
    const auto root = MakeRegistryFixture("missing_manifest");
    std::filesystem::remove(root / "plugins/sample_unlit/plugin.json");
    const auto assembled = jrpgmaker::editor::AssembleEditorPluginRegistry(root, UnlitFactory());
    RequireOnlyDiagnostic(assembled, "plugin.manifest.open", "plugins/sample_unlit/plugin.json");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor plugin assembly reports an invalid declared manifest structurally",
          "[editor][plugin][host]") {
    const auto root = MakeRegistryFixture("invalid_manifest");
    std::ofstream(root / "plugins/sample_unlit/plugin.json", std::ios::trunc) << "{";
    const auto assembled = jrpgmaker::editor::AssembleEditorPluginRegistry(root, UnlitFactory());
    RequireOnlyDiagnostic(assembled, "plugin.manifest.parse", "plugins/sample_unlit/plugin.json");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor plugin assembly rejects a manifest symlink escaping the project root",
          "[editor][plugin][host][security]") {
    const auto root = MakeRegistryFixture("manifest_symlink_escape");
    const auto outside =
        std::filesystem::temp_directory_path() / "jrpgmaker_editor_plugin_manifest_outside.json";
    std::filesystem::copy_file(root / "plugins/sample_unlit/plugin.json", outside,
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove(root / "plugins/sample_unlit/plugin.json");
    std::error_code error;
    std::filesystem::create_symlink(outside, root / "plugins/sample_unlit/plugin.json", error);
    if (error) {
        std::filesystem::remove(outside, error);
        std::filesystem::remove_all(root, error);
        SKIP("platform cannot create a file symlink for the containment test");
    }

    const auto assembled = jrpgmaker::editor::AssembleEditorPluginRegistry(root, UnlitFactory());
    RequireOnlyDiagnostic(assembled, "plugin.manifest.path", "plugins/sample_unlit/plugin.json");
    std::filesystem::remove(outside, error);
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor plugin assembly rejects a project plugin absent from the compiled catalog",
          "[editor][plugin][host]") {
    const auto root = MakeRegistryFixture("not_compiled");
    const auto assembled = jrpgmaker::editor::AssembleEditorPluginRegistry(root, {});
    RequireOnlyDiagnostic(assembled, "project.plugin_missing", "sample.unlit");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor plugin assembly exposes a compiled factory registration failure",
          "[editor][plugin][host]") {
    const auto root = MakeRegistryFixture("factory_failure");
    auto factories = UnlitFactory();
    factories.front().factory = {};
    const auto assembled = jrpgmaker::editor::AssembleEditorPluginRegistry(root, factories);
    RequireOnlyDiagnostic(assembled, "registry.factory", "sample.unlit");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor plugin assembly reports a project with no selectable plugin",
          "[editor][plugin][host]") {
    const auto root = MakeRegistryFixture("no_plugins");
    nlohmann::json project{{"schema", 1},
                           {"id", "project.registry"},
                           {"render_style", "sample.unlit"},
                           {"plugins", nlohmann::json::array()},
                           {"data_roots", nlohmann::json::array()}};
    std::ofstream(root / "project.json", std::ios::trunc) << project.dump(2);
    const auto assembled = jrpgmaker::editor::AssembleEditorPluginRegistry(root, UnlitFactory());
    RequireOnlyDiagnostic(assembled, "project.render_style", "render_style");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}
