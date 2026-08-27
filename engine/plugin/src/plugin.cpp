#include "jrpgmaker/plugin/plugin.hpp"

#include <algorithm>
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
    return {.manifest = std::move(manifest), .error = std::nullopt};
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
    if (manifest.engine_contract != 1u) {
        return PluginError{"registry.contract", "plugin engine contract is incompatible",
                           manifest.id};
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
