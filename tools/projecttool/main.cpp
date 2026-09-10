// projecttool CLI: creates, opens and validates a project workspace.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/plugin/plugin.hpp"
#include "jrpgmaker/plugins/register.hpp"
#include "jrpgmaker/project/plugin_registry.hpp"
#include "jrpgmaker/project/transient_data_adapter.hpp"
#include "jrpgmaker/project/workspace.hpp"

namespace {

constexpr std::size_t kMaxFiles = 4096;
constexpr std::uintmax_t kMaxBytes = 64u * 1024u * 1024u;

void PrintDiagnostics(const std::filesystem::path& root,
                      const std::vector<jrpgmaker::project::Diagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics)
        std::cerr << root / diagnostic.path << ": " << diagnostic.code << '\n';
}

struct ProjectContext {
    std::shared_ptr<const jrpgmaker::plugin::PluginRegistry> plugins;
    std::unique_ptr<jrpgmaker::project::ProjectWorkspace> workspace;
};

std::optional<ProjectContext>
CreateProjectContext(const std::filesystem::path& root,
                     jrpgmaker::project::DocumentAdapterRegistry adapters =
                         jrpgmaker::project::CreateDefaultDocumentAdapters(),
                     std::shared_ptr<const jrpgmaker::plugin::PluginRegistry> plugins = {}) {
    if (!plugins) {
        auto assembled = jrpgmaker::project::AssembleProjectPluginRegistry(
            root, jrpgmaker::plugins::CompiledSamplePlugins());
        if (!assembled) {
            PrintDiagnostics(root, assembled.diagnostics);
            return std::nullopt;
        }
        plugins = std::move(assembled.registry);
    }
    ProjectContext context;
    context.plugins = std::move(plugins);
    context.workspace = std::make_unique<jrpgmaker::project::ProjectWorkspace>(
        root, std::move(adapters), context.plugins.get());
    return context;
}

bool CopyTreeBounded(const std::filesystem::path& source, const std::filesystem::path& target,
                     std::size_t& file_count, std::uintmax_t& total_bytes) {
    std::error_code error;
    if (!std::filesystem::is_directory(source, error))
        return false;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(source, error)) {
        if (error)
            return false;
        if (entry.is_symlink(error) || error)
            return false;
        const auto relative = std::filesystem::relative(entry.path(), source, error);
        if (error)
            return false;
        const auto destination = target / relative;
        if (entry.is_directory(error)) {
            std::filesystem::create_directories(destination, error);
            if (error)
                return false;
            continue;
        }
        if (error || !entry.is_regular_file(error) || error)
            return false;
        const auto size = entry.file_size(error);
        if (error || file_count >= kMaxFiles || size > kMaxBytes - total_bytes)
            return false;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error ||
            !std::filesystem::copy_file(entry.path(), destination,
                                        std::filesystem::copy_options::none, error) ||
            error)
            return false;
        ++file_count;
        total_bytes += size;
    }
    return true;
}

bool CopyFile(const std::filesystem::path& source, const std::filesystem::path& target,
              std::size_t& file_count, std::uintmax_t& total_bytes) {
    std::error_code error;
    if (std::filesystem::is_symlink(source, error) || error ||
        !std::filesystem::is_regular_file(source, error))
        return false;
    const auto size = std::filesystem::file_size(source, error);
    if (error || file_count >= kMaxFiles || size > kMaxBytes - total_bytes)
        return false;
    std::filesystem::create_directories(target.parent_path(), error);
    if (error ||
        !std::filesystem::copy_file(source, target, std::filesystem::copy_options::none, error) ||
        error)
        return false;
    ++file_count;
    total_bytes += size;
    return true;
}

bool CreateProject(const std::filesystem::path& output,
                   const std::filesystem::path& template_root) {
    std::error_code error;
    if (!std::filesystem::is_directory(template_root, error)) {
        std::cerr << "template root is not a directory: " << template_root.string() << '\n';
        return false;
    }
    if (std::filesystem::exists(output, error)) {
        std::cerr << "refusing to overwrite an existing project directory: " << output.string()
                  << '\n';
        return false;
    }
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path(), error);
        if (error)
            return false;
    }

    const auto staging = output.parent_path() / ("." + output.filename().string() + ".staging");
    if (std::filesystem::exists(staging, error)) {
        std::cerr << "staging directory already exists: " << staging.string() << '\n';
        return false;
    }
    std::filesystem::create_directories(staging, error);
    if (error)
        return false;
    std::size_t file_count = 0;
    std::uintmax_t total_bytes = 0;
    bool ok =
        CopyTreeBounded(template_root / "assets", staging / "assets", file_count, total_bytes) &&
        CopyTreeBounded(template_root / "plugins", staging / "plugins", file_count, total_bytes) &&
        CopyFile(template_root / "assets/data/project_demo.json", staging / "project.json",
                 file_count, total_bytes);
    if (!ok) {
        std::filesystem::remove_all(staging, error);
        std::cerr << "template copy exceeded bounds or contains an unsupported entry\n";
        return false;
    }
    std::filesystem::rename(staging, output, error);
    if (error) {
        std::filesystem::remove_all(staging, error);
        return false;
    }
    std::filesystem::remove_all(staging, error);
    std::cout << output.string() << ": project created (" << file_count << " files, " << total_bytes
              << " bytes)\n";
    return true;
}

