#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/project/workspace.hpp"

namespace jrpgmaker::editor {

struct FormFieldProjection {
    std::string path;
    std::string label_key;
    std::string recipe;
    std::string value_type;
    nlohmann::json value;
    bool required = false;
    bool read_only = false;
};

struct FormProjection {
    std::string document_id;
    std::vector<FormFieldProjection> fields;
};

struct DocumentTabProjection {
    std::string document_id;
    std::string path;
    std::string label_key;
    bool active = false;
    bool dirty = false;
    bool editable = false;
    std::size_t diagnostic_count = 0;
};

struct DocumentTabsProjection {
    std::vector<DocumentTabProjection> tabs;
};

struct DiagnosticProjection {
    std::string document_id;
    project::Diagnostic diagnostic;
};

struct DiagnosticPanelProjection {
    std::string filter;
    std::vector<DiagnosticProjection> items;
};

struct DiffProjection {
    std::vector<project::Change> changes;
};

struct PreviewMetricProjection {
    std::string label_key;
    std::string value_type;
    nlohmann::json value;
};

struct PreviewProjection {
    bool valid = false;
    bool process_running = false;
    int process_exit_code = 0;
    std::string process_error;
    std::vector<project::Diagnostic> diagnostics;
    std::vector<PreviewMetricProjection> metrics;
};

[[nodiscard]] FormProjection BuildFormProjection(const project::DocumentAdapter& adapter,
                                                  const nlohmann::json& document);

[[nodiscard]] DocumentTabsProjection BuildDocumentTabsProjection(
    const std::vector<project::DocumentDescriptor>& documents, std::string_view active_document_id,
    bool dirty, const std::vector<project::Diagnostic>& diagnostics);

[[nodiscard]] DiagnosticPanelProjection BuildDiagnosticPanelProjection(
    const std::vector<project::DocumentDescriptor>& documents,
    const std::vector<project::Diagnostic>& diagnostics, std::string_view filter = {});

[[nodiscard]] DiffProjection BuildDiffProjection(const std::vector<project::Change>& changes);

[[nodiscard]] PreviewProjection BuildWorkspacePreview(const project::DiagnosticSet& diagnosis);

} // namespace jrpgmaker::editor
