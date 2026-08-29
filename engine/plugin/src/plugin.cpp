#include "jrpgmaker/plugin/plugin.hpp"

#include <algorithm>
#include <fstream>
#include <unordered_set>

namespace jrpgmaker::plugin {
namespace {

ManifestParseResult Fail(std::string code, std::string message, std::string path) {
    return {.manifest = std::nullopt,
            .error = PluginError{
                .code = std::move(code), .message = std::move(message), .path = std::move(path)}};
}

bool IsPositiveInteger(const nlohmann::json& value) {
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>() > 0;
    }
    return value.is_number_integer() && value.get<std::int64_t>() > 0;
}

bool IsSafeRelativePath(const std::string& path) {
    return !path.empty() && path.find("..") == std::string::npos && path.front() != '/' &&
           path.front() != '\\';
}

bool IsEditorFieldPath(const std::string& path) {
    if (path.size() < 2 || path.front() != '/' || path.back() == '/')
        return false;
    for (std::size_t index = 1; index < path.size(); ++index) {
        if (path[index] == '/' && path[index - 1] == '/')
            return false;
        if (path[index] == '~' && index + 1 == path.size())
            return false;
        if (path[index] == '~' && path[index + 1] != '0' && path[index + 1] != '1')
            return false;
    }
    return true;
}

bool IsEditorValueType(const std::string& value_type) {
    return value_type == "string" || value_type == "integer" || value_type == "boolean" ||
           value_type == "select" || value_type == "path" || value_type == "string[]" ||
           value_type == "path[]";
}

bool IsEditorRole(const std::string& role) {
    return role == "text" || role == "number" || role == "toggle" || role == "select" ||
           role == "list" || role == "object" || role == "resource-reference";
}

bool IsInDataRoot(std::string_view path, std::string_view root) {
    if (path == root)
        return true;
    return path.size() > root.size() && path.starts_with(root) && path[root.size()] == '/';
}

bool IsCanonicalPathWithin(const std::filesystem::path& root,
                           const std::filesystem::path& candidate) {
    const std::filesystem::path relative = candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute())
        return relative.empty();
    for (const auto& component : relative) {
        if (component == "..")
            return false;
    }
    return true;
}

} // namespace

ManifestParseResult ParseManifest(const nlohmann::json& document) {
    if (!document.is_object()) {
        return Fail("manifest.type", "plugin manifest must be an object", "$");
    }
    if (!document.contains("schema") || !IsPositiveInteger(document["schema"])) {
        return Fail("manifest.schema", "schema must be a positive integer", "schema");
    }
    if (document["schema"] != 1u) {
        return Fail("manifest.schema", "unsupported plugin manifest schema", "schema");
    }
    for (const char* field :
         {"id", "type", "version", "engine_contract", "data_roots", "capabilities"}) {
        if (!document.contains(field)) {
            return Fail("manifest.required", "missing required manifest field", field);
        }
    }
    if (!document["id"].is_string() || document["id"].get<std::string>().empty()) {
        return Fail("manifest.id", "id must be a non-empty string", "id");
    }
    if (!document["type"].is_string()) {
        return Fail("manifest.type", "type must be a string", "type");
    }
    const std::string type = document["type"].get<std::string>();
    PluginType plugin_type;
    if (type == "battle") {
        plugin_type = PluginType::kBattle;
    } else if (type == "render_style") {
        plugin_type = PluginType::kRenderStyle;
    } else {
        return Fail("manifest.type", "unknown plugin type", "type");
    }
    if (!IsPositiveInteger(document["version"])) {
        return Fail("manifest.version", "version must be a positive integer", "version");
    }
    if (!IsPositiveInteger(document["engine_contract"])) {
        return Fail("manifest.contract", "engine_contract must be a positive integer",
                    "engine_contract");
    }
    if (!document["data_roots"].is_array() || !document["capabilities"].is_array()) {
        return Fail("manifest.array", "data_roots and capabilities must be arrays", "$");
    }

    PluginManifest manifest{.schema = 1u,
                            .id = document["id"].get<std::string>(),
                            .type = plugin_type,
                            .version = document["version"].get<std::uint32_t>(),
                            .engine_contract = document["engine_contract"].get<std::uint32_t>(),
                            .data_roots = {},
                            .capabilities = {}};
    for (const auto& root : document["data_roots"]) {
        if (!root.is_string() || !IsSafeRelativePath(root.get<std::string>())) {
            return Fail("manifest.data_roots", "data root must be a safe relative path",
                        "data_roots");
        }
        manifest.data_roots.push_back(root.get<std::string>());
    }
    for (const auto& capability : document["capabilities"]) {
        if (!capability.is_string() || capability.get<std::string>().empty()) {
            return Fail("manifest.capabilities", "capability must be a non-empty string",
                        "capabilities");
        }
        manifest.capabilities.push_back(capability.get<std::string>());
    }
    if (const auto error = ValidatePluginManifest(manifest); error.has_value())
        return Fail(error->code, error->message, error->path);
    return {.manifest = std::move(manifest), .error = std::nullopt};
}

