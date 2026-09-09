#include "jrpgmaker/editor/editor_plugin_registry.hpp"

#include <algorithm>
#include <fstream>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

namespace jrpgmaker::editor {
namespace {

constexpr std::uintmax_t kMaxManifestBytes = 256u * 1024u;

void Add(std::vector<project::Diagnostic>& diagnostics, std::string code, std::string path) {
    diagnostics.push_back({std::move(code), std::move(path)});
}

bool IsSafeRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute())
        return false;
    for (const auto& component : path)
        if (component == "..")
            return false;
    return true;
}

std::optional<nlohmann::json> ReadJson(const std::filesystem::path& root,
                                       const std::filesystem::path& relative,
                                       std::vector<project::Diagnostic>& diagnostics,
                                       std::string_view prefix) {
    if (!IsSafeRelativePath(relative)) {
        Add(diagnostics, std::string(prefix) + ".path", relative.generic_string());
        return std::nullopt;
    }
    const auto path = root / relative;
    std::error_code error;
    const auto canonical_root = std::filesystem::weakly_canonical(root, error);
    if (error) {
        Add(diagnostics, std::string(prefix) + ".path", relative.generic_string());
        return std::nullopt;
    }
    const auto canonical_path = std::filesystem::weakly_canonical(path, error);
    if (error || !plugin::IsCanonicalPathWithin(canonical_root, canonical_path)) {
        Add(diagnostics, std::string(prefix) + ".path", relative.generic_string());
        return std::nullopt;
    }
    const auto size = std::filesystem::file_size(canonical_path, error);
    if (error) {
        Add(diagnostics, std::string(prefix) + ".open", relative.generic_string());
        return std::nullopt;
    }
    if (size > kMaxManifestBytes) {
        Add(diagnostics, std::string(prefix) + ".size", relative.generic_string());
        return std::nullopt;
    }
    try {
        std::ifstream input(canonical_path);
        nlohmann::json document;
        input >> document;
        return document;
    } catch (const std::exception&) {
        Add(diagnostics, std::string(prefix) + ".parse", relative.generic_string());
        return std::nullopt;
    }
}

} // namespace

EditorPluginRegistryAssembly
AssembleEditorPluginRegistry(const std::filesystem::path& project_root,
                             std::span<const EditorPluginFactoryBinding> compiled_factories) {
    EditorPluginRegistryAssembly result;
    if (compiled_factories.size() > plugin::PluginRegistry::kMaxPlugins) {
        Add(result.diagnostics, "editor.plugin.factory_catalog_limit", "plugins");
        return result;
    }
    std::unordered_set<std::string> factory_ids;
    for (const auto& binding : compiled_factories) {
        if (binding.id.empty() || !factory_ids.insert(binding.id).second) {
            Add(result.diagnostics, "editor.plugin.factory_catalog_invalid", binding.id);
            return result;
        }
    }

    const auto project_document =
        ReadJson(project_root, "project.json", result.diagnostics, "project.file");
    if (!project_document)
        return result;
    const auto parsed_project = plugin::ParseProjectManifest(*project_document);
    if (!parsed_project) {
        Add(result.diagnostics, parsed_project.error->code, parsed_project.error->path);
        return result;
    }

    auto registry = std::make_shared<plugin::PluginRegistry>();
    for (const auto& plugin_id : parsed_project.manifest->plugins) {
        const auto binding =
            std::find_if(compiled_factories.begin(), compiled_factories.end(),
                         [&plugin_id](const auto& candidate) { return candidate.id == plugin_id; });
        if (binding == compiled_factories.end()) {
            Add(result.diagnostics, "project.plugin_missing", plugin_id);
            continue;
        }
        const auto manifest_document =
            ReadJson(project_root, binding->manifest_path, result.diagnostics, "plugin.manifest");
        if (!manifest_document)
            continue;
        const auto parsed_manifest = plugin::ParseManifest(*manifest_document);
        if (!parsed_manifest) {
            Add(result.diagnostics, parsed_manifest.error->code,
                binding->manifest_path.generic_string() + ":" + parsed_manifest.error->path);
            continue;
        }
        if (parsed_manifest.manifest->id != plugin_id) {
            Add(result.diagnostics, "editor.plugin.manifest_id_mismatch",
                binding->manifest_path.generic_string());
            continue;
        }
        if (const auto error = registry->Register(*parsed_manifest.manifest, binding->factory);
            error.has_value())
            Add(result.diagnostics, error->code, error->path);
    }
    if (!result.diagnostics.empty())
        return result;
    if (const auto error = plugin::ValidateProjectPlugins(*parsed_project.manifest, *registry);
        error.has_value()) {
        Add(result.diagnostics, error->code, error->path);
        return result;
    }
    result.registry = std::move(registry);
    return result;
}

} // namespace jrpgmaker::editor
