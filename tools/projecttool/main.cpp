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

#include "jrpgmaker/core/calendar.hpp"
#include "jrpgmaker/core/input_actions.hpp"
#include "jrpgmaker/core/map_data.hpp"
#include "jrpgmaker/domain/event_script.hpp"
#include "jrpgmaker/domain/interaction.hpp"
#include "jrpgmaker/domain/localization.hpp"
#include "jrpgmaker/domain/schedule.hpp"
#include "jrpgmaker/domain/vertical_slice.hpp"
#include "jrpgmaker/plugin/plugin.hpp"
#include "jrpgmaker/project/workspace.hpp"

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

bool LoadJsonDocument(const std::filesystem::path& path, nlohmann::json& document);

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
    nlohmann::json material;
    const auto material_path = snapshot.root / manifest.material_document;
    if (ok && (!LoadJsonDocument(material_path, material) || !material.is_object() ||
               material.value("style_plugin_id", std::string{}) != manifest.render_style)) {
        std::cerr << material_path.string() << ": style_plugin_id must match render_style '"
                  << manifest.render_style << "'\n";
        ok = false;
    }
    if (ok) {
        std::cout << snapshot.root.string() << ": project manifest clean (id=" << manifest.id
                  << ", plugins=" << manifest.plugins.size() << ")\n";
    }
    return ok;
}