EditorExtensionParseResult ParseEditorExtension(const nlohmann::json& document) {
    if (!document.is_object())
        return {.extension = std::nullopt,
                .error = PluginError{"editor.type", "editor extension must be an object", "$"}};
    if (!document.contains("schema") || !IsPositiveInteger(document["schema"]) ||
        document["schema"] != 1u)
        return {.extension = std::nullopt,
                .error = PluginError{"editor.schema", "unsupported editor extension schema", "schema"}};
    for (const char* field : {"plugin_id", "editor_contract", "documents", "locales", "icons"}) {
        if (!document.contains(field))
            return {.extension = std::nullopt,
                    .error = PluginError{"editor.required", "missing editor extension field", field}};
    }
    if (!document["plugin_id"].is_string() || document["plugin_id"].get<std::string>().empty() ||
        !IsPositiveInteger(document["editor_contract"]) || document["editor_contract"] != kPluginEditorContract ||
        !document["documents"].is_array() || !document["locales"].is_object() ||
        !document["icons"].is_string())
        return {.extension = std::nullopt,
                .error = PluginError{"editor.field", "invalid editor extension field", "$"}};

    EditorExtension extension{.schema = 1,
                              .plugin_id = document["plugin_id"].get<std::string>(),
                              .editor_contract = kPluginEditorContract,
                              .documents = {},
                              .locales = {},
                              .icons = document["icons"].get<std::string>()};
    if (extension.icons.empty() || document["documents"].empty() || document["documents"].size() > 64 ||
        document["locales"].size() > 16)
        return {.extension = std::nullopt,
                .error = PluginError{"editor.bounds", "editor extension exceeds its bounds", "$"}};
    std::unordered_set<std::string> document_ids;
    for (const auto& value : document["documents"]) {
        if (!value.is_object() || !value.contains("type_id") || !value["type_id"].is_string() ||
            value["type_id"].get<std::string>().empty() || !value.contains("roots") ||
            !value["roots"].is_array() || value["roots"].empty() || value["roots"].size() > 32 ||
            !value.contains("descriptor") || !value["descriptor"].is_string() ||
            value["descriptor"].get<std::string>().empty() ||
            !document_ids.insert(value["type_id"].get<std::string>()).second)
            return {.extension = std::nullopt,
                    .error = PluginError{"editor.document", "invalid or duplicate editor document", "documents"}};
        EditorDocumentExtension descriptor{.type_id = value["type_id"].get<std::string>(),
                                           .roots = {},
                                           .descriptor = value["descriptor"].get<std::string>()};
        std::unordered_set<std::string> roots;
        for (const auto& root : value["roots"]) {
            if (!root.is_string() || !IsSafeRelativePath(root.get<std::string>()) ||
                !roots.insert(root.get<std::string>()).second)
                return {.extension = std::nullopt,
                        .error = PluginError{"editor.root", "invalid or duplicate editor root", "documents"}};
            descriptor.roots.push_back(root.get<std::string>());
        }
        if (!IsSafeRelativePath(descriptor.descriptor))
            return {.extension = std::nullopt,
                    .error = PluginError{"editor.descriptor", "descriptor must be a safe relative path", "documents"}};
        extension.documents.push_back(std::move(descriptor));
    }
    for (const auto& [locale, path] : document["locales"].items()) {
        if (locale.empty() || !path.is_string() || !IsSafeRelativePath(path.get<std::string>()) ||
            !extension.locales.emplace(locale, path.get<std::string>()).second)
            return {.extension = std::nullopt,
                    .error = PluginError{"editor.locale", "invalid editor locale resource", "locales"}};
    }
    return {.extension = std::move(extension), .error = std::nullopt};
}

