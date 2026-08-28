#include "jrpgmaker/project/workspace.hpp"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

#include "jrpgmaker/core/map_data.hpp"
#include "jrpgmaker/domain/event_script.hpp"
#include "jrpgmaker/domain/interaction.hpp"

namespace jrpgmaker::project {
namespace {

constexpr std::size_t kMaxDiagnostics = 128;

bool SafeRelative(const std::string& path) {
    return !path.empty() && path.find("..") == std::string::npos && path.front() != '/' &&
           path.front() != '\\';
}

void Add(std::vector<Diagnostic>& diagnostics, std::string code, std::string path) {
    if (diagnostics.size() < kMaxDiagnostics)
        diagnostics.push_back({std::move(code), std::move(path)});
}

bool Read(const std::filesystem::path& path, nlohmann::json& document,
          std::vector<Diagnostic>& diagnostics) {
    std::ifstream file(path);
    if (!file.is_open()) {
        Add(diagnostics, "project.file.open", path.string());
        return false;
    }
    try {
        file >> document;
    } catch (const std::exception&) {
        Add(diagnostics, "project.file.json", path.string());
        return false;
    }
    return true;
}

bool AddPath(const std::filesystem::path& root, const std::string& relative,
             std::vector<Diagnostic>& diagnostics) {
    if (!SafeRelative(relative)) {
        Add(diagnostics, "project.path.unsafe", relative);
        return false;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(root / relative, error) &&
        !std::filesystem::is_directory(root / relative, error)) {
        Add(diagnostics, "project.path.missing", relative);
        return false;
    }
    return true;
}

const std::vector<std::string>& EditableManifestFields() {
    static const std::vector<std::string> fields = {
        "id",           "render_style",      "battle_plugin", "plugins",
        "data_roots",   "material_document", "input_actions", "event_script",
        "localization", "resource_manifest", "navigation", "collision", "camera", "interaction"};
    return fields;
}

bool ValidateManifestDocument(const std::filesystem::path& root, const nlohmann::json& document,
                              plugin::ProjectManifest& manifest,
                              std::vector<Diagnostic>& diagnostics) {
    const auto parsed = plugin::ParseProjectManifest(document);
    if (!parsed) {
        Add(diagnostics, parsed.error->code, parsed.error->path);
        return false;
    }
    manifest = *parsed.manifest;
    for (const auto& path : manifest.data_roots)
        AddPath(root, path, diagnostics);
    for (const auto& path : {manifest.material_document, manifest.input_actions,
                             manifest.event_script, manifest.localization,
                             manifest.resource_manifest})
        AddPath(root, path, diagnostics);
    nlohmann::json material;
    if (diagnostics.empty() && Read(root / manifest.material_document, material, diagnostics) &&
        (!material.is_object() ||
         material.value("style_plugin_id", std::string{}) != manifest.render_style))
        Add(diagnostics, "project.material.style_mismatch", manifest.material_document);
    return diagnostics.empty();
}

void AddRevisionConflict(std::vector<Diagnostic>& diagnostics) {
    Add(diagnostics, "project.save.revision_conflict", "revision");
}

template <typename ParseFn>
std::vector<Diagnostic> ValidateParsedDocument(const nlohmann::json& document,
                                               const std::string& path, ParseFn&& parse) {
    try {
        parse(document);
    } catch (const std::exception&) {
        return {{"project.document.invalid", path}};
    }
    return {};
}

std::vector<Diagnostic> ValidateManifestAdapter(const nlohmann::json& document) {
    const auto parsed = plugin::ParseProjectManifest(document);
    if (parsed)
        return {};
    return {{parsed.error->code, parsed.error->path}};
}

} // namespace

AdapterResult DocumentAdapterRegistry::Register(DocumentAdapter adapter) {
    std::vector<Diagnostic> diagnostics;
    if (adapter.type_id.empty())
        Add(diagnostics, "project.adapter.type_id_required", "type_id");
    if (adapter.fields.size() > kMaxAdapters)
        Add(diagnostics, "project.adapter.field_limit", adapter.type_id);
    for (std::size_t i = 0; i < adapter.fields.size(); ++i) {
        if (adapter.fields[i].path.empty() || adapter.fields[i].path.front() != '/')
            Add(diagnostics, "project.adapter.field_path_invalid",
                adapter.type_id + "/fields/" + std::to_string(i));
        for (std::size_t j = 0; j < i; ++j)
            if (adapter.fields[i].path == adapter.fields[j].path)
                Add(diagnostics, "project.adapter.duplicate_field",
                    adapter.type_id + "/fields/" + std::to_string(i));
    }
    if (!adapter.validate)
        Add(diagnostics, "project.adapter.validator_required", adapter.type_id);
    if (Find(adapter.type_id) != nullptr)
        Add(diagnostics, "project.adapter.duplicate_type", adapter.type_id);
    if (adapters_.size() >= kMaxAdapters)
        Add(diagnostics, "project.adapter.limit", "registry");
    if (!diagnostics.empty())
        return {std::move(diagnostics)};
    adapters_.push_back(std::move(adapter));
    return {};
}

const DocumentAdapter* DocumentAdapterRegistry::Find(const std::string& type_id) const {
    const auto it = std::find_if(adapters_.begin(), adapters_.end(),
                                 [&type_id](const DocumentAdapter& adapter) {
                                     return adapter.type_id == type_id;
                                 });
    return it == adapters_.end() ? nullptr : &*it;
}

AdapterResult DocumentAdapterRegistry::Validate(const std::string& type_id,
                                                 const nlohmann::json& document) const {
    const auto* adapter = Find(type_id);
    if (adapter == nullptr)
        return {{Diagnostic{"project.adapter.unknown_type", type_id}}};
    return {adapter->validate(document)};
}

DocumentAdapterRegistry CreateDefaultDocumentAdapters() {
    DocumentAdapterRegistry registry;
    (void) registry.Register(DocumentAdapter{
        .type_id = "project.manifest",
        .fields = {{"/id", "string", "editor.project.id", "text", true, false},
                   {"/render_style", "string", "editor.project.render_style", "select", true, false},
                   {"/battle_plugin", "string", "editor.project.battle_plugin", "select", false, false},
                   {"/plugins", "string[]", "editor.project.plugins", "list", true, false},
                   {"/data_roots", "path[]", "editor.project.data_roots", "list", true, false},
                   {"/material_document", "path", "editor.project.material", "resource", true, false},
                   {"/input_actions", "path", "editor.project.input", "resource", true, false},
                   {"/event_script", "path", "editor.project.events", "resource", true, false},
                   {"/localization", "path", "editor.project.localization", "resource", true, false},
                   {"/resource_manifest", "path", "editor.project.resources", "resource", true, false},
                   {"/navigation", "path", "editor.project.navigation", "resource", true, false},
                   {"/collision", "path", "editor.project.collision", "resource", true, false},
                   {"/camera", "path", "editor.project.camera", "resource", true, false},
                   {"/interaction", "path", "editor.project.interaction", "resource", true, false}},
        .validate = ValidateManifestAdapter});
    (void) registry.Register(DocumentAdapter{
        .type_id = "domain.event_script",
        .fields = {{"/events", "object[]", "editor.events.items", "event_list", true, false}},
        .validate = [](const nlohmann::json& document) {
            return ValidateParsedDocument(document, "event_script", [](const auto& value) {
                (void) domain::ParseEventScript(value);
            });
        }});
    (void) registry.Register(DocumentAdapter{
        .type_id = "core.navigation",
        .fields = {{"/width", "integer", "editor.navigation.width", "number", true, false},
                   {"/height", "integer", "editor.navigation.height", "number", true, false},
                   {"/walkable", "boolean[]", "editor.navigation.walkable", "grid", true, false}},
        .validate = [](const nlohmann::json& document) {
            return ValidateParsedDocument(document, "navigation", [](const auto& value) {
                (void) core::ParseNavigationGrid(value);
            });
        }});
    (void) registry.Register(DocumentAdapter{
        .type_id = "core.collision",
        .fields = {{"/obstacles", "object[]", "editor.collision.obstacles", "aabb_list", true, false}},
        .validate = [](const nlohmann::json& document) {
            return ValidateParsedDocument(document, "collision", [](const auto& value) {
                (void) core::ParseCollisionAabbs(value);
            });
        }});
    (void) registry.Register(DocumentAdapter{
        .type_id = "core.camera",
        .fields = {{"/third_person", "object", "editor.camera.third_person", "camera", true, false},
                   {"/fixed_regions", "object[]", "editor.camera.fixed_regions", "region_list", true, false}},
        .validate = [](const nlohmann::json& document) {
            return ValidateParsedDocument(document, "camera", [](const auto& value) {
                (void) core::ParseCameraRigData(value);
            });
        }});
    (void) registry.Register(DocumentAdapter{
        .type_id = "domain.interaction",
        .fields = {{"/interactions", "object[]", "editor.interaction.points", "interaction_list", true, false}},
        .validate = [](const nlohmann::json& document) {
            return ValidateParsedDocument(document, "interaction", [](const auto& value) {
                (void) domain::ParseInteractionPoints(value);
            });
        }});
    return registry;
}

ProjectWorkspace::ProjectWorkspace(std::filesystem::path root, DocumentAdapterRegistry adapters)
    : root_(std::move(root)), adapters_(std::move(adapters)) {}

WorkspaceResult ProjectWorkspace::Open() {
    WorkspaceResult result;
    nlohmann::json document;
    std::vector<Diagnostic> diagnostics;
    if (!Read(root_ / "project.json", document, diagnostics)) {
        result.diagnostics = std::move(diagnostics);
        return result;
    }
    const auto adapter_result = adapters_.Validate("project.manifest", document);
    if (!adapter_result) {
        result.diagnostics = adapter_result.diagnostics;
        return result;
    }
    plugin::ProjectManifest manifest;
    if (!ValidateManifestDocument(root_, document, manifest, diagnostics)) {
        result.diagnostics = std::move(diagnostics);
        return result;
    }
    revision_ = 0;
    working_document_ = std::move(document);
    pending_changes_.clear();
    snapshot_ = ProjectSnapshot{root_, manifest, revision_};
    result.snapshot = snapshot_;
    return result;
}

DiagnosticSet ProjectWorkspace::Diagnose(const ProjectSnapshot& snapshot) const {
    DiagnosticSet result;
    nlohmann::json events_document;
    nlohmann::json navigation_document;
    nlohmann::json collision_document;
    nlohmann::json camera_document;
    nlohmann::json interaction_document;
    const auto event_path = snapshot.root / snapshot.manifest.event_script;
    if (!Read(event_path, events_document, result.diagnostics) ||
        !Read(snapshot.root / snapshot.manifest.navigation, navigation_document, result.diagnostics) ||
        !Read(snapshot.root / snapshot.manifest.collision, collision_document, result.diagnostics) ||
        !Read(snapshot.root / snapshot.manifest.camera, camera_document, result.diagnostics) ||
        !Read(snapshot.root / snapshot.manifest.interaction, interaction_document, result.diagnostics))
        return result;
    const auto validate = [this, &result](const char* type_id, const nlohmann::json& document) {
        const auto adapter_result = adapters_.Validate(type_id, document);
        for (const auto& diagnostic : adapter_result.diagnostics)
            Add(result.diagnostics, diagnostic.code, diagnostic.path);
    };
    validate("domain.event_script", events_document);
    validate("core.navigation", navigation_document);
    validate("core.collision", collision_document);
    validate("core.camera", camera_document);
    validate("domain.interaction", interaction_document);
    if (!result.diagnostics.empty())
        return result;
    try {
        const auto events = domain::ParseEventScript(events_document);
        const auto navigation = core::ParseNavigationGrid(navigation_document);
        const auto collision = core::ParseCollisionAabbs(collision_document);
        const auto camera = core::ParseCameraRigData(camera_document);
        const auto interactions = domain::ParseInteractionPoints(interaction_document);
        domain::ValidateInteractionTargets(interactions, events);
        result.event_count = events.events.size();
        result.interaction_count = interactions.size();
        result.collision_count = collision.size();
        result.navigation_width = navigation.width();
        result.navigation_height = navigation.height();
        result.camera_region_count = camera.fixed_regions.size();
    } catch (const std::exception&) {
        Add(result.diagnostics, "project.interaction.target_invalid", snapshot.manifest.interaction);
    }
    return result;
}

EditResult ProjectWorkspace::Apply(const EditCommand& command) {
    EditResult result{.revision = revision_, .changes = {}, .diagnostics = {}};
    if (!snapshot_.has_value()) {
        Add(result.diagnostics, "project.workspace.not_open", "workspace");
        return result;
    }
    if (command.document_id != "project.manifest") {
        Add(result.diagnostics, "project.edit.document_unknown", command.document_id);
        return result;
    }
    if (command.field_path.size() < 2 || command.field_path.front() != '/' ||
        command.field_path.find('/', 1) != std::string::npos) {
        Add(result.diagnostics, "project.edit.field_path_invalid", command.field_path);
        return result;
    }
    const std::string field = command.field_path.substr(1);
    if (std::find(EditableManifestFields().begin(), EditableManifestFields().end(), field) ==
        EditableManifestFields().end()) {
        Add(result.diagnostics, "project.edit.field_not_editable", command.field_path);
        return result;
    }
    nlohmann::json candidate = working_document_;
    const nlohmann::json before = candidate.contains(field) ? candidate[field] : nlohmann::json();
    candidate[field] = command.value;
    plugin::ProjectManifest manifest;
    if (!ValidateManifestDocument(root_, candidate, manifest, result.diagnostics))
        return result;
    if (before == command.value)
        return result;
    working_document_ = std::move(candidate);
    ++revision_;
    snapshot_->manifest = std::move(manifest);
    snapshot_->revision = revision_;
    result.revision = revision_;
    Change change{.document_id = command.document_id,
                  .field_path = command.field_path,
                  .before = before,
                  .after = command.value,
                  .sequence = pending_changes_.size()};
    pending_changes_.push_back(change);
    result.changes.push_back(std::move(change));
    return result;
}

SavePlan ProjectWorkspace::PrepareSave(std::uint64_t expected_revision) const {
    SavePlan result;
    if (!snapshot_.has_value()) {
        Add(result.diagnostics, "project.workspace.not_open", "workspace");
        return result;
    }
    if (expected_revision != revision_) {
        AddRevisionConflict(result.diagnostics);
        return result;
    }
    result.token = SaveToken{revision_};
    result.changes = pending_changes_;
    return result;
}

CommitResult ProjectWorkspace::Commit(const SaveToken& token) {
    CommitResult result{.revision = revision_, .backup = {}, .diagnostics = {}};
    if (!snapshot_.has_value()) {
        Add(result.diagnostics, "project.workspace.not_open", "workspace");
        return result;
    }
    if (token.revision != revision_) {
        AddRevisionConflict(result.diagnostics);
        return result;
    }
    if (pending_changes_.empty()) {
        Add(result.diagnostics, "project.save.nothing_to_commit", "project.json");
        return result;
    }
    const auto manifest_path = root_ / "project.json";
    const auto temporary = root_ / ".project.json.tmp";
    std::error_code error;
    if (std::filesystem::exists(temporary, error)) {
        Add(result.diagnostics, "project.save.temporary_exists", temporary.string());
        return result;
    }
    for (std::size_t index = 0; index <= 8; ++index) {
        const auto backup = index == 0 ? root_ / "project.json.bak"
                                       : root_ / ("project.json.bak." + std::to_string(index));
        if (std::filesystem::exists(backup, error))
            continue;
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output.is_open()) {
                Add(result.diagnostics, "project.save.temporary_open", temporary.string());
                return result;
            }
            output << working_document_.dump(2) << '\n';
            if (!output.good()) {
                std::filesystem::remove(temporary, error);
                Add(result.diagnostics, "project.save.temporary_write", temporary.string());
                return result;
            }
        }
        std::filesystem::rename(manifest_path, backup, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            Add(result.diagnostics, "project.save.backup_create", manifest_path.string());
            return result;
        }
        std::filesystem::rename(temporary, manifest_path, error);
        if (error) {
            std::filesystem::rename(backup, manifest_path, error);
            Add(result.diagnostics, "project.save.commit", manifest_path.string());
            return result;
        }
        result.backup = backup;
        pending_changes_.clear();
        return result;
    }
    Add(result.diagnostics, "project.save.backup_limit", manifest_path.string());
    return result;
}

MigrationResult ProjectWorkspace::Migrate(std::uint32_t target_schema) const {
    MigrationResult result;
    if (!snapshot_.has_value()) {
        Add(result.diagnostics, "project.workspace.not_open", "workspace");
        return result;
    }
    result.from_schema = snapshot_->manifest.schema;
    result.to_schema = target_schema;
    if (target_schema != 1 || result.from_schema != target_schema)
        Add(result.diagnostics, "project.migrate.unsupported_schema", "schema");
    return result;
}

} // namespace jrpgmaker::project