bool OpenProject(const std::filesystem::path& root, bool validate) {
    auto context = CreateProjectContext(root);
    if (!context)
        return false;
    auto& workspace = *context->workspace;
    const auto opened = workspace.Open();
    if (!opened) {
        PrintDiagnostics(root, opened.diagnostics);
        return false;
    }
    const auto& snapshot = *opened.snapshot;
    std::cout << root.string() << ": opened project id=" << snapshot.manifest.id
              << " render_style=" << snapshot.manifest.render_style << '\n';
    if (!validate)
        return true;
    const auto diagnosed = workspace.Diagnose(snapshot);
    if (!diagnosed) {
        PrintDiagnostics(root, diagnosed.diagnostics);
        return false;
    }
    std::cout << root.string() << ": project manifest clean (id=" << snapshot.manifest.id
              << ", plugins=" << snapshot.manifest.plugins.size() << ")\n";
    return true;
}

bool LoadJsonDocument(const std::filesystem::path& path, nlohmann::json& document) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << path.string() << ": cannot open JSON file\n";
        return false;
    }
    try {
        file >> document;
        return true;
    } catch (const std::exception& error) {
        std::cerr << path.string() << ": JSON parse error: " << error.what() << '\n';
        return false;
    }
}

bool EditProject(const std::filesystem::path& root, const std::filesystem::path& patch_path,
                 bool write) {
    nlohmann::json patch;
    if (!LoadJsonDocument(patch_path, patch) || !patch.is_object() || patch.empty()) {
        std::cerr << "manifest patch must be a non-empty object\n";
        return false;
    }
    auto context = CreateProjectContext(root);
    if (!context)
        return false;
    auto& workspace = *context->workspace;
    const auto opened = workspace.Open();
    if (!opened) {
        PrintDiagnostics(root, opened.diagnostics);
        return false;
    }
    std::vector<jrpgmaker::project::Change> changes;
    std::uint64_t current_revision = opened.snapshot->revision;
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        const auto edit = workspace.Apply({"project.manifest", "/" + it.key(), it.value()});
        if (!edit) {
            PrintDiagnostics(root, edit.diagnostics);
            return false;
        }
        current_revision = edit.revision;
        changes.insert(changes.end(), edit.changes.begin(), edit.changes.end());
    }
    if (changes.empty()) {
        std::cout << "project.json: no changes\n";
        return true;
    }
    for (const auto& change : changes)
        std::cout << change.field_path << ": " << change.before.dump() << " -> "
                  << change.after.dump() << '\n';
    if (!write)
        return true;
    const auto plan = workspace.PrepareSave(current_revision);
    if (!plan) {
        PrintDiagnostics(root, plan.diagnostics);
        return false;
    }
    const auto committed = workspace.Commit(*plan.token);
    if (!committed) {
        PrintDiagnostics(root, committed.diagnostics);
        return false;
    }
    std::cout << (root / "project.json").string()
              << ": written; backup=" << committed.backup.string() << '\n';
    return true;
}

bool MigrateProject(const std::filesystem::path& root) {
    auto context = CreateProjectContext(root);
    if (!context)
        return false;
    auto& workspace = *context->workspace;
    const auto opened = workspace.Open();
    if (!opened) {
        PrintDiagnostics(root, opened.diagnostics);
        return false;
    }
    const auto migration = workspace.Migrate();
    if (!migration) {
        PrintDiagnostics(root, migration.diagnostics);
        return false;
    }
    std::cout << root.string() << ": schema 1 requires no migration\n";
    return true;
}

bool DiagnoseProject(const std::filesystem::path& root) {
    auto context = CreateProjectContext(root);
    if (!context)
        return false;
    auto& workspace = *context->workspace;
    const auto opened = workspace.Open();
    if (!opened) {
        PrintDiagnostics(root, opened.diagnostics);
        return false;
    }
    const auto diagnosed = workspace.Diagnose(*opened.snapshot);
    if (!diagnosed) {
        PrintDiagnostics(root, diagnosed.diagnostics);
        return false;
    }
    std::cout << root.string() << ": diagnostic snapshot events=" << diagnosed.event_count
              << " interactions=" << diagnosed.interaction_count
              << " collision_boxes=" << diagnosed.collision_count
              << " navigation=" << diagnosed.navigation_width << "x" << diagnosed.navigation_height
              << " camera_regions=" << diagnosed.camera_region_count << '\n';
    return true;
}