EditorDescriptorParseResult ParseEditorDescriptor(const nlohmann::json& document) {
    auto fail = [](std::string code, std::string message, std::string path) {
        return EditorDescriptorParseResult{
            .descriptor = std::nullopt,
            .error = PluginError{std::move(code), std::move(message), std::move(path)}};
    };
    if (!document.is_object())
        return fail("editor_descriptor.type", "editor descriptor must be an object", "$");
    if (!document.contains("schema") || !IsPositiveInteger(document["schema"]) ||
        document["schema"] != 1u)
        return fail("editor_descriptor.schema", "unsupported editor descriptor schema", "schema");
    if (!document.contains("type_id") || !document["type_id"].is_string() ||
        document["type_id"].get<std::string>().empty())
        return fail("editor_descriptor.type_id", "type_id must be a non-empty string", "type_id");
    if (!document.contains("fields") || !document["fields"].is_array() ||
        document["fields"].empty() || document["fields"].size() > 256)
        return fail("editor_descriptor.fields", "fields must contain 1 to 256 entries", "fields");

    EditorDescriptor descriptor{.schema = 1,
                                .type_id = document["type_id"].get<std::string>(),
                                .fields = {}};
    std::unordered_set<std::string> paths;
    for (const auto& value : document["fields"]) {
        if (!value.is_object())
            return fail("editor_descriptor.field", "field must be an object", "fields");
        for (const char* field : {"path", "value_type", "role", "label_key", "recipe"}) {
            if (!value.contains(field) || !value[field].is_string() || value[field].get<std::string>().empty())
                return fail("editor_descriptor.required", "missing required field property", field);
        }
        const std::string path = value["path"].get<std::string>();
        const std::string value_type = value["value_type"].get<std::string>();
        const std::string role = value["role"].get<std::string>();
        if (!IsEditorFieldPath(path) || !paths.insert(path).second)
            return fail("editor_descriptor.path", "field paths must be safe and unique", "path");
        if (!IsEditorValueType(value_type) || !IsEditorRole(role))
            return fail("editor_descriptor.kind", "unsupported field value type or role", "fields");
        if (value_type == "select" && role != "select")
            return fail("editor_descriptor.kind", "select fields must use the select role", "role");
        if (role == "select" && value_type != "select")
            return fail("editor_descriptor.kind", "select role requires select value type", "value_type");

        EditorFieldDescriptor field{.path = path,
                                    .value_type = value_type,
                                    .role = role,
                                    .label_key = value["label_key"].get<std::string>(),
                                    .recipe = value["recipe"].get<std::string>(),
                                    .required = false,
                                    .read_only = false,
                                    .choices = {}};
        if (value.contains("required")) {
            if (!value["required"].is_boolean())
                return fail("editor_descriptor.boolean", "required must be boolean", "required");
            field.required = value["required"].get<bool>();
        }
        if (value.contains("read_only")) {
            if (!value["read_only"].is_boolean())
                return fail("editor_descriptor.boolean", "read_only must be boolean", "read_only");
            field.read_only = value["read_only"].get<bool>();
        }
        if (value.contains("choices")) {
            if (value_type != "select" || !value["choices"].is_array() || value["choices"].empty() ||
                value["choices"].size() > 64)
                return fail("editor_descriptor.choices", "select choices are invalid or out of bounds", "choices");
            std::unordered_set<std::string> choices;
            for (const auto& choice : value["choices"]) {
                if (!choice.is_string() || choice.get<std::string>().empty() ||
                    !choices.insert(choice.get<std::string>()).second)
                    return fail("editor_descriptor.choices", "choices must be non-empty and unique", "choices");
                field.choices.push_back(choice.get<std::string>());
            }
        } else if (value_type == "select") {
            return fail("editor_descriptor.choices", "select fields require choices", "choices");
        }
        descriptor.fields.push_back(std::move(field));
    }
    return {.descriptor = std::move(descriptor), .error = std::nullopt};
}

