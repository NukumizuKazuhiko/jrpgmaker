#include "jrpgmaker/project/workspace.hpp"

#include <algorithm>
#include <array>
#include <fstream>

#include <nlohmann/json.hpp>

#include "jrpgmaker/core/input_actions.hpp"
#include "jrpgmaker/core/map_data.hpp"
#include "jrpgmaker/domain/event_script.hpp"
#include "jrpgmaker/domain/interaction.hpp"
#include "jrpgmaker/domain/localization.hpp"

namespace jrpgmaker::project {
namespace {

constexpr std::size_t kMaxDiagnostics = 128;
constexpr std::size_t kMaxFieldChoices = 64;

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
    for (const auto& path :
         {manifest.material_document, manifest.input_actions, manifest.event_script,
          manifest.localization, manifest.resource_manifest})
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
        if (adapter.fields[i].choices.size() > kMaxFieldChoices)
            Add(diagnostics, "project.adapter.choice_limit",
                adapter.type_id + "/fields/" + std::to_string(i));
        for (std::size_t choice = 0; choice < adapter.fields[i].choices.size(); ++choice) {
            const auto& value = adapter.fields[i].choices[choice];
            if (value.empty())
                Add(diagnostics, "project.adapter.choice_invalid",
                    adapter.type_id + "/fields/" + std::to_string(i));
            for (std::size_t previous = 0; previous < choice; ++previous)
                if (value == adapter.fields[i].choices[previous])
                    Add(diagnostics, "project.adapter.duplicate_choice",
                        adapter.type_id + "/fields/" + std::to_string(i));
        }
        if (!adapter.fields[i].choices.empty() && adapter.fields[i].value_type != "select")
            Add(diagnostics, "project.adapter.choice_type_invalid",
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
    const auto it = std::find_if(
        adapters_.begin(), adapters_.end(),
        [&type_id](const DocumentAdapter& adapter) { return adapter.type_id == type_id; });
    return it == adapters_.end() ? nullptr : &*it;
}

DocumentAdapter* DocumentAdapterRegistry::Find(const std::string& type_id) {
    const auto it =
        std::find_if(adapters_.begin(), adapters_.end(),
                     [&type_id](DocumentAdapter& adapter) { return adapter.type_id == type_id; });
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
        .fields =
            {{"/id", "string", "editor.project.id", "text", true, false, {}},
             {"/render_style", "string", "editor.project.render_style", "select", true, false, {}},
             {"/battle_plugin",
              "string",
              "editor.project.battle_plugin",
              "select",
              false,
              false,
              {}},
             {"/plugins", "string[]", "editor.project.plugins", "list", true, false, {}},
             {"/data_roots", "path[]", "editor.project.data_roots", "list", true, false, {}},
             {"/material_document", "path", "editor.project.material", "resource", true, false, {}},
             {"/input_actions", "path", "editor.project.input", "resource", true, false, {}},
             {"/event_script", "path", "editor.project.events", "resource", true, false, {}},
             {"/localization", "path", "editor.project.localization", "resource", true, false, {}},
             {"/resource_manifest",
              "path",
              "editor.project.resources",
              "resource",
              true,
              false,
              {}},
             {"/navigation", "path", "editor.project.navigation", "resource", true, false, {}},
             {"/collision", "path", "editor.project.collision", "resource", true, false, {}},
             {"/camera", "path", "editor.project.camera", "resource", true, false, {}},
             {"/interaction", "path", "editor.project.interaction", "resource", true, false, {}}},
        .validate = ValidateManifestAdapter,
        .normalize_edit = {}});
    (void) registry.Register(DocumentAdapter{
        .type_id = "domain.event_script",
        .fields = {{"/events", "object[]", "editor.events.items", "event_list", true, false, {}}},
        .validate =
            [](const nlohmann::json& document) {
                return ValidateParsedDocument(document, "event_script", [](const auto& value) {
                    (void) domain::ParseEventScript(value);
                });
            },
        .normalize_edit = {}});
    (void) registry.Register(DocumentAdapter{
        .type_id = "core.navigation",
        .fields =
            {{"/width", "integer", "editor.navigation.width", "number", true, false, {}},
             {"/height", "integer", "editor.navigation.height", "number", true, false, {}},
             {"/walkable", "boolean[]", "editor.navigation.walkable", "grid", true, false, {}}},
        .validate =
            [](const nlohmann::json& document) {
                return ValidateParsedDocument(document, "navigation", [](const auto& value) {
                    (void) core::ParseNavigationGrid(value);
                });
            },
        .normalize_edit =
            [](std::string_view field_path, nlohmann::json& candidate) {
                if (field_path != "/width" && field_path != "/height")
                    return std::vector<Diagnostic>{};
                if (!candidate.contains("width") || !candidate.contains("height") ||
                    !candidate["width"].is_number_integer() ||
                    !candidate["height"].is_number_integer() || !candidate.contains("walkable") ||
                    !candidate["walkable"].is_array())
                    return std::vector<Diagnostic>{
                        {"project.navigation.resize_invalid", std::string(field_path)}};
                const auto width = candidate["width"].get<std::int64_t>();
                const auto height = candidate["height"].get<std::int64_t>();
                constexpr std::int64_t kMaxCells = 4096;
                if (width <= 0 || height <= 0 || width > kMaxCells || height > kMaxCells ||
                    width > kMaxCells / height)
                    return std::vector<Diagnostic>{
                        {"project.navigation.resize_range", std::string(field_path)}};
                const auto cell_count = static_cast<std::size_t>(width * height);
                while (candidate["walkable"].size() < cell_count)
                    candidate["walkable"].push_back(true);
                while (candidate["walkable"].size() > cell_count)
                    candidate["walkable"].erase(candidate["walkable"].end() - 1);
                return std::vector<Diagnostic>{};
            }});
    (void) registry.Register(DocumentAdapter{
        .type_id = "core.collision",
        .fields = {{"/obstacles",
                    "object[]",
                    "editor.collision.obstacles",
                    "aabb_list",
                    true,
                    false,
                    {}}},
        .validate =
            [](const nlohmann::json& document) {
                return ValidateParsedDocument(document, "collision", [](const auto& value) {
                    (void) core::ParseCollisionAabbs(value);
                });
            },
        .normalize_edit = {}});
    (void) registry.Register(DocumentAdapter{
        .type_id = "core.camera",
        .fields =
            {{"/third_person", "object", "editor.camera.third_person", "camera", true, false, {}},
             {"/fixed_regions",
              "object[]",
              "editor.camera.fixed_regions",
              "region_list",
              true,
              false,
              {}}},
        .validate =
            [](const nlohmann::json& document) {
                return ValidateParsedDocument(document, "camera", [](const auto& value) {
                    (void) core::ParseCameraRigData(value);
                });
            },
        .normalize_edit = {}});
    (void) registry.Register(DocumentAdapter{
        .type_id = "domain.interaction",
        .fields = {{"/interactions",
                    "object[]",
                    "editor.interaction.points",
                    "interaction_list",
                    true,
                    false,
                    {}}},
        .validate =
            [](const nlohmann::json& document) {
                return ValidateParsedDocument(document, "interaction", [](const auto& value) {
                    (void) domain::ParseInteractionPoints(value);
                });
            },
        .normalize_edit = {}});
    (void) registry.Register(DocumentAdapter{
        .type_id = "core.material",
        .fields = {{"/style_plugin_id",
                    "string",
                    "editor.material.style_plugin",
                    "select",
                    true,
                    false,
                    {}}},
        .validate =
            [](const nlohmann::json& document) {
                if (!document.is_object() || document.value("schema", 0) != 1 ||
                    !document.contains("style_plugin_id") ||
                    !document["style_plugin_id"].is_string() ||
                    document["style_plugin_id"].get<std::string>().empty())
                    return std::vector<Diagnostic>{{"project.document.invalid", "material"}};
                return std::vector<Diagnostic>{};
            },
        .normalize_edit = {}});
    (void) registry.Register(DocumentAdapter{
        .type_id = "app.input_actions",
        .fields =
            {{"/actions", "object[]", "editor.input.actions", "action_list", true, false, {}}},
        .validate =
            [](const nlohmann::json& document) {
                const auto parsed = core::ParseInputActionMap(document);
                if (parsed)
                    return std::vector<Diagnostic>{};
                return std::vector<Diagnostic>{{"project.document.invalid", "input_actions"}};
            },
        .normalize_edit = {}});
    (void) registry.Register(DocumentAdapter{
        .type_id = "domain.localization",
        .fields =
            {{"/strings", "object", "editor.localization.strings", "string_map", true, false, {}}},
        .validate =
            [](const nlohmann::json& document) {
                const auto parsed = domain::ParseLocalizationTable(document);
                if (parsed)
                    return std::vector<Diagnostic>{};
                return std::vector<Diagnostic>{{"project.document.invalid", "localization"}};
            },
        .normalize_edit = {}});
    (void) registry.Register(DocumentAdapter{
        .type_id = "project.resources",
        .fields = {{"/resources",
                    "object[]",
                    "editor.resources.items",
                    "resource_list",
                    true,
                    false,
                    {}}},
        .validate =
            [](const nlohmann::json& document) {
                if (!document.is_object() || document.value("schema", 0) != 1 ||
                    !document.contains("resources") || !document["resources"].is_array() ||
                    document["resources"].empty() || document["resources"].size() > 4096)
                    return std::vector<Diagnostic>{{"project.document.invalid", "resources"}};
                return std::vector<Diagnostic>{};
            },
        .normalize_edit = {}});
    return registry;
}

