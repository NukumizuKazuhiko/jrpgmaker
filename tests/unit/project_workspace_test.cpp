#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "jrpgmaker/project/transient_data_adapter.hpp"
#include "jrpgmaker/project/workspace.hpp"

namespace {

class NoopPlugin final : public jrpgmaker::plugin::IPlugin {};

class SidecarValidatingPlugin final : public jrpgmaker::plugin::IPlugin {
public:
    [[nodiscard]] jrpgmaker::plugin::PluginValidationResult
    ValidateData(const jrpgmaker::plugin::PluginValidationContext& context) const override {
        const auto data = context.read_file("assets/data/plugin_doc.json");
        if (!data)
            return {.issues = {data.error.value_or(jrpgmaker::plugin::PluginError{
                        "test.plugin.read", "sidecar could not be read", "plugin_doc"})}};
        const std::string json(reinterpret_cast<const char*>(data.bytes.data()), data.bytes.size());
        if (nlohmann::json::parse(json).value("name", std::string{}) == "invalid")
            return {.issues = {{"test.plugin.invalid", "plugin sidecar data is invalid",
                                "assets/data/plugin_doc.json"}}};
        return {};
    }
};

class ThrowingSidecarPlugin final : public jrpgmaker::plugin::IPlugin {
public:
    [[nodiscard]] jrpgmaker::plugin::PluginValidationResult
    ValidateData(const jrpgmaker::plugin::PluginValidationContext&) const override {
        throw std::runtime_error("test validator failure");
    }
};

bool RegisterProjectPlugins(jrpgmaker::plugin::PluginRegistry& registry,
                            jrpgmaker::plugin::PluginRegistry::Factory render_factory) {
    const auto register_plugin = [&registry](const char* id, const char* type,
                                             jrpgmaker::plugin::PluginRegistry::Factory factory) {
        nlohmann::json document;
        document["schema"] = 1;
        document["id"] = id;
        document["type"] = type;
        document["version"] = 1;
        document["engine_contract"] = 1;
        document["data_roots"] = nlohmann::json::array({"assets/data"});
        document["capabilities"] = nlohmann::json::array();
        const auto manifest = jrpgmaker::plugin::ParseManifest(document);
        if (!manifest || registry.Register(*manifest.manifest, std::move(factory)).has_value())
            return false;
        return true;
    };
    return register_plugin("sample.unlit", "render_style", std::move(render_factory)) &&
           register_plugin("sample.style", "render_style",
                           [] { return std::make_unique<NoopPlugin>(); }) &&
           register_plugin("sample.instant", "battle",
                           [] { return std::make_unique<NoopPlugin>(); }) &&
           register_plugin("sample.turn_based", "battle",
                           [] { return std::make_unique<NoopPlugin>(); });
}

std::filesystem::path MakeFixture() {
    const auto root = std::filesystem::temp_directory_path() / "jrpgmaker_workspace_fixture";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "assets/data");
    std::filesystem::copy(std::filesystem::path(JRPGMAKER_ASSET_DIR) / "data", root / "assets/data",
                          std::filesystem::copy_options::recursive, error);
    std::filesystem::copy_file(
        std::filesystem::path(JRPGMAKER_ASSET_DIR) / "data/project_demo.json",
        root / "project.json", std::filesystem::copy_options::overwrite_existing, error);
    std::ofstream(root / "assets/data/transient.json")
        << R"({"width":1,"height":1,"walkable":[true]})";
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

TEST_CASE("project workspace applies an edit and prepares a stable save plan",
          "[project][editor]") {
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

TEST_CASE("project workspace selects and saves an adapter-backed data document",
          "[project][editor]") {
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
    REQUIRE(workspace.SelectDocument("project.manifest").empty());
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

TEST_CASE("project workspace commits edits across multiple documents atomically",
          "[project][editor]") {
    const auto root = MakeFixture();
    jrpgmaker::project::ProjectWorkspace workspace(root);
    REQUIRE(workspace.Open());

    REQUIRE(workspace.SelectDocument("core.navigation").empty());
    auto walkable = workspace.CurrentDocument()["walkable"];
    walkable[0] = false;
    const auto navigation_edit = workspace.Apply({"core.navigation", "/walkable", walkable});
    REQUIRE(navigation_edit);

    REQUIRE(workspace.SelectDocument("project.manifest").empty());
    const auto manifest_edit =
        workspace.Apply({"project.manifest", "/id", "project.multi_document"});
    REQUIRE(manifest_edit);
    const auto plan = workspace.PrepareSave(manifest_edit.revision);
    REQUIRE(plan);
    REQUIRE(plan.changes.size() == 2);
    REQUIRE(workspace.Commit(*plan.token));

    std::ifstream manifest_input(root / "project.json");
    nlohmann::json manifest;
    manifest_input >> manifest;
    REQUIRE(manifest["id"] == "project.multi_document");
    std::ifstream navigation_input(root / "assets/data/navigation_demo.json");
    nlohmann::json navigation;
    navigation_input >> navigation;
    REQUIRE(navigation["walkable"][0] == false);
    REQUIRE(std::filesystem::exists(root / "project.json.bak"));
    REQUIRE(std::filesystem::exists(root / "assets/data/navigation_demo.json.bak"));
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

TEST_CASE("project workspace rejects stale save and preserves the source file",
          "[project][editor]") {
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

TEST_CASE("project workspace rejects a save when the complete project diagnosis fails",
          "[project][editor]") {
    const auto root = MakeFixture();
    jrpgmaker::project::ProjectWorkspace workspace(root);
    const auto opened = workspace.Open();
    REQUIRE(opened);
    REQUIRE(workspace.SelectDocument("core.navigation").empty());
    const auto edit = workspace.Apply({"core.navigation", "/walkable/0", false});
    REQUIRE(edit);

    const auto navigation_path = root / "assets/data/navigation_demo.json";
    std::ifstream before_input(navigation_path, std::ios::binary);
    const std::string before((std::istreambuf_iterator<char>(before_input)),
                             std::istreambuf_iterator<char>());
    nlohmann::json invalid_events = nlohmann::json::object();
    invalid_events["schema"] = 1;
    invalid_events["events"] = "invalid";
    {
        std::ofstream invalid_output(root / "assets/data/events_demo.json", std::ios::trunc);
        invalid_output << invalid_events.dump(2) << '\n';
    }
    std::ifstream verify_input(root / "assets/data/events_demo.json");
    nlohmann::json verify_events;
    verify_input >> verify_events;
    REQUIRE(verify_events["events"].is_string());
    const auto diagnosis = workspace.Diagnose(*opened.snapshot);
    REQUIRE_FALSE(diagnosis);

    const auto plan = workspace.PrepareSave(edit.revision);
    REQUIRE_FALSE(plan);
    REQUIRE_FALSE(plan.diagnostics.empty());
    REQUIRE(plan.diagnostics.front().code == "project.document.invalid");
    std::ifstream after_input(navigation_path, std::ios::binary);
    const std::string after((std::istreambuf_iterator<char>(after_input)),
                            std::istreambuf_iterator<char>());
    REQUIRE(after == before);
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

TEST_CASE("document adapter registry rejects duplicates and validates documents",
          "[project][editor]") {
    jrpgmaker::project::DocumentAdapterRegistry registry;
    auto adapter = jrpgmaker::project::DocumentAdapter{
        .type_id = "calendar",
        .fields = {{.path = "/id",
                    .value_type = "string",
                    .label_key = "editor.calendar.id",
                    .recipe = "",
                    .required = false,
                    .read_only = false,
                    .choices = {}}},
        .validate =
            [](const nlohmann::json& document) {
                if (!document.is_object())
                    return std::vector<jrpgmaker::project::Diagnostic>{
                        {"document.object_required", "/"}};
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
        .validate =
            [](const nlohmann::json&) { return std::vector<jrpgmaker::project::Diagnostic>{}; },
        .normalize_edit = {}};
    const auto result = registry.Register(adapter);
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(result.diagnostics.empty());
    REQUIRE(result.diagnostics.front().code == "project.adapter.duplicate_choice");
}

TEST_CASE("plugin editor descriptor registers as a project adapter", "[project][plugin][p13]") {
    const auto parsed = jrpgmaker::plugin::ParseEditorDescriptor(nlohmann::json{
        {"schema", 1},
        {"type_id", "vendor.example.document.v1"},
        {"fields", nlohmann::json::array({nlohmann::json{{"path", "/name"},
                                                         {"value_type", "string"},
                                                         {"role", "text"},
                                                         {"label_key", "plugin.example.name"},
                                                         {"recipe", "input"}}})}});
    REQUIRE(parsed);

    jrpgmaker::project::DocumentAdapterRegistry registry;
    const auto result = jrpgmaker::project::RegisterEditorDescriptor(
        registry, *parsed.descriptor,
        [](const nlohmann::json&) { return std::vector<jrpgmaker::project::Diagnostic>{}; });
    REQUIRE(result);
    const auto* adapter = registry.Find("vendor.example.document.v1");
    REQUIRE(adapter != nullptr);
    REQUIRE(adapter->fields[0].label_key == "plugin.example.name");
}

TEST_CASE("workspace diagnosis runs registered plugin validators", "[project][plugin][p13]") {
    const auto root = MakeFixture();
    jrpgmaker::plugin::PluginRegistry registry;
    const auto register_plugin = [&registry](const char* id, const char* type,
                                             jrpgmaker::plugin::PluginRegistry::Factory factory) {
        nlohmann::json document;
        document["schema"] = 1;
        document["id"] = id;
        document["type"] = type;
        document["version"] = 1;
        document["engine_contract"] = 1;
        document["data_roots"] = nlohmann::json::array({"assets/data"});
        document["capabilities"] = nlohmann::json::array();
        auto manifest = jrpgmaker::plugin::ParseManifest(document);
        REQUIRE(manifest);
        REQUIRE_FALSE(registry.Register(*manifest.manifest, std::move(factory)));
    };
    register_plugin("sample.unlit", "render_style", [] { return std::make_unique<NoopPlugin>(); });
    register_plugin("sample.style", "render_style", [] { return std::make_unique<NoopPlugin>(); });
    register_plugin("sample.instant", "battle", [] { return std::make_unique<NoopPlugin>(); });
    register_plugin("sample.turn_based", "battle", [] { return std::make_unique<NoopPlugin>(); });

    jrpgmaker::project::ProjectWorkspace workspace(
        root, jrpgmaker::project::CreateDefaultDocumentAdapters(), &registry);
    const auto opened = workspace.Open();
    REQUIRE(opened);
    REQUIRE(workspace.Diagnose(*opened.snapshot).diagnostics.empty());
}

TEST_CASE("workspace edits and saves an external plugin document", "[project][plugin][p13]") {
    const auto root = MakeFixture();
    std::ofstream output(root / "assets/data/plugin_doc.json");
    output << R"json({"schema":1,"name":"before"})json";
    output.close();

    const auto parsed = jrpgmaker::plugin::ParseEditorDescriptor(nlohmann::json{
        {"schema", 1},
        {"type_id", "vendor.example.document.v1"},
        {"fields", nlohmann::json::array({nlohmann::json{{"path", "/name"},
                                                         {"value_type", "string"},
                                                         {"role", "text"},
                                                         {"label_key", "plugin.example.name"},
                                                         {"recipe", "input"}}})}});
    REQUIRE(parsed);
    auto adapters = jrpgmaker::project::CreateDefaultDocumentAdapters();
    REQUIRE(jrpgmaker::project::RegisterEditorDescriptor(
        adapters, *parsed.descriptor, [](const nlohmann::json& document) {
            if (document.value("schema", 0) == 1)
                return std::vector<jrpgmaker::project::Diagnostic>{};
            return std::vector<jrpgmaker::project::Diagnostic>{{"test.invalid", "schema"}};
        }));
    jrpgmaker::project::ProjectWorkspace workspace(root, std::move(adapters));
    (void) workspace.RegisterExternalDocuments({jrpgmaker::project::DocumentDescriptor{
        .id = "plugin:vendor.example.document.v1:assets/data/plugin_doc.json",
        .path = "assets/data/plugin_doc.json",
        .label_key = "plugin.example.document",
        .editable = true,
        .type_id = "vendor.example.document.v1",
        .category_key = {}}});
    const auto opened = workspace.Open();
    REQUIRE(opened);
    REQUIRE(
        workspace.SelectDocument("plugin:vendor.example.document.v1:assets/data/plugin_doc.json")
            .empty());
    const auto edit = workspace.Apply(
        {.document_id = "plugin:vendor.example.document.v1:assets/data/plugin_doc.json",
         .field_path = "/name",
         .value = "after"});
    REQUIRE(edit);
    const auto token = workspace.PrepareSave(edit.revision);
    REQUIRE(token);
    REQUIRE(workspace.Commit(*token.token));
    nlohmann::json saved;
    std::ifstream input(root / "assets/data/plugin_doc.json");
    input >> saved;
    REQUIRE(saved["name"] == "after");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("workspace plugin validator sees the sidecar working copy before save",
          "[project][plugin][p13]") {
    const auto root = MakeFixture();
    std::ofstream output(root / "assets/data/plugin_doc.json");
    output << R"json({"schema":1,"name":"before"})json";
    output.close();

    jrpgmaker::plugin::PluginRegistry registry;
    const auto register_plugin = [&registry](const char* id, const char* type,
                                             jrpgmaker::plugin::PluginRegistry::Factory factory) {
        nlohmann::json document;
        document["schema"] = 1;
        document["id"] = id;
        document["type"] = type;
        document["version"] = 1;
        document["engine_contract"] = 1;
        document["data_roots"] = nlohmann::json::array({"assets/data"});
        document["capabilities"] = nlohmann::json::array();
        const auto manifest = jrpgmaker::plugin::ParseManifest(document);
        REQUIRE(manifest);
        REQUIRE_FALSE(registry.Register(*manifest.manifest, std::move(factory)));
    };
    register_plugin("sample.unlit", "render_style",
                    [] { return std::make_unique<SidecarValidatingPlugin>(); });
    register_plugin("sample.style", "render_style", [] { return std::make_unique<NoopPlugin>(); });
    register_plugin("sample.instant", "battle", [] { return std::make_unique<NoopPlugin>(); });
    register_plugin("sample.turn_based", "battle", [] { return std::make_unique<NoopPlugin>(); });

    auto adapters = jrpgmaker::project::CreateDefaultDocumentAdapters();
    const auto parsed = jrpgmaker::plugin::ParseEditorDescriptor(nlohmann::json{
        {"schema", 1},
        {"type_id", "vendor.example.document.v1"},
        {"fields", nlohmann::json::array({nlohmann::json{{"path", "/name"},
                                                         {"value_type", "string"},
                                                         {"role", "text"},
                                                         {"label_key", "plugin.example.name"},
                                                         {"recipe", "input"}}})}});
    REQUIRE(parsed);
    REQUIRE(jrpgmaker::project::RegisterEditorDescriptor(
        adapters, *parsed.descriptor,
        [](const nlohmann::json&) { return std::vector<jrpgmaker::project::Diagnostic>{}; }));
    jrpgmaker::project::ProjectWorkspace workspace(root, std::move(adapters), &registry);
    (void) workspace.RegisterExternalDocuments({jrpgmaker::project::DocumentDescriptor{
        .id = "plugin:vendor.example.document.v1:assets/data/plugin_doc.json",
        .path = "assets/data/plugin_doc.json",
        .label_key = "plugin.example.document",
        .editable = true,
        .type_id = "vendor.example.document.v1",
        .category_key = {}}});
    const auto opened = workspace.Open();
    REQUIRE(opened);
    const auto document_id = "plugin:vendor.example.document.v1:assets/data/plugin_doc.json";
    REQUIRE(workspace.SelectDocument(document_id).empty());
    const auto edit =
        workspace.Apply({.document_id = document_id, .field_path = "/name", .value = "invalid"});
    REQUIRE(edit);

    const auto plan = workspace.PrepareSave(edit.revision);
    REQUIRE_FALSE(plan);
    REQUIRE_FALSE(plan.diagnostics.empty());
    REQUIRE(plan.diagnostics.front().code == "test.plugin.invalid");
    std::ifstream input(root / "assets/data/plugin_doc.json");
    nlohmann::json saved;
    input >> saved;
    REQUIRE(saved["name"] == "before");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("workspace blocks save when a sidecar plugin validator throws",
          "[project][plugin][p13]") {
    const auto root = MakeFixture();
    const auto sidecar_path = root / "assets/data/plugin_doc.json";
    std::ofstream output(sidecar_path);
    output << R"json({"schema":1,"name":"before"})json";
    output.close();

    jrpgmaker::plugin::PluginRegistry registry;
    REQUIRE(
        RegisterProjectPlugins(registry, [] { return std::make_unique<ThrowingSidecarPlugin>(); }));
    auto adapters = jrpgmaker::project::CreateDefaultDocumentAdapters();
    const auto parsed = jrpgmaker::plugin::ParseEditorDescriptor(nlohmann::json{
        {"schema", 1},
        {"type_id", "vendor.example.document.v1"},
        {"fields", nlohmann::json::array({nlohmann::json{{"path", "/name"},
                                                         {"value_type", "string"},
                                                         {"role", "text"},
                                                         {"label_key", "plugin.example.name"},
                                                         {"recipe", "input"}}})}});
    REQUIRE(parsed);
    REQUIRE(jrpgmaker::project::RegisterEditorDescriptor(
        adapters, *parsed.descriptor,
        [](const nlohmann::json&) { return std::vector<jrpgmaker::project::Diagnostic>{}; }));
    jrpgmaker::project::ProjectWorkspace workspace(root, std::move(adapters), &registry);
    const auto document_id = "plugin:vendor.example.document.v1:assets/data/plugin_doc.json";
    (void) workspace.RegisterExternalDocuments(
        {jrpgmaker::project::DocumentDescriptor{.id = document_id,
                                                .path = "assets/data/plugin_doc.json",
                                                .label_key = "plugin.example.document",
                                                .editable = true,
                                                .type_id = "vendor.example.document.v1",
                                                .category_key = {}}});
    const auto opened = workspace.Open();
    REQUIRE(opened);
    REQUIRE(workspace.SelectDocument(document_id).empty());
    const auto edit =
        workspace.Apply({.document_id = document_id, .field_path = "/name", .value = "after"});
    REQUIRE(edit);

    const auto plan = workspace.PrepareSave(edit.revision);
    REQUIRE_FALSE(plan);
    REQUIRE_FALSE(plan.token.has_value());
    REQUIRE_FALSE(plan.diagnostics.empty());
    REQUIRE(plan.diagnostics.front().code == "plugin.validator.exception");
    std::ifstream input(sidecar_path, std::ios::binary);
    const std::string saved((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
    REQUIRE(saved == R"json({"schema":1,"name":"before"})json");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("project workspace validates an external descriptor before save",
          "[project][plugin][p13]") {
    const auto root = MakeFixture();
    std::ofstream output(root / "assets/data/plugin_doc.json");
    output << R"json({"schema":1,"name":17})json";
    output.close();

    const auto parsed = jrpgmaker::plugin::ParseEditorDescriptor(nlohmann::json{
        {"schema", 1},
        {"type_id", "vendor.example.document.v1"},
        {"fields", nlohmann::json::array({nlohmann::json{{"path", "/name"},
                                                         {"value_type", "string"},
                                                         {"role", "text"},
                                                         {"label_key", "plugin.example.name"},
                                                         {"recipe", "input"},
                                                         {"required", true}}})}});
    REQUIRE(parsed);
    auto adapters = jrpgmaker::project::CreateDefaultDocumentAdapters();
    REQUIRE(jrpgmaker::project::RegisterEditorDescriptor(adapters, *parsed.descriptor, {}));
    jrpgmaker::project::ProjectWorkspace workspace(root, std::move(adapters));
    (void) workspace.RegisterExternalDocuments({jrpgmaker::project::DocumentDescriptor{
        .id = "plugin:vendor.example.document.v1:assets/data/plugin_doc.json",
        .path = "assets/data/plugin_doc.json",
        .label_key = "plugin.example.document",
        .editable = true,
        .type_id = "vendor.example.document.v1",
        .category_key = {}}});
    const auto opened = workspace.Open();
    REQUIRE(opened);

    const auto plan = workspace.PrepareSave(opened.snapshot->revision);
    REQUIRE_FALSE(plan);
    REQUIRE_FALSE(plan.diagnostics.empty());
    REQUIRE(plan.diagnostics.front().code == "project.editor_descriptor.field_type");
    REQUIRE(plan.diagnostics.front().path ==
            "plugin:vendor.example.document.v1:assets/data/plugin_doc.json:/name");

    std::ifstream input(root / "assets/data/plugin_doc.json");
    nlohmann::json saved;
    input >> saved;
    REQUIRE(saved["name"] == 17);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("project workspace rejects edits to read-only external documents",
          "[project][plugin][p13][read-only]") {
    const auto root = MakeFixture();
    std::ofstream output(root / "assets/data/plugin_doc.json");
    output << R"json({"schema":1,"name":"before"})json";
    output.close();

    auto adapters = jrpgmaker::project::CreateDefaultDocumentAdapters();
    const auto parsed = jrpgmaker::plugin::ParseEditorDescriptor(nlohmann::json{
        {"schema", 1},
        {"type_id", "vendor.example.read_only.v1"},
        {"fields", nlohmann::json::array({nlohmann::json{{"path", "/name"},
                                                         {"value_type", "string"},
                                                         {"role", "text"},
                                                         {"label_key", "plugin.example.name"},
                                                         {"recipe", "input"}}})}});
    REQUIRE(parsed);
    REQUIRE(jrpgmaker::project::RegisterEditorDescriptor(adapters, *parsed.descriptor, {}));
    jrpgmaker::project::ProjectWorkspace workspace(root, std::move(adapters));
    const auto document_id =
        "plugin:readonly:vendor.example.read_only.v1:assets/data/plugin_doc.json";
    (void) workspace.RegisterExternalDocuments({jrpgmaker::project::DocumentDescriptor{
        .id = document_id,
        .path = "assets/data/plugin_doc.json",
        .label_key = "plugin.example.document",
        .editable = false,
        .type_id = "vendor.example.read_only.v1",
        .category_key = "editor.project.category.plugins"}});
    const auto opened = workspace.Open();
    REQUIRE(opened);
    REQUIRE(workspace.SelectDocument(document_id).empty());
    const auto edit = workspace.Apply({document_id, "/name", "after"});
    REQUIRE_FALSE(edit);
    REQUIRE(edit.diagnostics.size() == 1);
    REQUIRE(edit.diagnostics.front().code == "project.edit.document_read_only");
    REQUIRE(edit.diagnostics.front().path == document_id);

    std::ifstream input(root / "assets/data/plugin_doc.json");
    nlohmann::json saved;
    input >> saved;
    REQUIRE(saved["name"] == "before");
    std::error_code error;
    std::filesystem::remove_all(root, error);
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

TEST_CASE("external document registration rejects path escapes atomically",
          "[project][external-document][security]") {
    const auto root = MakeFixture();
    jrpgmaker::project::ProjectWorkspace workspace(root);
    const auto diagnostics =
        workspace.RegisterExternalDocuments({{.id = "cli.valid",
                                              .path = "assets/data/transient.json",
                                              .label_key = {},
                                              .editable = true,
                                              .type_id = "core.navigation",
                                              .category_key = {}},
                                             {.id = "cli.escape",
                                              .path = "../outside.json",
                                              .label_key = {},
                                              .editable = true,
                                              .type_id = "core.navigation",
                                              .category_key = {}}});

    REQUIRE(diagnostics.size() == 1);
    REQUIRE(diagnostics.front().code == "project.external_document.invalid");
    const auto opened = workspace.Open();
    REQUIRE(opened);
    REQUIRE(workspace.DescribeDocuments(*opened.snapshot).size() == 10);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("external document registration rejects built-in catalog conflicts",
          "[project][external-document]") {
    const auto root = MakeFixture();
    jrpgmaker::project::ProjectWorkspace workspace(root);
    const auto diagnostics =
        workspace.RegisterExternalDocuments({{.id = "core.navigation",
                                              .path = "assets/data/transient.json",
                                              .label_key = {},
                                              .editable = true,
                                              .type_id = "core.navigation",
                                              .category_key = {}}});
    REQUIRE(diagnostics.size() == 1);
    REQUIRE(diagnostics.front().code == "project.external_document.builtin_conflict");

    jrpgmaker::project::ProjectWorkspace path_workspace(root);
    const auto path_diagnostics =
        path_workspace.RegisterExternalDocuments({{.id = "cli.navigation.alias",
                                                   .path = "assets/data/./navigation_demo.json",
                                                   .label_key = {},
                                                   .editable = true,
                                                   .type_id = "core.navigation",
                                                   .category_key = {}}});
    REQUIRE(path_diagnostics.size() == 1);
    REQUIRE(path_diagnostics.front().code == "project.external_document.builtin_conflict");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("external document registration rejects an over-budget batch atomically",
          "[project][external-document]") {
    const auto root = MakeFixture();
    std::vector<jrpgmaker::project::DocumentDescriptor> documents(
        jrpgmaker::project::ProjectWorkspace::kMaxExternalDocuments + 1);
    jrpgmaker::project::ProjectWorkspace workspace(root);
    const auto diagnostics = workspace.RegisterExternalDocuments(std::move(documents));
    REQUIRE(diagnostics.size() == 1);
    REQUIRE(diagnostics.front().code == "project.external_document.limit");
    const auto opened = workspace.Open();
    REQUIRE(opened);
    REQUIRE(workspace.DescribeDocuments(*opened.snapshot).size() == 10);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("external document registration enforces its cumulative budget atomically",
          "[project][external-document]") {
    const auto root = MakeFixture();
    jrpgmaker::project::ProjectWorkspace workspace(root);
    REQUIRE(workspace
                .RegisterExternalDocuments({{.id = "cli.valid",
                                             .path = "assets/data/transient.json",
                                             .label_key = {},
                                             .editable = true,
                                             .type_id = "core.navigation",
                                             .category_key = {}}})
                .empty());
    std::vector<jrpgmaker::project::DocumentDescriptor> overflow(
        jrpgmaker::project::ProjectWorkspace::kMaxExternalDocuments);
    const auto diagnostics = workspace.RegisterExternalDocuments(std::move(overflow));
    REQUIRE(diagnostics.size() == 1);
    REQUIRE(diagnostics.front().code == "project.external_document.limit");
    const auto opened = workspace.Open();
    REQUIRE(opened);
    REQUIRE(workspace.DescribeDocuments(*opened.snapshot).size() == 11);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("workspace applies an object patch against the final candidate once",
          "[project][object-patch]") {
    const auto root = MakeFixture();
    std::ofstream(root / "assets/data/range.json")
        << R"({"low":5,"high":5,"a/b":1,"c~d":1,"locked":1})";
    const auto validations = std::make_shared<int>(0);
    auto adapters = jrpgmaker::project::CreateDefaultDocumentAdapters();
    REQUIRE(adapters.Register({.type_id = "test.range",
                               .fields = {{"/low", "integer", {}, {}, true, false, {}},
                                          {"/high", "integer", {}, {}, true, false, {}},
                                          {"/a~1b", "integer", {}, {}, true, false, {}},
                                          {"/c~0d", "integer", {}, {}, true, false, {}},
                                          {"/locked", "integer", {}, {}, true, true, {}}},
                               .validate =
                                   [validations](const nlohmann::json& document) {
                                       ++*validations;
                                       if (document.value("low", 0) <= document.value("high", 0))
                                           return std::vector<jrpgmaker::project::Diagnostic>{};
                                       return std::vector<jrpgmaker::project::Diagnostic>{
                                           {"test.range.invalid", "range"}};
                                   },
                               .normalize_edit = {}}));
    jrpgmaker::project::ProjectWorkspace workspace(root, std::move(adapters));
    REQUIRE(workspace
                .RegisterExternalDocuments({{.id = "test.range.document",
                                             .path = "assets/data/range.json",
                                             .label_key = {},
                                             .editable = true,
                                             .type_id = "test.range",
                                             .category_key = {}}})
                .empty());
    const auto opened = workspace.Open();
    REQUIRE(opened);
    REQUIRE(workspace.SelectDocument("test.range.document").empty());
    const int validations_before_edit = *validations;
    const auto initial = workspace.CurrentDocument();
    const auto initial_revision = opened.snapshot->revision;
    REQUIRE_FALSE(workspace.ApplyObjectPatch("test.range.document", nlohmann::json::array()));
    REQUIRE_FALSE(workspace.ApplyObjectPatch("test.range.document", nlohmann::json::object()));
    nlohmann::json oversized = nlohmann::json::object();
    for (std::size_t index = 0; index <= jrpgmaker::project::ProjectWorkspace::kMaxObjectPatchKeys;
         ++index)
        oversized["key" + std::to_string(index)] = index;
    REQUIRE_FALSE(workspace.ApplyObjectPatch("test.range.document", oversized));
    const auto unknown = workspace.ApplyObjectPatch("test.range.document", {{"missing", 1}});
    REQUIRE_FALSE(unknown);
    REQUIRE(unknown.diagnostics.front().code == "project.edit.field_not_editable");
    const auto read_only = workspace.ApplyObjectPatch("test.range.document", {{"locked", 2}});
    REQUIRE_FALSE(read_only);
    REQUIRE(read_only.diagnostics.front().code == "project.edit.field_read_only");
    REQUIRE(workspace.CurrentDocument() == initial);
    REQUIRE(workspace.PendingChanges().empty());
    REQUIRE(initial_revision == opened.snapshot->revision);

    const auto edited = workspace.ApplyObjectPatch(
        "test.range.document", nlohmann::json{{"low", 0}, {"high", 0}, {"a/b", 2}, {"c~d", 2}});

    REQUIRE(edited);
    REQUIRE(*validations == validations_before_edit + 1);
    REQUIRE(edited.revision == opened.snapshot->revision + 1);
    REQUIRE(edited.changes.size() == 4);
    REQUIRE(workspace.CurrentDocument()["a/b"] == 2);
    REQUIRE(workspace.CurrentDocument()["c~d"] == 2);
    const auto pending_before_noop = workspace.PendingChanges().size();
    const auto noop = workspace.ApplyObjectPatch(
        "test.range.document", nlohmann::json{{"low", 0}, {"high", 0}, {"a/b", 2}, {"c~d", 2}});
    REQUIRE(noop);
    REQUIRE(noop.changes.empty());
    REQUIRE(noop.revision == edited.revision);
    REQUIRE(workspace.PendingChanges().size() == pending_before_noop);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("workspace rejects object patch normalizer changes outside declared fields",
          "[project][object-patch]") {
    const auto root = MakeFixture();
    std::ofstream(root / "assets/data/normalized.json") << R"({"name":"before"})";
    auto adapters = jrpgmaker::project::CreateDefaultDocumentAdapters();
    REQUIRE(adapters.Register(
        {.type_id = "test.normalized",
         .fields = {{"/name", "string", {}, {}, true, false, {}}},
         .validate =
             [](const nlohmann::json&) { return std::vector<jrpgmaker::project::Diagnostic>{}; },
         .normalize_edit =
             [](std::string_view, nlohmann::json& candidate) {
                 candidate["hidden"] = true;
                 return std::vector<jrpgmaker::project::Diagnostic>{};
             }}));
    jrpgmaker::project::ProjectWorkspace workspace(root, std::move(adapters));
    REQUIRE(workspace
                .RegisterExternalDocuments({{.id = "test.normalized.document",
                                             .path = "assets/data/normalized.json",
                                             .label_key = {},
                                             .editable = true,
                                             .type_id = "test.normalized",
                                             .category_key = {}}})
                .empty());
    REQUIRE(workspace.Open());
    REQUIRE(workspace.SelectDocument("test.normalized.document").empty());
    const auto before = workspace.CurrentDocument();

    const auto edit = workspace.ApplyObjectPatch("test.normalized.document", {{"name", "after"}});

    REQUIRE_FALSE(edit);
    REQUIRE(edit.diagnostics.size() == 1);
    REQUIRE(edit.diagnostics.front().code == "project.edit.normalizer_out_of_contract");
    REQUIRE(workspace.CurrentDocument() == before);
    REQUIRE(workspace.PendingChanges().empty());
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("transient calendar adapter validates the patched document through the calendar parser",
          "[project][transient-data]") {
    const auto root = MakeFixture();
    const auto result = jrpgmaker::project::CreateTransientDataAdapter(
        root, "assets/data/calendar_demo.json", {{"id", "calendar.edited"}});

    REQUIRE(result);
    REQUIRE(result.adapter->fields.size() == 1);
    REQUIRE(result.adapter->fields.front().path == "/id");
    nlohmann::json valid_document;
    std::ifstream(root / "assets/data/calendar_demo.json") >> valid_document;
    valid_document["id"] = "calendar.edited";
    REQUIRE(result.adapter->validate(valid_document).empty());
    valid_document["schema"] = 0;
    const auto diagnostics = result.adapter->validate(valid_document);
    REQUIRE(diagnostics.size() == 1);
    REQUIRE(diagnostics.front().code == "project.transient_data.invalid");
    REQUIRE(diagnostics.front().path == "assets/data/calendar_demo.json");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("transient data adapter supports every schema-aware projecttool document",
          "[project][transient-data]") {
    const auto root = MakeFixture();
    constexpr std::array names = {
        "events_demo.json",     "navigation_demo.json",    "collision_demo.json",
        "camera_demo.json",     "interaction_demo.json",   "input_actions.json",
        "localization_en.json", "material_demo.json",      "material_accent.json",
        "schedule_demo.json",   "vertical_slice_demo.json"};

    for (const auto* name : names) {
        const auto relative = std::filesystem::path("assets/data") / name;
        const auto result =
            jrpgmaker::project::CreateTransientDataAdapter(root, relative, {{"schema", 1}});
        CAPTURE(name);
        REQUIRE(result);
        nlohmann::json document;
        std::ifstream(root / relative) >> document;
        REQUIRE(result.adapter->validate(document).empty());
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("transient data adapters reject invalid candidates with structured diagnostics",
          "[project][transient-data]") {
    const auto root = MakeFixture();
    constexpr std::array names = {
        "events_demo.json",     "navigation_demo.json",  "collision_demo.json",
        "camera_demo.json",     "interaction_demo.json", "input_actions.json",
        "calendar_demo.json",   "localization_en.json",  "material_demo.json",
        "material_accent.json", "schedule_demo.json",    "vertical_slice_demo.json"};

    for (const auto* name : names) {
        const auto relative = std::filesystem::path("assets/data") / name;
        const auto result =
            jrpgmaker::project::CreateTransientDataAdapter(root, relative, {{"schema", 1}});
        CAPTURE(name);
        REQUIRE(result);
        const auto diagnostics = result.adapter->validate(nlohmann::json::object());
        REQUIRE(diagnostics.size() == 1);
        REQUIRE(diagnostics.front().code == "project.transient_data.invalid");
        REQUIRE(diagnostics.front().path == relative.generic_string());
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("transient data adapter recognizes only declared plugin data roots",
          "[project][transient-data]") {
    const auto root = MakeFixture();
    const auto path = std::filesystem::path("mods/sample/content/document.json");
    const std::array plugin_roots = {std::filesystem::path("mods/sample/content")};

    const auto undeclared =
        jrpgmaker::project::CreateTransientDataAdapter(root, path, {{"value", 1}});
    REQUIRE_FALSE(undeclared);
    REQUIRE(undeclared.diagnostics.front().code == "project.transient_data.unknown_file");
    const auto declared =
        jrpgmaker::project::CreateTransientDataAdapter(root, path, {{"value", 1}}, plugin_roots);
    REQUIRE(declared);
    REQUIRE(declared.adapter->validate(nlohmann::json::object()).empty());
    REQUIRE_FALSE(declared.adapter->validate(nlohmann::json::array()).empty());
    const auto root_itself = jrpgmaker::project::CreateTransientDataAdapter(
        root, "mods/sample/content", {{"value", 1}}, plugin_roots);
    REQUIRE_FALSE(root_itself);
    REQUIRE(root_itself.diagnostics.front().code == "project.transient_data.unknown_file");
    const auto name_collision = jrpgmaker::project::CreateTransientDataAdapter(
        root, "mods/sample/content/calendar_demo.json", {{"value", 1}}, plugin_roots);
    REQUIRE(name_collision);
    REQUIRE(name_collision.adapter->validate(nlohmann::json::object()).empty());
    REQUIRE_FALSE(name_collision.adapter->validate(nlohmann::json::array()).empty());
}

#if defined(_WIN32)
TEST_CASE("transient data adapter rejects drive-relative Windows paths",
          "[project][transient-data]") {
    const auto result = jrpgmaker::project::CreateTransientDataAdapter(
        MakeFixture(), "C:calendar_demo.json", {{"schema", 1}});

    REQUIRE_FALSE(result);
    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics.front().code == "project.transient_data.unsafe_path");
}
#endif
