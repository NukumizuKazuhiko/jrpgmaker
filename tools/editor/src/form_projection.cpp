#include "jrpgmaker/editor/form_projection.hpp"

#include <utility>

#include <string_view>

#include <algorithm>

namespace jrpgmaker::editor {

FormProjection BuildFormProjection(const project::DocumentAdapter& adapter,
                                    const nlohmann::json& document) {
    FormProjection projection{.document_id = adapter.type_id, .fields = {}};
    projection.fields.reserve(adapter.fields.size());
    for (const auto& descriptor : adapter.fields) {
        FormFieldProjection field{.path = descriptor.path,
                                  .label_key = descriptor.label_key,
                                  .recipe = descriptor.recipe,
                                  .value_type = descriptor.value_type,
                                  .value = nullptr,
                                  .required = descriptor.required,
                                  .read_only = descriptor.read_only};
        const auto pointer = nlohmann::json::json_pointer(descriptor.path);
        if (document.contains(pointer))
            field.value = document.at(pointer);
        else
            field.value = nullptr;
        projection.fields.push_back(std::move(field));
    }
    return projection;
}

DocumentTabsProjection BuildDocumentTabsProjection(
    const std::vector<project::DocumentDescriptor>& documents, std::string_view active_document_id,
    bool dirty, const std::vector<project::Diagnostic>& diagnostics) {
    DocumentTabsProjection projection;
    projection.tabs.reserve(documents.size());
    for (const auto& document : documents) {
        std::size_t diagnostic_count = 0;
        const auto relative_path = document.path.generic_string();
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.path == relative_path ||
                (document.id == "project.manifest" && diagnostic.path == "project.json"))
                ++diagnostic_count;
        }
        projection.tabs.push_back(DocumentTabProjection{
            .document_id = document.id,
            .path = relative_path,
            .label_key = document.label_key,
            .active = document.id == active_document_id,
            .dirty = dirty && document.id == active_document_id,
            .editable = document.editable,
            .diagnostic_count = diagnostic_count});
    }
    return projection;
}

DiagnosticPanelProjection BuildDiagnosticPanelProjection(
    const std::vector<project::DocumentDescriptor>& documents,
    const std::vector<project::Diagnostic>& diagnostics, std::string_view filter) {
    DiagnosticPanelProjection projection{.filter = std::string(filter), .items = {}};
    for (const auto& diagnostic : diagnostics) {
        if (!filter.empty() && diagnostic.code.find(filter) == std::string::npos &&
            diagnostic.path.find(filter) == std::string::npos)
            continue;
        std::string document_id;
        for (const auto& document : documents) {
            if (diagnostic.path == document.path.generic_string() ||
                (document.id == "project.manifest" && diagnostic.path == "project.json")) {
                document_id = document.id;
                break;
            }
        }
        projection.items.push_back(DiagnosticProjection{std::move(document_id), diagnostic});
    }
    return projection;
}

DiffProjection BuildDiffProjection(const std::vector<project::Change>& changes) {
    DiffProjection projection{.changes = changes};
    std::stable_sort(projection.changes.begin(), projection.changes.end(),
                     [](const auto& left, const auto& right) {
                         return left.sequence < right.sequence;
                     });
    return projection;
}

PreviewProjection BuildWorkspacePreview(const project::DiagnosticSet& diagnosis) {
    PreviewProjection projection{.valid = diagnosis.diagnostics.empty(),
                                 .process_running = false,
                                 .process_exit_code = 0,
                                 .process_error = {},
                                 .standard_output = {},
                                 .standard_error = {},
                                 .diagnostics = diagnosis.diagnostics,
                                 .metrics = {}};
    if (!projection.valid)
        return projection;
    projection.metrics = {
        {"editor.preview.event_count", "integer", diagnosis.event_count},
        {"editor.preview.interaction_count", "integer", diagnosis.interaction_count},
        {"editor.preview.collision_count", "integer", diagnosis.collision_count},
        {"editor.preview.navigation_width", "integer", diagnosis.navigation_width},
        {"editor.preview.navigation_height", "integer", diagnosis.navigation_height},
        {"editor.preview.camera_region_count", "integer", diagnosis.camera_region_count},
    };
    return projection;
}

} // namespace jrpgmaker::editor
