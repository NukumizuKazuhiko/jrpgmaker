#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/plugin/plugin.hpp"

namespace jrpgmaker::project {

struct Diagnostic {
    std::string code;
    std::string path;
};

struct FieldDescriptor {
    std::string path;
    std::string value_type;
    std::string label_key;
    std::string recipe;
    bool required = false;
    bool read_only = false;
};

using DocumentValidator =
    std::function<std::vector<Diagnostic>(const nlohmann::json& document)>;

struct DocumentAdapter {
    std::string type_id;
    std::vector<FieldDescriptor> fields;
    DocumentValidator validate;
};

struct AdapterResult {
    std::vector<Diagnostic> diagnostics;
    explicit operator bool() const { return diagnostics.empty(); }
};

class DocumentAdapterRegistry final {
public:
    static constexpr std::size_t kMaxAdapters = 64;

    [[nodiscard]] AdapterResult Register(DocumentAdapter adapter);
    [[nodiscard]] AdapterResult Validate(const std::string& type_id,
                                         const nlohmann::json& document) const;
    [[nodiscard]] const DocumentAdapter* Find(const std::string& type_id) const;
    [[nodiscard]] std::size_t size() const { return adapters_.size(); }

private:
    std::vector<DocumentAdapter> adapters_;
};

[[nodiscard]] DocumentAdapterRegistry CreateDefaultDocumentAdapters();

struct ProjectSnapshot {
    std::filesystem::path root;
    plugin::ProjectManifest manifest;
    std::uint64_t revision = 0;
};

struct DocumentDescriptor {
    std::string id;
    std::filesystem::path path;
    std::string label_key;
    bool editable = false;
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

struct MigrationResult {
    std::uint32_t from_schema = 0;
    std::uint32_t to_schema = 0;
    bool changed = false;
    std::vector<Diagnostic> diagnostics;
    explicit operator bool() const { return diagnostics.empty(); }
};

class ProjectWorkspace final {
public:
    explicit ProjectWorkspace(std::filesystem::path root,
                              DocumentAdapterRegistry adapters = CreateDefaultDocumentAdapters());

    [[nodiscard]] WorkspaceResult Open();
    [[nodiscard]] std::vector<Diagnostic> SelectDocument(std::string_view document_id);
    [[nodiscard]] std::string_view CurrentDocumentId() const { return current_document_id_; }
    [[nodiscard]] std::vector<DocumentDescriptor>
    DescribeDocuments(const ProjectSnapshot& snapshot) const;
    [[nodiscard]] DiagnosticSet Diagnose(const ProjectSnapshot& snapshot) const;
    [[nodiscard]] EditResult Apply(const EditCommand& command);
    [[nodiscard]] const nlohmann::json& CurrentDocument() const { return working_document_; }
    [[nodiscard]] const std::vector<Change>& PendingChanges() const { return pending_changes_; }
    [[nodiscard]] SavePlan PrepareSave(std::uint64_t expected_revision) const;
    [[nodiscard]] CommitResult Commit(const SaveToken& token);
    [[nodiscard]] MigrationResult Migrate(std::uint32_t target_schema = 1) const;

private:
    std::filesystem::path root_;
    nlohmann::json working_document_;
    std::string current_document_id_ = "project.manifest";
    std::optional<ProjectSnapshot> snapshot_;
    std::vector<Change> pending_changes_;
    std::uint64_t revision_ = 0;
    DocumentAdapterRegistry adapters_;
};

} // namespace jrpgmaker::project