AdapterResult RegisterEditorDescriptor(DocumentAdapterRegistry& registry,
                                       const plugin::EditorDescriptor& descriptor,
                                       DocumentValidator validator,
                                       DocumentEditNormalizer normalizer) {
    DocumentAdapter adapter{.type_id = descriptor.type_id,
                            .fields = {},
                            .validate = std::move(validator),
                            .normalize_edit = std::move(normalizer)};
    adapter.fields.reserve(descriptor.fields.size());
    for (const auto& field : descriptor.fields)
        adapter.fields.push_back(FieldDescriptor{.path = field.path,
                                                 .value_type = field.value_type,
                                                 .label_key = field.label_key,
                                                 .recipe = field.recipe,
                                                 .required = field.required,
                                                 .read_only = field.read_only,
                                                 .choices = field.choices});
    return registry.Register(std::move(adapter));
}

ProjectWorkspace::ProjectWorkspace(std::filesystem::path root, DocumentAdapterRegistry adapters,
                                   const plugin::PluginRegistry* plugins)
    : root_(std::move(root)), adapters_(std::move(adapters)), plugins_(plugins) {}

void ProjectWorkspace::SetExternalDocuments(std::vector<DocumentDescriptor> documents) {
    external_documents_ = std::move(documents);
}

std::vector<DocumentDescriptor>
ProjectWorkspace::DescribeDocuments(const ProjectSnapshot& snapshot) const {
    struct Entry {
        const char* id;
        const char* path;
        const char* label_key;
    };
    const std::array entries = {
        Entry{"project.manifest", "project.json", "editor.document.project"},
        Entry{"domain.event_script", snapshot.manifest.event_script.c_str(),
              "editor.document.events"},
        Entry{"core.navigation", snapshot.manifest.navigation.c_str(),
              "editor.document.navigation"},
        Entry{"core.collision", snapshot.manifest.collision.c_str(), "editor.document.collision"},
        Entry{"core.camera", snapshot.manifest.camera.c_str(), "editor.document.camera"},
        Entry{"domain.interaction", snapshot.manifest.interaction.c_str(),
              "editor.document.interaction"},
        Entry{"core.material", snapshot.manifest.material_document.c_str(),
              "editor.document.material"},
        Entry{"app.input_actions", snapshot.manifest.input_actions.c_str(),
              "editor.document.input"},
        Entry{"domain.localization", snapshot.manifest.localization.c_str(),
              "editor.document.localization"},
        Entry{"project.resources", snapshot.manifest.resource_manifest.c_str(),
              "editor.document.resources"},
    };
    std::vector<DocumentDescriptor> result;
    result.reserve(entries.size());
    for (const auto& entry : entries) {
        result.push_back(DocumentDescriptor{entry.id, entry.path, entry.label_key,
                                            adapters_.Find(entry.id) != nullptr, entry.id});
    }
    result.insert(result.end(), external_documents_.begin(), external_documents_.end());
    return result;
}

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
    current_document_id_ = "project.manifest";
    pending_changes_.clear();
    original_documents_.clear();
    working_documents_.clear();
    original_documents_.emplace(current_document_id_, working_document_);
    working_documents_.emplace(current_document_id_, working_document_);
    snapshot_ = ProjectSnapshot{root_, manifest, revision_};
    result.snapshot = snapshot_;
    return result;
}

