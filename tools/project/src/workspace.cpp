#include "jrpgmaker/project/workspace.hpp"

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

} // namespace

ProjectWorkspace::ProjectWorkspace(std::filesystem::path root) : root_(std::move(root)) {}

WorkspaceResult ProjectWorkspace::Open() const {
    WorkspaceResult result;
    nlohmann::json document;
    std::vector<Diagnostic> diagnostics;
    if (!Read(root_ / "project.json", document, diagnostics)) {
        result.diagnostics = std::move(diagnostics);
        return result;
    }
    const auto parsed = plugin::ParseProjectManifest(document);
    if (!parsed) {
        Add(diagnostics, parsed.error->code, parsed.error->path);
        result.diagnostics = std::move(diagnostics);
        return result;
    }
    const auto& manifest = *parsed.manifest;
    for (const auto& path : manifest.data_roots)
        AddPath(root_, path, diagnostics);
    for (const auto& path : {manifest.material_document, manifest.input_actions,
                             manifest.event_script, manifest.localization,
                             manifest.resource_manifest})
        AddPath(root_, path, diagnostics);
    if (!diagnostics.empty()) {
        result.diagnostics = std::move(diagnostics);
        return result;
    }
    result.snapshot = ProjectSnapshot{root_, manifest};
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
    const auto data_directory = event_path.parent_path();
    if (!Read(event_path, events_document, result.diagnostics) ||
        !Read(data_directory / "navigation_demo.json", navigation_document, result.diagnostics) ||
        !Read(data_directory / "collision_demo.json", collision_document, result.diagnostics) ||
        !Read(data_directory / "camera_demo.json", camera_document, result.diagnostics) ||
        !Read(data_directory / "interaction_demo.json", interaction_document, result.diagnostics))
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
        Add(result.diagnostics, "project.diagnose.invalid_data", snapshot.manifest.event_script);
    }
    return result;
}

} // namespace jrpgmaker::project