bool EditData(const std::filesystem::path& root, const std::filesystem::path& relative,
              const std::filesystem::path& patch_path, bool write) {
    nlohmann::json patch;
    if (!LoadJsonDocument(patch_path, patch) || !patch.is_object() || patch.empty()) {
        std::cerr << "data patch must be a non-empty JSON object\n";
        return false;
    }
    auto assembled = jrpgmaker::project::AssembleProjectPluginRegistry(
        root, jrpgmaker::plugins::CompiledSamplePlugins());
    if (!assembled) {
        PrintDiagnostics(root, assembled.diagnostics);
        return false;
    }
    auto adapter =
        jrpgmaker::project::CreateTransientDataAdapter(root, relative, patch, assembled.data_roots);
    if (!adapter) {
        PrintDiagnostics(root, adapter.diagnostics);
        return false;
    }
    const auto type_id = adapter.adapter->type_id;
    auto adapters = jrpgmaker::project::CreateDefaultDocumentAdapters();
    const auto registered_adapter = adapters.Register(std::move(*adapter.adapter));
    if (!registered_adapter) {
        PrintDiagnostics(root, registered_adapter.diagnostics);
        return false;
    }
    auto context = CreateProjectContext(root, std::move(adapters), std::move(assembled.registry));
    if (!context)
        return false;
    auto& workspace = *context->workspace;
    const auto document_id = "transient:" + relative.generic_string();
    const auto registration =
        workspace.RegisterExternalDocuments({{document_id, relative, {}, true, type_id, {}}});
    if (!registration.empty()) {
        PrintDiagnostics(root, registration);
        return false;
    }
    const auto opened = workspace.Open();
    if (!opened) {
        PrintDiagnostics(root, opened.diagnostics);
        return false;
    }
    const auto selected = workspace.SelectDocument(document_id);
    if (!selected.empty()) {
        PrintDiagnostics(root, selected);
        return false;
    }
    const auto edit = workspace.ApplyObjectPatch(document_id, patch);
    if (!edit) {
        PrintDiagnostics(root, edit.diagnostics);
        return false;
    }
    if (edit.changes.empty()) {
        std::cout << (root / relative).string() << ": no changes\n";
        return true;
    }
    for (const auto& change : edit.changes)
        std::cout << change.field_path << ": " << change.before.dump() << " -> "
                  << change.after.dump() << '\n';
    if (!write)
        return true;
    const auto plan = workspace.PrepareSave(edit.revision);
    if (!plan) {
        PrintDiagnostics(root, plan.diagnostics);
        return false;
    }
    const auto committed = workspace.Commit(*plan.token);
    if (!committed) {
        PrintDiagnostics(root, committed.diagnostics);
        return false;
    }
    std::cout << (root / relative).string()
              << ": data written; backup=" << committed.backup.string() << '\n';
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3 || (std::string(argv[1]) == "create" && argc != 4) ||
        ((std::string(argv[1]) == "open" || std::string(argv[1]) == "validate" ||
          std::string(argv[1]) == "migrate" || std::string(argv[1]) == "diagnose" ||
          std::string(argv[1]) == "preview") &&
         argc != 3) ||
        ((std::string(argv[1]) == "diff" || std::string(argv[1]) == "write") && argc != 4) ||
        ((std::string(argv[1]) == "data-diff" || std::string(argv[1]) == "data-write") &&
         argc != 5)) {
        std::cerr << "usage: projecttool create <output-root> <template-root>\n"
                     "       projecttool open <project-root>\n"
                     "       projecttool validate <project-root>\n"
                     "       projecttool diff <project-root> <patch.json>\n"
                     "       projecttool write <project-root> <patch.json>\n"
                     "       projecttool migrate <project-root>\n"
                     "       projecttool diagnose <project-root>\n"
                     "       projecttool preview <project-root>\n"
                     "       projecttool data-diff <project-root> <path> <patch.json>\n"
                     "       projecttool data-write <project-root> <path> <patch.json>\n";
        return 2;
    }
    const std::string command = argv[1];
    if (command == "create")
        return CreateProject(argv[2], argv[3]) ? 0 : 1;
    if (command == "open")
        return OpenProject(argv[2], false) ? 0 : 1;
    if (command == "validate")
        return OpenProject(argv[2], true) ? 0 : 1;
    if (command == "diff")
        return EditProject(argv[2], argv[3], false) ? 0 : 1;
    if (command == "write")
        return EditProject(argv[2], argv[3], true) ? 0 : 1;
    if (command == "migrate")
        return MigrateProject(argv[2]) ? 0 : 1;
    if (command == "diagnose" || command == "preview")
        return DiagnoseProject(argv[2]) ? 0 : 1;
    if (command == "data-diff" || command == "data-write")
        return EditData(argv[2], argv[3], argv[4], command == "data-write") ? 0 : 1;
    std::cerr << "unknown projecttool command: " << command << '\n';
    return 2;
}
