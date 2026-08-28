#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "jrpgmaker/plugin/plugin.hpp"

namespace jrpgmaker::project {

struct Diagnostic {
    std::string code;
    std::string path;
};

struct ProjectSnapshot {
    std::filesystem::path root;
    plugin::ProjectManifest manifest;
};

struct WorkspaceResult {
    std::optional<ProjectSnapshot> snapshot;
    std::vector<Diagnostic> diagnostics;
    explicit operator bool() const { return snapshot.has_value() && diagnostics.empty(); }
};

struct DiagnosticSet {
    std::vector<Diagnostic> diagnostics;
    std::size_t event_count = 0;
    std::size_t interaction_count = 0;
    std::size_t collision_count = 0;
    std::size_t navigation_width = 0;
    std::size_t navigation_height = 0;
    std::size_t camera_region_count = 0;
    explicit operator bool() const { return diagnostics.empty(); }
};

class ProjectWorkspace final {
public:
    explicit ProjectWorkspace(std::filesystem::path root);

    [[nodiscard]] WorkspaceResult Open() const;
    [[nodiscard]] DiagnosticSet Diagnose(const ProjectSnapshot& snapshot) const;

private:
    std::filesystem::path root_;
};

} // namespace jrpgmaker::project