std::vector<Diagnostic> ProjectWorkspace::SelectDocument(std::string_view document_id) {
    std::vector<Diagnostic> diagnostics;
    if (!snapshot_) {
        Add(diagnostics, "project.workspace.not_open", "workspace");
        return diagnostics;
    }
    const auto documents = DescribeDocuments(*snapshot_);
    const auto it =
        std::find_if(documents.begin(), documents.end(),
                     [document_id](const auto& item) { return item.id == document_id; });
    if (it == documents.end()) {
        Add(diagnostics, "project.edit.document_unknown", std::string(document_id));
        return diagnostics;
    }
    if (it->id == current_document_id_)
        return diagnostics;
    nlohmann::json document;
    const auto working = working_documents_.find(it->id);
    if (working != working_documents_.end()) {
        document = working->second;
    } else if (!Read(root_ / it->path, document, diagnostics)) {
        return diagnostics;
    }
    const auto adapter_id = it->type_id.empty() ? it->id : it->type_id;
    if (adapters_.Find(adapter_id) == nullptr) {
        Add(diagnostics, "project.adapter.unknown_type", it->id);
        return diagnostics;
    }
    const auto validation = adapters_.Validate(adapter_id, document);
    diagnostics.insert(diagnostics.end(), validation.diagnostics.begin(),
                       validation.diagnostics.end());
    if (!diagnostics.empty())
        return diagnostics;
    if (!original_documents_.contains(it->id))
        original_documents_.emplace(it->id, document);
    working_documents_[it->id] = document;
    working_document_ = std::move(document);
    current_document_id_ = it->id;
    return diagnostics;
}

