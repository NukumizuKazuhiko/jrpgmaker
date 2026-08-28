#include "jrpgmaker/editor/form_projection.hpp"

#include <utility>

namespace jrpgmaker::editor {

FormProjection BuildFormProjection(const project::DocumentAdapter& adapter,
                                    const nlohmann::json& document) {
    FormProjection projection{.document_id = adapter.type_id};
    projection.fields.reserve(adapter.fields.size());
    for (const auto& descriptor : adapter.fields) {
        FormFieldProjection field{.path = descriptor.path,
                                  .label_key = descriptor.label_key,
                                  .recipe = descriptor.recipe,
                                  .value_type = descriptor.value_type,
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

PreviewProjection BuildWorkspacePreview(const project::DiagnosticSet& diagnosis) {
    PreviewProjection projection{.valid = diagnosis.diagnostics.empty(),
                                 .diagnostics = diagnosis.diagnostics};
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