std::optional<PluginError> ValidateEditorExtension(const EditorExtension& extension,
                                                   const PluginManifest& manifest) {
    if (extension.schema != 1 || extension.editor_contract != kPluginEditorContract)
        return PluginError{"editor.contract", "unsupported editor extension contract", "editor_contract"};
    if (extension.plugin_id != manifest.id)
        return PluginError{"editor.plugin_id", "editor extension plugin_id does not match manifest", "plugin_id"};
    for (const auto& document : extension.documents) {
        for (const auto& root : document.roots) {
            bool contained = false;
            for (const auto& data_root : manifest.data_roots)
                if (IsInDataRoot(root, data_root)) {
                    contained = true;
                    break;
                }
            if (!contained)
                return PluginError{"editor.root", "editor root exceeds plugin data roots", "documents"};
        }
    }
    return std::nullopt;
}

std::vector<PluginError> ValidateEditorExtensionResources(const EditorExtension& extension,
                                                          const PluginManifest& manifest,
                                                          const std::filesystem::path& plugin_root) {
    std::vector<PluginError> issues;
    const auto append = [&issues](PluginError issue) {
        if (issues.size() < 128u)
            issues.push_back(std::move(issue));
    };
    if (const auto error = ValidateEditorExtension(extension, manifest); error.has_value()) {
        append(*error);
        return issues;
    }
    std::error_code error;
    const auto canonical_root = std::filesystem::weakly_canonical(plugin_root, error);
    if (error) {
        append(PluginError{"editor.resource.root", "plugin root cannot be resolved", "$"});
        return issues;
    }
    std::size_t total_bytes = 0;
    const auto check = [&](const std::string& relative, std::string_view kind) {
        if (!IsSafeRelativePath(relative)) {
            append(PluginError{"editor.resource.path", "editor resource path is unsafe",
                               std::string(kind)});
            return;
        }
        const auto path = plugin_root / relative;
        const auto canonical_path = std::filesystem::weakly_canonical(path, error);
        if (error || !IsCanonicalPathWithin(canonical_root, canonical_path)) {
            append(PluginError{"editor.resource.path", "editor resource escapes plugin root",
                               relative});
            return;
        }
        if (!std::filesystem::is_regular_file(canonical_path, error) || error) {
            append(PluginError{"editor.resource.missing", "editor resource is missing", relative});
            return;
        }
        const auto size = std::filesystem::file_size(canonical_path, error);
        if (error || size > kMaxPluginValidationFileBytes) {
            append(PluginError{"editor.resource.file_size",
                               "editor resource exceeds 256 KiB", relative});
            return;
        }
        if (total_bytes > kMaxPluginValidationTotalBytes - static_cast<std::size_t>(size))
            append(PluginError{"editor.resource.byte_budget",
                               "editor resources exceed the byte budget", extension.plugin_id});
        else
            total_bytes += static_cast<std::size_t>(size);
    };
    check(extension.icons, "icons");
    for (const auto& [locale, path] : extension.locales)
        check(path, "locales/" + locale);
    for (const auto& document : extension.documents)
        check(document.descriptor, "documents/" + document.type_id);
    return issues;
}

