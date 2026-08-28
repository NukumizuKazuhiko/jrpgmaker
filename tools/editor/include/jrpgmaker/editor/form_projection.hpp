#pragma once

#include <string>
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

struct PreviewMetricProjection {
    std::string label_key;
    std::string value_type;
    nlohmann::json value;
};

struct PreviewProjection {
    bool valid = false;
    std::vector<project::Diagnostic> diagnostics;
    std::vector<PreviewMetricProjection> metrics;
};

[[nodiscard]] FormProjection BuildFormProjection(const project::DocumentAdapter& adapter,
                                                  const nlohmann::json& document);

[[nodiscard]] PreviewProjection BuildWorkspacePreview(const project::DiagnosticSet& diagnosis);

} // namespace jrpgmaker::editor