DiagnosticSet ProjectWorkspace::Diagnose(const ProjectSnapshot& snapshot) const {
    DiagnosticSet result;
    nlohmann::json events_document;
    nlohmann::json navigation_document;
    nlohmann::json collision_document;
    nlohmann::json camera_document;
    nlohmann::json interaction_document;
    nlohmann::json material_document;
    nlohmann::json input_document;
    nlohmann::json localization_document;
    nlohmann::json resource_document;
    const auto load = [this, &snapshot, &result](std::string_view document_id,
                                                 const std::string& relative_path,
                                                 nlohmann::json& document) {
        const auto working = working_documents_.find(std::string(document_id));
        if (working != working_documents_.end()) {
            document = working->second;
            return true;
        }
        return Read(snapshot.root / relative_path, document, result.diagnostics);
    };
    if (!load("domain.event_script", snapshot.manifest.event_script, events_document) ||
        !load("core.navigation", snapshot.manifest.navigation, navigation_document) ||
        !load("core.collision", snapshot.manifest.collision, collision_document) ||
        !load("core.camera", snapshot.manifest.camera, camera_document) ||
        !load("domain.interaction", snapshot.manifest.interaction, interaction_document) ||
        !load("core.material", snapshot.manifest.material_document, material_document) ||
        !load("app.input_actions", snapshot.manifest.input_actions, input_document) ||
        !load("domain.localization", snapshot.manifest.localization, localization_document) ||
        !load("project.resources", snapshot.manifest.resource_manifest, resource_document))
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
    validate("core.material", material_document);
    validate("app.input_actions", input_document);
    validate("domain.localization", localization_document);
    validate("project.resources", resource_document);
    for (const auto& document : external_documents_) {
        nlohmann::json external;
        if (!load(document.id, document.path.generic_string(), external))
            continue;
        const auto adapter_id = document.type_id.empty() ? document.id : document.type_id;
        const auto adapter_result = adapters_.Validate(adapter_id, external);
        for (const auto& diagnostic : adapter_result.diagnostics)
            Add(result.diagnostics, diagnostic.code, document.id + ":" + diagnostic.path);
    }
    if (plugins_ != nullptr) {
        for (const auto& issue :
             plugin::ValidateProjectPluginData(snapshot.manifest, *plugins_, snapshot.root))
            Add(result.diagnostics, issue.code, issue.path);
    }
    if (!result.diagnostics.empty())
        return result;
    try {
        const auto events = domain::ParseEventScript(events_document);
        const auto navigation = core::ParseNavigationGrid(navigation_document);
        const auto collision = core::ParseCollisionAabbs(collision_document);
        const auto camera = core::ParseCameraRigData(camera_document);
        const auto interactions = domain::ParseInteractionPoints(interaction_document);
        const auto localization = domain::ParseLocalizationTable(localization_document);
        domain::ValidateInteractionTargets(interactions, events);
        for (const auto& issue : domain::ValidateLocalizationCoverage(events, *localization.table))
            Add(result.diagnostics, "project.localization.missing_key", issue.key);
        result.event_count = events.events.size();
        result.interaction_count = interactions.size();
        result.collision_count = collision.size();
        result.navigation_width = navigation.width();
        result.navigation_height = navigation.height();
        result.camera_region_count = camera.fixed_regions.size();
    } catch (const std::exception&) {
        Add(result.diagnostics, "project.interaction.target_invalid",
            snapshot.manifest.interaction);
    }
    return result;
}

