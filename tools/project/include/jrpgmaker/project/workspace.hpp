#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/plugin/plugin.hpp"

namespace jrpgmaker::project {

struct Diagnostic {
    std::string code;
    std::string path;
};

struct ProjectSnapshot {
    std::filesystem::path root;
    plugin::ProjectManifest manifest;
    std::uint64_t revision = 0;
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

struct EditCommand {
    std::string document_id;
    std::string field_path;
    nlohmann::json value;
};

struct Change {
    std::string document_id;
    std::string field_path;
    nlohmann::json before;
    nlohmann::json after;
    std::size_t sequence = 0;
};

struct EditResult {
    std::uint64_t revision = 0;
    std::vector<Change> changes;
    std::vector<Diagnostic> diagnostics;
    explicit operator bool() const { return diagnostics.empty(); }
};

struct SaveToken {
    std::uint64_t revision = 0;
};

struct SavePlan {
    std::optional<SaveToken> token;
    std::vector<Change> changes;
    std::vector<Diagnostic> diagnostics;
    explicit operator bool() const { return token.has_value() && diagnostics.empty(); }
};

struct CommitResult {
    std::uint64_t revision = 0;
    std::filesystem::path backup;
    std::vector<Diagnostic> diagnostics;
    explicit operator bool() const { return diagnostics.empty(); }
};

class ProjectWorkspace final {
public:
    explicit ProjectWorkspace(std::filesystem::path root);

    [[nodiscard]] WorkspaceResult Open();
    [[nodiscard]] DiagnosticSet Diagnose(const ProjectSnapshot& snapshot) const;
    [[nodiscard]] EditResult Apply(const EditCommand& command);
    [[nodiscard]] SavePlan PrepareSave(std::uint64_t expected_revision) const;
    [[nodiscard]] CommitResult Commit(const SaveToken& token);

private:
    std::filesystem::path root_;
    nlohmann::json working_document_;
    std::optional<ProjectSnapshot> snapshot_;
    std::vector<Change> pending_changes_;
    std::uint64_t revision_ = 0;
};

} // namespace jrpgmaker::project