std::optional<PluginError> ValidatePluginManifest(const PluginManifest& manifest) {
    if (manifest.schema != 1u)
        return PluginError{"manifest.schema", "unsupported plugin manifest schema", "schema"};
    if (manifest.id.empty())
        return PluginError{"manifest.id", "id must be a non-empty string", "id"};
    if (manifest.type != PluginType::kBattle && manifest.type != PluginType::kRenderStyle)
        return PluginError{"manifest.type", "unknown plugin type", "type"};
    if (manifest.version == 0u)
        return PluginError{"manifest.version", "version must be a positive integer", "version"};
    if (manifest.engine_contract != kPluginEngineContract) {
        return PluginError{"manifest.contract", "plugin engine contract is incompatible",
                           "engine_contract"};
    }
    std::unordered_set<std::string> roots;
    for (const std::string& root : manifest.data_roots) {
        if (!IsSafeRelativePath(root))
            return PluginError{"manifest.data_roots", "data root must be a safe relative path",
                               "data_roots"};
        if (!roots.insert(root).second)
            return PluginError{"manifest.data_roots", "data roots must be unique", "data_roots"};
    }
    std::unordered_set<std::string> capabilities;
    for (const std::string& capability : manifest.capabilities) {
        if (capability.empty())
            return PluginError{"manifest.capabilities", "capability must be a non-empty string",
                               "capabilities"};
        if (!capabilities.insert(capability).second)
            return PluginError{"manifest.capabilities", "capabilities must be unique",
                               "capabilities"};
    }
    return std::nullopt;
}