EditResult ProjectWorkspace::Apply(const EditCommand& command) {
    EditResult result{.revision = revision_, .changes = {}, .diagnostics = {}};
    if (!snapshot_.has_value()) {
        Add(result.diagnostics, "project.workspace.not_open", "workspace");
        return result;
    }
    if (command.document_id != current_document_id_) {
        Add(result.diagnostics, "project.edit.document_not_selected", command.document_id);
        return result;
    }
    const auto documents = DescribeDocuments(*snapshot_);
    const auto current =
        std::find_if(documents.begin(), documents.end(),
                     [this](const auto& document) { return document.id == current_document_id_; });
    const auto adapter_id = current == documents.end() || current->type_id.empty()
                                ? current_document_id_
                                : current->type_id;
    const auto* adapter = adapters_.Find(adapter_id);
    if (adapter == nullptr) {
        Add(result.diagnostics, "project.adapter.unknown_type", current_document_id_);
        return result;
    }
    const auto field_it =
        std::find_if(adapter->fields.begin(), adapter->fields.end(),
                     [&command](const auto& field) { return field.path == command.field_path; });
    if (field_it == adapter->fields.end()) {
        Add(result.diagnostics, "project.edit.field_not_editable", command.field_path);
        return result;
    }
    if (field_it->read_only) {
        Add(result.diagnostics, "project.edit.field_read_only", command.field_path);
        return result;
    }
    if (command.field_path.size() < 2 || command.field_path.front() != '/') {
        Add(result.diagnostics, "project.edit.field_path_invalid", command.field_path);
        return result;
    }
    nlohmann::json candidate = working_document_;
    const auto pointer = nlohmann::json::json_pointer(command.field_path);
    const nlohmann::json original_document = candidate;
    candidate[pointer] = command.value;
    if (adapter->normalize_edit) {
        const auto normalization = adapter->normalize_edit(command.field_path, candidate);
        result.diagnostics.insert(result.diagnostics.end(), normalization.begin(),
                                  normalization.end());
        if (!result.diagnostics.empty())
            return result;
    }
    const auto validation = adapters_.Validate(adapter_id, candidate);
    result.diagnostics = validation.diagnostics;
    if (!result.diagnostics.empty())
        return result;
    if (current_document_id_ == "core.material" &&
        candidate.value("style_plugin_id", std::string{}) != snapshot_->manifest.render_style) {
        Add(result.diagnostics, "project.material.style_mismatch", command.field_path);
        return result;
    }
    plugin::ProjectManifest manifest;
    if (current_document_id_ == "project.manifest" &&
        !ValidateManifestDocument(root_, candidate, manifest, result.diagnostics))
        return result;
    if (original_document == candidate)
        return result;
    working_document_ = std::move(candidate);
    working_documents_[current_document_id_] = working_document_;
    ++revision_;
    if (current_document_id_ == "project.manifest")
        snapshot_->manifest = std::move(manifest);
    snapshot_->revision = revision_;
    result.revision = revision_;
    for (const auto& field : adapter->fields) {
        const auto field_pointer = nlohmann::json::json_pointer(field.path);
        const auto old_value = original_document.contains(field_pointer)
                                   ? original_document.at(field_pointer)
                                   : nlohmann::json();
        const auto new_value = working_document_.contains(field_pointer)
                                   ? working_document_.at(field_pointer)
                                   : nlohmann::json();
        if (old_value == new_value)
            continue;
        Change change{.document_id = command.document_id,
                      .field_path = field.path,
                      .before = old_value,
                      .after = new_value,
                      .sequence = pending_changes_.size() + result.changes.size()};
        pending_changes_.push_back(change);
        result.changes.push_back(std::move(change));
    }
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
    const auto documents = DescribeDocuments(*snapshot_);
    struct SaveFile {
        std::string id;
        std::filesystem::path path;
        std::filesystem::path temporary;
        std::filesystem::path backup;
        bool backed_up = false;
        bool installed = false;
    };
    std::vector<std::string> ids;
    for (const auto& change : pending_changes_)
        if (std::find(ids.begin(), ids.end(), change.document_id) == ids.end())
            ids.push_back(change.document_id);

    std::vector<SaveFile> files;
    files.reserve(ids.size());
    std::error_code error;
    const auto remove_temporaries = [&files](std::error_code& cleanup_error) {
        for (const auto& file : files)
            std::filesystem::remove(file.temporary, cleanup_error);
    };
    for (const auto& id : ids) {
        const auto document_it = std::find_if(documents.begin(), documents.end(),
                                              [&id](const auto& item) { return item.id == id; });
        if (document_it == documents.end()) {
            Add(result.diagnostics, "project.edit.document_unknown", id);
            return result;
        }
        const auto working = working_documents_.find(id);
        if (working == working_documents_.end()) {
            Add(result.diagnostics, "project.save.document_unloaded", id);
            return result;
        }
        const auto document_path = root_ / document_it->path;
        SaveFile file{.id = id,
                      .path = document_path,
                      .temporary = std::filesystem::path(document_path.string() + ".tmp"),
                      .backup = {},
                      .backed_up = false,
                      .installed = false};
        if (std::filesystem::exists(file.temporary, error)) {
            Add(result.diagnostics, "project.save.temporary_exists", file.temporary.string());
            remove_temporaries(error);
            return result;
        }
        bool backup_available = false;
        for (std::size_t index = 0; index <= 8; ++index) {
            file.backup =
                index == 0
                    ? std::filesystem::path(file.path.string() + ".bak")
                    : std::filesystem::path(file.path.string() + ".bak." + std::to_string(index));
            if (!std::filesystem::exists(file.backup, error)) {
                backup_available = true;
                break;
            }
        }
        if (!backup_available) {
            Add(result.diagnostics, "project.save.backup_limit", file.path.string());
            remove_temporaries(error);
            return result;
        }
        std::ofstream output(file.temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            Add(result.diagnostics, "project.save.temporary_open", file.temporary.string());
            remove_temporaries(error);
            return result;
        }
        output << working->second.dump(2) << '\n';
        if (!output.good()) {
            remove_temporaries(error);
            Add(result.diagnostics, "project.save.temporary_write", file.temporary.string());
            return result;
        }
        files.push_back(std::move(file));
    }

    const auto rollback = [&files](std::error_code& rollback_error) {
        for (auto it = files.rbegin(); it != files.rend(); ++it) {
            if (it->installed)
                std::filesystem::remove(it->path, rollback_error);
            if (it->backed_up)
                std::filesystem::rename(it->backup, it->path, rollback_error);
            std::filesystem::remove(it->temporary, rollback_error);
        }
    };
    for (auto& file : files) {
        std::filesystem::rename(file.path, file.backup, error);
        if (error) {
            rollback(error);
            Add(result.diagnostics, "project.save.backup_create", file.path.string());
            return result;
        }
        file.backed_up = true;
    }
    for (auto& file : files) {
        std::filesystem::rename(file.temporary, file.path, error);
        if (error) {
            rollback(error);
            Add(result.diagnostics, "project.save.commit", file.path.string());
            return result;
        }
        file.installed = true;
    }
    result.backup = files.front().backup;
    for (const auto& file : files) {
        pending_changes_.erase(
            std::remove_if(pending_changes_.begin(), pending_changes_.end(),
                           [&file](const auto& change) { return change.document_id == file.id; }),
            pending_changes_.end());
        original_documents_[file.id] = working_documents_.at(file.id);
    }
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
