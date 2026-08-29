#include "jrpgmaker/editor/editor_session.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <limits>
#include <utility>

namespace jrpgmaker::editor {

EditorSession::EditorSession(std::filesystem::path root)
    : root_(root), workspace_(std::move(root), adapters_) {}

EditorSession::EditorSession(std::filesystem::path root, project::DocumentAdapterRegistry adapters)
    : adapters_(std::move(adapters)), root_(root), workspace_(std::move(root), adapters_) {}

bool EditorSession::Open(std::filesystem::path root) {
    if (root.empty())
        return false;
    preview_process_.Stop();
    root_ = std::move(root);
    workspace_ = project::ProjectWorkspace(root_, adapters_);
    snapshot_.reset();
    state_ = {};
    focus_context_.ClearFocusables();
    return Open();
}

void EditorSession::SetDiagnostics(std::vector<project::Diagnostic> diagnostics) {
    state_.diagnostics = std::move(diagnostics);
    if (snapshot_)
        state_.tabs = BuildDocumentTabsProjection(workspace_.DescribeDocuments(*snapshot_),
                                                  state_.form.document_id, workspace_.PendingChanges(),
                                                  state_.diagnostics);
    if (snapshot_)
        state_.diagnostic_panel = BuildDiagnosticPanelProjection(
            workspace_.DescribeDocuments(*snapshot_), state_.diagnostics,
            state_.diagnostic_panel.filter);
}

void EditorSession::RebuildProjection() {
    const auto* adapter = adapters_.Find(std::string(workspace_.CurrentDocumentId()));
    if (adapter == nullptr || !snapshot_)
        return;
    state_.form = BuildFormProjection(*adapter, workspace_.CurrentDocument());
    state_.preview = BuildWorkspacePreview(workspace_.Diagnose(*snapshot_));
    state_.tabs = BuildDocumentTabsProjection(workspace_.DescribeDocuments(*snapshot_),
                                              state_.form.document_id, workspace_.PendingChanges(),
                                              state_.preview.diagnostics);
    state_.diff = BuildDiffProjection(workspace_.PendingChanges());
    state_.revision = snapshot_->revision;
}

void EditorSession::SyncTextField(bool select_all) {
    state_.text_selection_start = 0;
    state_.text_selection_end = 0;
    state_.text_caret = 0;
    state_.text_composing = false;
    if (state_.form.fields.empty() || state_.selected_field >= state_.form.fields.size())
        return;
    const auto& value = state_.form.fields[state_.selected_field].value;
    if (!value.is_string() && !value.is_number_integer())
        return;
    text_field_.SetText(value.is_string() ? value.get<std::string>() : value.dump());
    if (select_all)
        text_field_.SelectAll();
    PublishTextFieldState();
}

void EditorSession::PublishTextFieldState() {
    state_.text_selection_start = text_field_.selection_start();
    state_.text_selection_end = text_field_.selection_end();
    state_.text_caret = text_field_.caret();
    state_.text_composing = text_field_.composing();
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
    SyncTextField(true);
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

bool EditorSession::SelectDocument(std::string_view document_id) {
    if (!state_.open || !snapshot_)
        return false;
    const auto diagnostics = workspace_.SelectDocument(document_id);
    if (!diagnostics.empty()) {
        SetDiagnostics(diagnostics);
        return false;
    }
    state_.selected_field = 0;
    RebuildProjection();
    focus_context_.ClearFocusables();
    for (std::size_t index = 0; index < state_.form.fields.size(); ++index)
        (void) focus_context_.RegisterFocusable(index + 1, index);
    SyncTextField(true);
    SetDiagnostics(state_.preview.diagnostics);
    return true;
}

bool EditorSession::SetDiagnosticFilter(std::string_view filter) {
    if (!state_.open || !snapshot_)
        return false;
    state_.diagnostic_panel = BuildDiagnosticPanelProjection(
        workspace_.DescribeDocuments(*snapshot_), state_.diagnostics, filter);
    return true;
}

bool EditorSession::LocateDiagnostic(std::size_t index) {
    if (!state_.open || index >= state_.diagnostic_panel.items.size())
        return false;
    const auto& item = state_.diagnostic_panel.items[index];
    if (item.document_id.empty() || !SelectDocument(item.document_id))
        return false;
    for (std::size_t field_index = 0; field_index < state_.form.fields.size(); ++field_index) {
        if (state_.form.fields[field_index].path == item.diagnostic.path)
            return SelectField(field_index);
    }
    return true;
}

bool EditorSession::SelectNext() {
    if (!state_.open || state_.form.fields.empty())
        return false;
    if (!focus_context_.MoveFocus(1))
        return false;
    state_.selected_field = static_cast<std::size_t>(focus_context_.focused_widget() - 1);
    SyncTextField(true);
    return state_.selected_field < state_.form.fields.size();
}

bool EditorSession::SelectPrevious() {
    if (!state_.open || state_.form.fields.empty())
        return false;
    if (!focus_context_.MoveFocus(-1))
        return false;
    state_.selected_field = static_cast<std::size_t>(focus_context_.focused_widget() - 1);
    SyncTextField(true);
    return state_.selected_field < state_.form.fields.size();
}

bool EditorSession::SelectField(std::size_t index) {
    if (!state_.open || index >= state_.form.fields.size())
        return false;
    if (!focus_context_.SetFocus(index + 1))
        return false;
    state_.selected_field = index;
    SyncTextField(true);
    return true;
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
    if (!state_.open || state_.form.fields.empty() ||
        state_.selected_field >= state_.form.fields.size())
        return false;
    const auto& field = state_.form.fields[state_.selected_field];
    if (field.read_only || (field.value_type != "string" && field.value_type != "integer"))
        return false;
    const auto value_type = field.value_type;
    std::vector<ui::UiCommand> commands;
    if (!text_field_.Apply({.type = ui::UiEventType::kTextInput, .text = std::string(value)},
                           commands, state_.selected_field + 1))
        return false;
    for (const auto& command : commands) {
        if (command.type != ui::UiCommandType::kTextChanged)
            continue;
        if (value_type == "string") {
            const bool applied = ApplySelected(command.text);
            if (applied)
                SyncTextField(false);
            return applied;
        }
        std::int64_t parsed = 0;
        const auto [end, error] = std::from_chars(command.text.data(),
                                                  command.text.data() + command.text.size(), parsed);
        if (error != std::errc{} || end != command.text.data() + command.text.size()) {
            SetDiagnostics({{"project.edit.integer_invalid", field.path}});
            return false;
        }
        const bool applied = ApplySelected(parsed);
        if (applied)
            SyncTextField(false);
        return applied;
    }
    return false;
}

bool EditorSession::ApplySelectedComposition(std::string_view value) {
    if (!state_.open || state_.form.fields.empty() ||
        state_.selected_field >= state_.form.fields.size())
        return false;
    const auto& field = state_.form.fields[state_.selected_field];
    if (field.read_only || field.value_type != "string")
        return false;
    std::vector<ui::UiCommand> commands;
    const bool applied = text_field_.Apply(
        {.type = ui::UiEventType::kTextComposition, .text = std::string(value)}, commands,
        state_.selected_field + 1);
    SyncTextField(false);
    return applied;
}

bool EditorSession::ApplySelectedKey(std::string_view key) {
    if (!state_.open || state_.form.fields.empty() ||
        state_.selected_field >= state_.form.fields.size())
        return false;
    const auto& field = state_.form.fields[state_.selected_field];
    if (field.read_only || field.value_type != "string")
        return false;
    ui::UiEvent event{.type = ui::UiEventType::kKeyDown, .text = std::string(key)};
    if (key == "Enter")
        event.type = ui::UiEventType::kConfirm;
    else if (key == "Escape")
        event.type = ui::UiEventType::kCancel;
    std::vector<ui::UiCommand> commands;
    if (!text_field_.Apply(event, commands, state_.selected_field + 1))
        return false;
    for (const auto& command : commands) {
        if (command.type != ui::UiCommandType::kTextChanged)
            continue;
        const bool applied = ApplySelected(command.text);
        if (applied)
            SyncTextField(false);
        return applied;
    }
    PublishTextFieldState();
    return true;
}

bool EditorSession::AdjustSelectedInteger(int delta) {
    if (!state_.open || state_.form.fields.empty() ||
        state_.selected_field >= state_.form.fields.size() || (delta != -1 && delta != 1))
        return false;
    const auto& field = state_.form.fields[state_.selected_field];
    if (field.read_only || field.value_type != "integer" || !field.value.is_number_integer())
        return false;
    const auto current = field.value.get<std::int64_t>();
    if ((delta > 0 && current == std::numeric_limits<std::int64_t>::max()) ||
        (delta < 0 && current == std::numeric_limits<std::int64_t>::min()))
        return false;
    const bool applied = ApplySelected(current + delta);
    if (applied)
        SyncTextField(false);
    return applied;
}

bool EditorSession::ToggleSelectedBoolean() {
    if (!state_.open || state_.form.fields.empty() ||
        state_.selected_field >= state_.form.fields.size())
        return false;
    const auto& field = state_.form.fields[state_.selected_field];
    if (field.read_only || field.value_type != "boolean" || !field.value.is_boolean())
        return false;
    return ApplySelected(!field.value.get<bool>());
}

bool EditorSession::CycleSelectedChoice(int direction) {
    if (!state_.open || state_.form.fields.empty() ||
        state_.selected_field >= state_.form.fields.size() || (direction != -1 && direction != 1))
        return false;
    const auto& field = state_.form.fields[state_.selected_field];
    if (field.read_only || field.value_type != "select" || !field.value.is_string() ||
        field.choices.empty())
        return false;
    const auto current = std::find(field.choices.begin(), field.choices.end(),
                                    field.value.get<std::string>());
    if (current == field.choices.end())
        return false;
    const auto index = static_cast<std::ptrdiff_t>(current - field.choices.begin());
    const auto count = static_cast<std::ptrdiff_t>(field.choices.size());
    const auto next = (index + direction + count) % count;
    return ApplySelected(field.choices[static_cast<std::size_t>(next)]);
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

bool EditorSession::StartPreview(const std::filesystem::path& executable) {
    if (!state_.open)
        return false;
    const bool started = preview_process_.Start(executable, root_);
    state_.preview.process_running = preview_process_.state().running;
    state_.preview.process_exit_code = preview_process_.state().exit_code;
    state_.preview.process_error = preview_process_.state().error;
    state_.preview.standard_output = preview_process_.state().standard_output;
    state_.preview.standard_error = preview_process_.state().standard_error;
    return started;
}

bool EditorSession::PollPreview() {
    const auto before = preview_process_.state();
    preview_process_.Poll();
    const auto& after = preview_process_.state();
    state_.preview.process_running = after.running;
    state_.preview.process_exit_code = after.exit_code;
    state_.preview.process_error = after.error;
    state_.preview.standard_output = after.standard_output;
    state_.preview.standard_error = after.standard_error;
    return before.running != after.running || before.exit_code != after.exit_code ||
           before.error != after.error || before.standard_output != after.standard_output ||
           before.standard_error != after.standard_error;
}

} // namespace jrpgmaker::editor
