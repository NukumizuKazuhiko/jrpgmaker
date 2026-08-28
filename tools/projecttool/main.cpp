// projecttool CLI: creates, opens and validates a project workspace.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/plugin/plugin.hpp"

namespace {

constexpr std::size_t kMaxFiles = 4096;
constexpr std::uintmax_t kMaxBytes = 64u * 1024u * 1024u;

struct ProjectSnapshot {
    std::filesystem::path root;
    jrpgmaker::plugin::ProjectManifest manifest;
};

bool IsSafeRelativePath(const std::string& path) {
    return !path.empty() && path.find("..") == std::string::npos && path.front() != '/' &&
           path.front() != '\\';
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

bool LoadSnapshot(const std::filesystem::path& root, ProjectSnapshot& snapshot) {
    const auto manifest_path = root / "project.json";
    std::ifstream file(manifest_path);
    if (!file.is_open()) {
        std::cerr << manifest_path.string() << ": cannot open project manifest\n";
        return false;
    }
    try {
        nlohmann::json document;
        file >> document;
        const auto result = jrpgmaker::plugin::ParseProjectManifest(document);
        if (!result) {
            std::cerr << manifest_path.string() << ": " << result.error->code << ": "
                      << result.error->message << " (" << result.error->path << ")\n";
            return false;
        }
        snapshot = {.root = root, .manifest = *result.manifest};
        return true;
    } catch (const std::exception& error) {
        std::cerr << manifest_path.string() << ": parse error: " << error.what() << '\n';
        return false;
    }
}

bool ValidateSnapshot(const ProjectSnapshot& snapshot) {
    const auto& manifest = snapshot.manifest;
    std::vector<std::string> paths = manifest.data_roots;
    paths.push_back(manifest.material_document);
    paths.push_back(manifest.input_actions);
    paths.push_back(manifest.event_script);
    paths.push_back(manifest.localization);
    paths.push_back(manifest.resource_manifest);
    bool ok = true;
    for (const auto& relative : paths) {
        if (!IsSafeRelativePath(relative) || !std::filesystem::exists(snapshot.root / relative)) {
            std::cerr << "project.json: missing or unsafe reference: " << relative << '\n';
            ok = false;
        }
    }
    if (ok) {
        std::cout << snapshot.root.string() << ": project manifest clean (id=" << manifest.id
                  << ", plugins=" << manifest.plugins.size() << ")\n";
    }
    return ok;
}

bool OpenProject(const std::filesystem::path& root, bool validate) {
    ProjectSnapshot snapshot;
    if (!LoadSnapshot(root, snapshot))
        return false;
    std::cout << root.string() << ": opened project id=" << snapshot.manifest.id
              << " render_style=" << snapshot.manifest.render_style << '\n';
    return !validate || ValidateSnapshot(snapshot);
}

const std::vector<std::string>& EditableFields() {
    static const std::vector<std::string> fields = {
        "id",           "render_style",      "battle_plugin", "plugins",
        "data_roots",   "material_document", "input_actions", "event_script",
        "localization", "resource_manifest"};
    return fields;
}

bool IsEditableField(const std::string& field) {
    const auto& fields = EditableFields();
    return std::find(fields.begin(), fields.end(), field) != fields.end();
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

bool ApplyManifestPatch(const nlohmann::json& patch, nlohmann::json& document) {
    if (!patch.is_object() || patch.empty()) {
        std::cerr << "manifest patch must be a non-empty object\n";
        return false;
    }
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (!IsEditableField(it.key())) {
            std::cerr << "manifest patch contains non-editable field: " << it.key() << '\n';
            return false;
        }
        document[it.key()] = it.value();
    }
    return true;
}

bool BuildEditedDocument(const std::filesystem::path& root, const std::filesystem::path& patch_path,
                         nlohmann::json& original, nlohmann::json& edited,
                         ProjectSnapshot& snapshot) {
    if (!LoadJsonDocument(root / "project.json", original))
        return false;
    edited = original;
    nlohmann::json patch;
    if (!LoadJsonDocument(patch_path, patch) || !ApplyManifestPatch(patch, edited))
        return false;
    const auto result = jrpgmaker::plugin::ParseProjectManifest(edited);
    if (!result) {
        std::cerr << patch_path.string() << ": " << result.error->code << ": "
                  << result.error->message << " (" << result.error->path << ")\n";
        return false;
    }
    snapshot = {.root = root, .manifest = *result.manifest};
    return ValidateSnapshot(snapshot);
}

void PrintManifestDiff(const nlohmann::json& original, const nlohmann::json& edited) {
    bool changed = false;
    for (const auto& field : EditableFields()) {
        const nlohmann::json before = original.contains(field) ? original[field] : nlohmann::json();
        const nlohmann::json after = edited.contains(field) ? edited[field] : nlohmann::json();
        if (before == after)
            continue;
        changed = true;
        std::cout << "/" << field << ": " << before.dump() << " -> " << after.dump() << '\n';
    }
    if (!changed)
        std::cout << "project.json: no changes\n";
}

bool WriteEditedManifest(const std::filesystem::path& root, const nlohmann::json& document) {
    const auto manifest = root / "project.json";
    const auto temporary = root / ".project.json.tmp";
    std::error_code error;
    if (std::filesystem::exists(temporary, error)) {
        std::cerr << "refusing to overwrite an existing temporary manifest\n";
        return false;
    }
    std::filesystem::path backup;
    for (std::size_t index = 0; index <= 8; ++index) {
        const auto candidate = index == 0 ? root / "project.json.bak"
                                          : root / ("project.json.bak." + std::to_string(index));
        if (!std::filesystem::exists(candidate, error)) {
            backup = candidate;
            break;
        }
    }
    if (backup.empty()) {
        std::cerr << "refusing to write: backup retention limit reached\n";
        return false;
    }
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            std::cerr << temporary.string() << ": cannot create temporary manifest\n";
            return false;
        }
        output << document.dump(2) << '\n';
        if (!output) {
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::rename(manifest, backup, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        std::filesystem::remove(backup, error);
        return false;
    }
    std::filesystem::rename(temporary, manifest, error);
    if (error) {
        std::filesystem::rename(backup, manifest, error);
        return false;
    }
    std::cout << manifest.string() << ": written; backup=" << backup.string() << '\n';
    return true;
}

bool EditProject(const std::filesystem::path& root, const std::filesystem::path& patch_path,
                 bool write) {
    nlohmann::json original;
    nlohmann::json edited;
    ProjectSnapshot snapshot;
    if (!BuildEditedDocument(root, patch_path, original, edited, snapshot))
        return false;
    PrintManifestDiff(original, edited);
    return !write || WriteEditedManifest(root, edited);
}

bool MigrateProject(const std::filesystem::path& root) {
    ProjectSnapshot snapshot;
    if (!LoadSnapshot(root, snapshot))
        return false;
    if (snapshot.manifest.schema != 1) {
        std::cerr << "unsupported project schema for migration\n";
        return false;
    }
    std::cout << root.string() << ": schema 1 requires no migration\n";
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3 || (std::string(argv[1]) == "create" && argc != 4) ||
        ((std::string(argv[1]) == "open" || std::string(argv[1]) == "validate" ||
          std::string(argv[1]) == "migrate") &&
         argc != 3) ||
        ((std::string(argv[1]) == "diff" || std::string(argv[1]) == "write") && argc != 4)) {
        std::cerr << "usage: projecttool create <output-root> <template-root>\n"
                     "       projecttool open <project-root>\n"
                     "       projecttool validate <project-root>\n"
                     "       projecttool diff <project-root> <patch.json>\n"
                     "       projecttool write <project-root> <patch.json>\n"
                     "       projecttool migrate <project-root>\n";
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
    std::cerr << "unknown projecttool command: " << command << '\n';
    return 2;
}