bool OpenProject(const std::filesystem::path& root, bool validate) {
    jrpgmaker::project::ProjectWorkspace workspace(root);
    const auto opened = workspace.Open();
    if (!opened) {
        for (const auto& diagnostic : opened.diagnostics)
            std::cerr << root / diagnostic.path << ": " << diagnostic.code << '\n';
        return false;
    }
    const auto& snapshot = *opened.snapshot;
    std::cout << root.string() << ": opened project id=" << snapshot.manifest.id
              << " render_style=" << snapshot.manifest.render_style << '\n';
    if (!validate)
        return true;
    const auto diagnosed = workspace.Diagnose(snapshot);
    if (!diagnosed) {
        for (const auto& diagnostic : diagnosed.diagnostics)
            std::cerr << root / diagnostic.path << ": " << diagnostic.code << '\n';
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
    jrpgmaker::project::ProjectWorkspace workspace(root);
    const auto opened = workspace.Open();
    if (!opened) {
        for (const auto& diagnostic : opened.diagnostics)
            std::cerr << root / diagnostic.path << ": " << diagnostic.code << '\n';
        return false;
    }
    std::vector<jrpgmaker::project::Change> changes;
    std::uint64_t current_revision = opened.snapshot->revision;
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        const auto edit = workspace.Apply({"project.manifest", "/" + it.key(), it.value()});
        if (!edit) {
            for (const auto& diagnostic : edit.diagnostics)
                std::cerr << root / diagnostic.path << ": " << diagnostic.code << '\n';
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
        for (const auto& diagnostic : plan.diagnostics)
            std::cerr << root / diagnostic.path << ": " << diagnostic.code << '\n';
        return false;
    }
    const auto committed = workspace.Commit(*plan.token);
    if (!committed) {
        for (const auto& diagnostic : committed.diagnostics)
            std::cerr << root / diagnostic.path << ": " << diagnostic.code << '\n';
        return false;
    }
    std::cout << (root / "project.json").string() << ": written; backup="
              << committed.backup.string() << '\n';
    return true;
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

bool DiagnoseProject(const std::filesystem::path& root) {
    jrpgmaker::project::ProjectWorkspace workspace(root);
    const auto opened = workspace.Open();
    if (!opened) {
        for (const auto& diagnostic : opened.diagnostics)
            std::cerr << root / diagnostic.path << ": " << diagnostic.code << '\n';
        return false;
    }
    const auto diagnosed = workspace.Diagnose(*opened.snapshot);
    if (!diagnosed) {
        for (const auto& diagnostic : diagnosed.diagnostics)
            std::cerr << root / diagnostic.path << ": " << diagnostic.code << '\n';
        return false;
    }
    std::cout << root.string() << ": diagnostic snapshot events=" << diagnosed.event_count
              << " interactions=" << diagnosed.interaction_count
              << " collision_boxes=" << diagnosed.collision_count << " navigation="
              << diagnosed.navigation_width << "x" << diagnosed.navigation_height
              << " camera_regions=" << diagnosed.camera_region_count << '\n';
    return true;
}

bool ValidateDataDocument(const std::filesystem::path& root, const std::filesystem::path& relative,
                          const nlohmann::json& document) {
    const std::string name = relative.filename().string();
    try {
        if (name == "events_demo.json") {
            (void) jrpgmaker::domain::ParseEventScript(document);
        } else if (name == "navigation_demo.json") {
            (void) jrpgmaker::core::ParseNavigationGrid(document);
        } else if (name == "collision_demo.json") {
            (void) jrpgmaker::core::ParseCollisionAabbs(document);
        } else if (name == "camera_demo.json") {
            (void) jrpgmaker::core::ParseCameraRigData(document);
        } else if (name == "interaction_demo.json") {
            (void) jrpgmaker::domain::ParseInteractionPoints(document);
        } else if (name == "input_actions.json") {
            (void) jrpgmaker::core::ParseInputActionMap(document);
        } else if (name == "calendar_demo.json") {
            const auto result = jrpgmaker::core::ParseCalendarDefinition(document);
            if (!result.ok)
                throw std::invalid_argument(result.error);
        } else if (name == "localization_en.json") {
            const auto result = jrpgmaker::domain::ParseLocalizationTable(document);
            if (!result)
                throw std::invalid_argument(result.error);
        } else if (name == "material_demo.json" || name == "material_accent.json") {
            if (!document.is_object() || document.value("schema", 0) != 1 ||
                !document.contains("parameters"))
                throw std::invalid_argument("material requires schema 1 and parameters");
        } else if (name == "schedule_demo.json") {
            nlohmann::json calendar_document;
            if (!LoadJsonDocument(root / "assets/data/calendar_demo.json", calendar_document))
                return false;
            const auto calendar_result =
                jrpgmaker::core::ParseCalendarDefinition(calendar_document);
            if (!calendar_result.ok)
                throw std::invalid_argument(calendar_result.error);
            (void) jrpgmaker::domain::ParseScheduleTable(document, calendar_result.calendar);
        } else if (name == "vertical_slice_demo.json") {
            (void) jrpgmaker::domain::ParseVerticalSliceDefinition(document);
        } else {
            throw std::invalid_argument("unsupported schema-aware data file");
        }
    } catch (const std::exception& error) {
        std::cerr << (root / relative).string() << ": data validation error: " << error.what()
                  << '\n';
        return false;
    }
    return true;
}

bool EditData(const std::filesystem::path& root, const std::filesystem::path& relative,
              const std::filesystem::path& patch_path, bool write) {
    if (relative.empty() || relative.is_absolute() ||
        relative.string().find("..") != std::string::npos) {
        std::cerr << "data path must be safe and relative\n";
        return false;
    }
    nlohmann::json original;
    nlohmann::json patch;
    if (!LoadJsonDocument(root / relative, original) || !LoadJsonDocument(patch_path, patch) ||
        !patch.is_object() || patch.empty()) {
        std::cerr << "data patch must be a non-empty JSON object\n";
        return false;
    }
    nlohmann::json edited = original;
    for (auto it = patch.begin(); it != patch.end(); ++it)
        edited[it.key()] = it.value();
    if (!ValidateDataDocument(root, relative, edited))
        return false;
    bool changed = false;
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (!original.contains(it.key()) || original[it.key()] != it.value()) {
            std::cout << "/" << it.key() << ": "
                      << (original.contains(it.key()) ? original[it.key()].dump() : "null")
                      << " -> " << it.value().dump() << '\n';
            changed = true;
        }
    }
    if (!changed)
        std::cout << (root / relative).string() << ": no changes\n";
    if (!write)
        return true;
    const auto temporary = root / (relative.string() + ".tmp");
    std::error_code error;
    if (std::filesystem::exists(temporary, error)) {
        std::cerr << "refusing to overwrite existing data temporary file\n";
        return false;
    }
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
        return false;
    output << edited.dump(2) << '\n';
    output.close();
    if (!output)
        return false;
    const auto backup = root / (relative.string() + ".bak");
    if (std::filesystem::exists(backup, error)) {
        std::cerr << "refusing to overwrite existing data backup\n";
        std::filesystem::remove(temporary, error);
        return false;
    }
    std::filesystem::rename(root / relative, backup, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    std::filesystem::rename(temporary, root / relative, error);
    if (error) {
        std::filesystem::rename(backup, root / relative, error);
        return false;
    }
    std::cout << (root / relative).string() << ": data written; backup=" << backup.string() << '\n';
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