ProjectManifestParseResult ParseProjectManifest(const nlohmann::json& document) {
    if (!document.is_object() || !document.contains("schema") ||
        !IsPositiveInteger(document["schema"]) || document["schema"] != 1u) {
        return {.manifest = std::nullopt,
                .error =
                    PluginError{"project.schema", "unsupported project manifest schema", "schema"}};
    }
    for (const char* field : {"id", "render_style", "plugins", "data_roots"}) {
        if (!document.contains(field)) {
            return {.manifest = std::nullopt,
                    .error =
                        PluginError{"project.required", "missing required project field", field}};
        }
    }
    for (const char* field : {"id", "render_style"}) {
        if (!document[field].is_string() || document[field].get<std::string>().empty()) {
            return {.manifest = std::nullopt,
                    .error =
                        PluginError{"project.field", "project field must be non-empty", field}};
        }
    }
    if (!document["plugins"].is_array() || !document["data_roots"].is_array()) {
        return {.manifest = std::nullopt,
                .error =
                    PluginError{"project.array", "plugins and data_roots must be arrays", "$"}};
    }

    ProjectManifest project{.schema = 1u,
                            .id = document["id"].get<std::string>(),
                            .render_style = document["render_style"].get<std::string>(),
                            .battle_plugin = {},
                            .plugins = {},
                            .data_roots = {}};
    std::unordered_set<std::string> ids;
    for (const auto& value : document["plugins"]) {
        if (!value.is_string() || value.get<std::string>().empty() ||
            !ids.insert(value.get<std::string>()).second) {
            return {.manifest = std::nullopt,
                    .error = PluginError{"project.plugins",
                                         "plugin IDs must be unique non-empty strings", "plugins"}};
        }
        project.plugins.push_back(value.get<std::string>());
    }
    if (ids.find(project.render_style) == ids.end()) {
        return {.manifest = std::nullopt,
                .error = PluginError{"project.render_style",
                                     "render_style must be listed in plugins", "render_style"}};
    }
    if (document.contains("battle_plugin")) {
        if (!document["battle_plugin"].is_string()) {
            return {.manifest = std::nullopt,
                    .error = PluginError{"project.battle_plugin", "battle_plugin must be a string",
                                         "battle_plugin"}};
        }
        project.battle_plugin = document["battle_plugin"].get<std::string>();
        if (!project.battle_plugin.empty() && ids.find(project.battle_plugin) == ids.end()) {
            return {.manifest = std::nullopt,
                    .error =
                        PluginError{"project.battle_plugin",
                                    "battle_plugin must be listed in plugins", "battle_plugin"}};
        }
    }
    for (const auto& value : document["data_roots"]) {
        if (!value.is_string() || !IsSafeRelativePath(value.get<std::string>())) {
            return {.manifest = std::nullopt,
                    .error = PluginError{"project.data_roots",
                                         "data root must be a safe relative path", "data_roots"}};
        }
        project.data_roots.push_back(value.get<std::string>());
    }
    if (document.contains("material_document")) {
        if (!document["material_document"].is_string() ||
            !IsSafeRelativePath(document["material_document"].get<std::string>())) {
            return {.manifest = std::nullopt,
                    .error = PluginError{"project.material_document",
                                         "material_document must be a safe relative path",
                                         "material_document"}};
        }
        project.material_document = document["material_document"].get<std::string>();
    }
    if (document.contains("input_actions")) {
        if (!document["input_actions"].is_string() ||
            !IsSafeRelativePath(document["input_actions"].get<std::string>())) {
            return {.manifest = std::nullopt,
                    .error =
                        PluginError{"project.input_actions",
                                    "input_actions must be a safe relative path", "input_actions"}};
        }
        project.input_actions = document["input_actions"].get<std::string>();
    }
    for (const char* field : {"event_script", "localization", "resource_manifest", "navigation",
                              "collision", "camera", "interaction"}) {
        if (!document.contains(field))
            continue;
        if (!document[field].is_string() ||
            !IsSafeRelativePath(document[field].get<std::string>())) {
            return {.manifest = std::nullopt,
                    .error = PluginError{"project.path", "project data path must be safe", field}};
        }
        if (std::string(field) == "event_script")
            project.event_script = document[field].get<std::string>();
        else if (std::string(field) == "localization")
            project.localization = document[field].get<std::string>();
        else if (std::string(field) == "resource_manifest")
            project.resource_manifest = document[field].get<std::string>();
        else if (std::string(field) == "navigation")
            project.navigation = document[field].get<std::string>();
        else if (std::string(field) == "collision")
            project.collision = document[field].get<std::string>();
        else if (std::string(field) == "camera")
            project.camera = document[field].get<std::string>();
        else
            project.interaction = document[field].get<std::string>();
    }
    return {.manifest = std::move(project), .error = std::nullopt};
}

std::optional<PluginError> ValidateProjectPlugins(const ProjectManifest& project,
                                                  const PluginRegistry& registry) {
    for (const std::string& id : project.plugins) {
        const auto manifest = registry.FindManifest(id);
        if (!manifest.has_value()) {
            return PluginError{"project.plugin_missing", "project plugin is not registered", id};
        }
    }
    const auto render_manifest = registry.FindManifest(project.render_style);
    if (!render_manifest.has_value() || render_manifest->type != PluginType::kRenderStyle) {
        return PluginError{"project.render_style",
                           "project render_style is not a registered render plugin",
                           project.render_style};
    }
    if (!project.battle_plugin.empty()) {
        const auto battle_manifest = registry.FindManifest(project.battle_plugin);
        if (!battle_manifest.has_value() || battle_manifest->type != PluginType::kBattle) {
            return PluginError{"project.battle_plugin",
                               "project battle_plugin is not a registered battle plugin",
                               project.battle_plugin};
        }
    }
    return std::nullopt;
}

