#include "jrpgmaker/editor/editor_session.hpp"

#include <utility>

namespace jrpgmaker::editor {

EditorSession::EditorSession(std::filesystem::path root) : workspace_(std::move(root), adapters_) {}

void EditorSession::SetDiagnostics(std::vector<project::Diagnostic> diagnostics) {
    state_.diagnostics = std::move(diagnostics);
}

void EditorSession::RebuildProjection() {
    const auto* adapter = adapters_.Find("project.manifest");
    if (adapter == nullptr || !snapshot_)
        return;
    state_.form = BuildFormProjection(*adapter, workspace_.CurrentDocument());
    state_.preview = BuildWorkspacePreview(workspace_.Diagnose(*snapshot_));
    state_.revision = snapshot_->revision;
}

bool EditorSession::Open() {
    const auto result = workspace_.Open();
    if (!result) {
        state_.open = false;
        SetDiagnostics(result.diagnostics);
        return false;
    }
    snapshot_ = result.snapshot;
    state_ = {};
    state_.open = true;
    RebuildProjection();
    focus_context_.ClearFocusables();
    for (std::size_t index = 0; index < state_.form.fields.size(); ++index)
        (void) focus_context_.RegisterFocusable(index + 1, index);
    SetDiagnostics(state_.preview.diagnostics);
    return state_.preview.valid;
}

bool EditorSession::Refresh() {
    if (!state_.open || !snapshot_)
        return false;
    const auto diagnosis = workspace_.Diagnose(*snapshot_);
    state_.preview = BuildWorkspacePreview(diagnosis);
    SetDiagnostics(diagnosis.diagnostics);
    return state_.preview.valid;
}

bool EditorSession::SelectNext() {
    if (!state_.open || state_.form.fields.empty())
        return false;
    if (!focus_context_.MoveFocus(1))
        return false;
    state_.selected_field = static_cast<std::size_t>(focus_context_.focused_widget() - 1);
    return state_.selected_field < state_.form.fields.size();
}

bool EditorSession::SelectPrevious() {
    if (!state_.open || state_.form.fields.empty())
        return false;
    if (!focus_context_.MoveFocus(-1))
        return false;
    state_.selected_field = static_cast<std::size_t>(focus_context_.focused_widget() - 1);
    return state_.selected_field < state_.form.fields.size();
}

bool EditorSession::ApplySelected(nlohmann::json value) {
    if (!state_.open || state_.form.fields.empty() ||
        state_.selected_field >= state_.form.fields.size())
        return false;
    const auto& field = state_.form.fields[state_.selected_field];
    if (field.read_only)
        return false;
    const auto result = workspace_.Apply({state_.form.document_id, field.path, std::move(value)});
    if (!result) {
        SetDiagnostics(result.diagnostics);
        return false;
    }
    state_.dirty = !result.changes.empty() || state_.dirty;
    state_.revision = result.revision;
    snapshot_->revision = result.revision;
    RebuildProjection();
    SetDiagnostics({});
    return true;
}

bool EditorSession::ApplySelectedText(std::string_view value) {
    if (!state_.open || state_.form.fields.empty() || state_.selected_field >= state_.form.fields.size())
        return false;
    const auto& field = state_.form.fields[state_.selected_field];
    if (field.read_only || field.value_type != "string")
        return false;
    return ApplySelected(std::string(value));
}

bool EditorSession::Save() {
    if (!state_.open || !snapshot_)
        return false;
    const auto plan = workspace_.PrepareSave(state_.revision);
    if (!plan) {
        SetDiagnostics(plan.diagnostics);
        return false;
    }
    if (!plan.token || plan.changes.empty()) {
        state_.dirty = false;
        SetDiagnostics({});
        return true;
    }
    const auto result = workspace_.Commit(*plan.token);
    if (!result) {
        SetDiagnostics(result.diagnostics);
        return false;
    }
    state_.dirty = false;
    SetDiagnostics({});
    return true;
}

} // namespace jrpgmaker::editor