std::optional<PluginError> ValidateProjectDataRoots(const ProjectManifest& project,
                                                    const std::filesystem::path& project_root) {
    for (const std::string& relative : project.data_roots) {
        const std::filesystem::path path = project_root / relative;
        std::error_code error;
        if (!std::filesystem::is_directory(path, error)) {
            return PluginError{"project.data_root_missing", "project data root does not exist",
                               relative};
        }
    }
    return std::nullopt;
}

std::vector<PluginError> ValidateProjectPluginData(const ProjectManifest& project,
                                                   const PluginRegistry& registry,
                                                   const std::filesystem::path& project_root) {
    std::vector<PluginError> issues;
    const auto append = [&issues](PluginError issue) {
        if (issues.size() < 128u)
            issues.push_back(std::move(issue));
    };
    if (const auto error = ValidateProjectPlugins(project, registry); error.has_value())
        append(*error);
    if (const auto error = ValidateProjectDataRoots(project, project_root); error.has_value())
        append(*error);

    for (const std::string& id : project.plugins) {
        const auto manifest = registry.FindManifest(id);
        if (!manifest.has_value())
            continue;
        const auto created = registry.Create(id, manifest->type);
        if (!created) {
            append(created.error.value_or(PluginError{
                "plugin.validator.create", "plugin validator could not be created", id}));
            continue;
        }

        std::size_t reads = 0;
        std::size_t total_bytes = 0;
        const auto read_file = [&, manifest = *manifest](std::string_view requested) {
            PluginDataReadResult result;
            const std::string relative(requested);
            if (!IsSafeRelativePath(relative) ||
                std::none_of(
                    manifest.data_roots.begin(), manifest.data_roots.end(),
                    [&](const std::string& root) { return IsInDataRoot(relative, root); })) {
                result.error =
                    PluginError{"plugin.validator.path", "data file is outside plugin roots",
                                id + ":" + relative};
                return result;
            }
            if (reads >= kMaxPluginValidationFiles) {
                result.error = PluginError{"plugin.validator.file_budget",
                                           "plugin validator file budget exceeded", id};
                return result;
            }
            const std::filesystem::path path = project_root / std::filesystem::path(relative);
            std::error_code error;
            const auto canonical_project_root =
                std::filesystem::weakly_canonical(project_root, error);
            if (error) {
                result.error =
                    PluginError{"plugin.validator.path", "project root cannot be resolved", id};
                return result;
            }
            const auto canonical_path = std::filesystem::weakly_canonical(path, error);
            if (error) {
                result.error = PluginError{"plugin.validator.path",
                                           "plugin data path cannot be resolved", relative};
                return result;
            }
            if (!IsCanonicalPathWithin(canonical_project_root, canonical_path)) {
                result.error = PluginError{"plugin.validator.path",
                                           "plugin data path escapes project root", relative};
                return result;
            }
            bool within_declared_root = false;
            for (const std::string& root : manifest.data_roots) {
                if (!IsInDataRoot(relative, root))
                    continue;
                const auto canonical_root =
                    std::filesystem::weakly_canonical(project_root / root, error);
                if (!error && IsCanonicalPathWithin(canonical_root, canonical_path)) {
                    within_declared_root = true;
                    break;
                }
            }
            if (!within_declared_root) {
                result.error =
                    PluginError{"plugin.validator.path", "plugin data path escapes declared roots",
                                id + ":" + relative};
                return result;
            }
            const auto size = std::filesystem::file_size(path, error);
            if (error || size > kMaxPluginValidationFileBytes) {
                result.error =
                    PluginError{"plugin.validator.file_size",
                                "plugin data file is missing or exceeds 256 KiB", relative};
                return result;
            }
            if (total_bytes > kMaxPluginValidationTotalBytes - static_cast<std::size_t>(size)) {
                result.error = PluginError{"plugin.validator.byte_budget",
                                           "plugin validator byte budget exceeded", id};
                return result;
            }
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                result.error = PluginError{"plugin.validator.open",
                                           "plugin data file cannot be opened", relative};
                return result;
            }
            result.bytes.resize(static_cast<std::size_t>(size));
            file.read(reinterpret_cast<char*>(result.bytes.data()),
                      static_cast<std::streamsize>(result.bytes.size()));
            if (!file && !result.bytes.empty()) {
                result.bytes.clear();
                result.error = PluginError{"plugin.validator.read",
                                           "plugin data file cannot be read", relative};
                return result;
            }
            ++reads;
            total_bytes += result.bytes.size();
            return result;
        };

        PluginValidationResult validation;
        try {
            validation = created.instance->ValidateData(
                PluginValidationContext{.manifest = *manifest, .read_file = read_file});
        } catch (...) {
            append(PluginError{"plugin.validator.exception", "plugin validator failed", id});
            continue;
        }
        for (PluginError issue : validation.issues) {
            if (issue.code.empty())
                issue.code = "plugin.validator.issue";
            const std::string prefix = id + ":";
            if (!issue.path.starts_with(prefix))
                issue.path = id + (issue.path.empty() ? std::string{} : ":" + issue.path);
            append(std::move(issue));
        }
    }
    return issues;
}

std::optional<PluginError> PluginRegistry::Register(PluginManifest manifest, Factory factory) {
    if (manifest.id.empty()) {
        return PluginError{"registry.id", "plugin id must not be empty", "id"};
    }
    if (!factory) {
        return PluginError{"registry.factory", "plugin factory must not be empty", manifest.id};
    }
    if (entries_.size() >= kMaxPlugins) {
        return PluginError{"registry.capacity", "plugin registry capacity exceeded", manifest.id};
    }
    const auto duplicate =
        std::find_if(entries_.begin(), entries_.end(),
                     [&manifest](const Entry& entry) { return entry.manifest.id == manifest.id; });
    if (duplicate != entries_.end()) {
        return PluginError{"registry.duplicate", "plugin id is already registered", manifest.id};
    }
    if (const auto error = ValidatePluginManifest(manifest); error.has_value()) {
        PluginError registry_error = *error;
        if (registry_error.code == "manifest.contract")
            registry_error.code = "registry.contract";
        registry_error.path = manifest.id;
        return registry_error;
    }
    entries_.push_back({.manifest = std::move(manifest), .factory = std::move(factory)});
    return std::nullopt;
}

PluginCreateResult PluginRegistry::Create(const std::string& id, PluginType type) const {
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [&id](const Entry& entry) { return entry.manifest.id == id; });
    if (it == entries_.end()) {
        return {.instance = nullptr,
                .error = PluginError{"registry.missing", "plugin id is not registered", id}};
    }
    if (it->manifest.type != type) {
        return {.instance = nullptr,
                .error = PluginError{"registry.type", "plugin type does not match request", id}};
    }
    try {
        return {.instance = it->factory(), .error = std::nullopt};
    } catch (...) {
        return {.instance = nullptr,
                .error = PluginError{"registry.factory_error", "plugin factory failed", id}};
    }
}

std::optional<PluginManifest> PluginRegistry::FindManifest(const std::string& id) const {
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [&id](const Entry& entry) { return entry.manifest.id == id; });
    return it == entries_.end() ? std::nullopt : std::optional<PluginManifest>(it->manifest);
}

PluginCreateResult CreateProjectRenderStyle(const ProjectManifest& project,
                                            const PluginRegistry& registry) {
    return registry.Create(project.render_style, PluginType::kRenderStyle);
}

PluginCreateResult CreateProjectBattlePlugin(const ProjectManifest& project,
                                             const PluginRegistry& registry) {
    if (project.battle_plugin.empty()) {
        return {.instance = nullptr,
                .error = PluginError{"project.battle_plugin",
                                     "project does not select a battle plugin", "battle_plugin"}};
    }
    return registry.Create(project.battle_plugin, PluginType::kBattle);
}

} // namespace jrpgmaker::plugin
