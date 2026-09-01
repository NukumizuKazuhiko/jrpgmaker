#include "jrpgmaker/editor/editor_session.hpp"
#include "jrpgmaker/core/map_data.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <fstream>
#include <limits>
#include <unordered_set>
#include <utility>

namespace jrpgmaker::editor {

namespace {

constexpr std::string_view kReadOnlyPluginDocumentType = "plugin.read_only.document";

void AddPluginDiagnostic(std::vector<project::Diagnostic>& diagnostics,
                         const plugin::PluginError& error) {
    diagnostics.push_back({error.code, error.path});
}

void AddPluginDiagnosticAt(std::vector<project::Diagnostic>& diagnostics,
                           const plugin::PluginError& error,
                           const std::filesystem::path& resource_path) {
    const auto path = resource_path.generic_string();
    diagnostics.push_back(
        {error.code, path + (error.path.empty() ? std::string{} : ":" + error.path)});
}

bool ReadJson(const std::filesystem::path& path, nlohmann::json& document,
              std::vector<project::Diagnostic>& diagnostics) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > plugin::kMaxPluginValidationFileBytes) {
        diagnostics.push_back({"editor.plugin.resource_file_size", path.string()});
        return false;
    }
    std::ifstream file(path);
    if (!file.is_open()) {
        diagnostics.push_back({"editor.plugin.resource_missing", path.string()});
        return false;
    }
    try {
        file >> document;
    } catch (const std::exception&) {
        diagnostics.push_back({"editor.plugin.resource_json", path.string()});
        return false;
    }
    return true;
}

bool IsNavigationCellSelection(const std::optional<SelectionTarget>& selection) {
    return selection.has_value() && selection->kind == "navigation.cell";
}

bool IsReadOnlyDocument(const EditorSessionState& state) {
    const auto current = std::find_if(
        state.tabs.tabs.begin(), state.tabs.tabs.end(),
        [&state](const auto& document) { return document.document_id == state.form.document_id; });
    return current != state.tabs.tabs.end() && !current->editable;
}

} // namespace

EditorSession::EditorSession(std::filesystem::path root)
    : root_(root), workspace_(std::move(root), adapters_) {}

EditorSession::EditorSession(std::filesystem::path root, project::DocumentAdapterRegistry adapters,
                             const plugin::PluginRegistry* plugins)
    : adapters_(std::move(adapters)), plugins_(plugins), root_(root),
      workspace_(std::move(root), adapters_, plugins_) {}

bool EditorSession::Open(std::filesystem::path root) {
    if (root.empty())
        return false;
    preview_process_.Stop();
    root_ = std::move(root);
    workspace_ = project::ProjectWorkspace(root_, adapters_, plugins_);
    snapshot_.reset();
    state_ = {};
    focus_context_.ClearFocusables();
    return Open();
}

void EditorSession::SetDiagnostics(std::vector<project::Diagnostic> diagnostics) {
    state_.diagnostics = std::move(diagnostics);
    if (snapshot_)
        state_.tabs = BuildDocumentTabsProjection(workspace_.DescribeDocuments(*snapshot_),
                                                  state_.form.document_id,
                                                  workspace_.PendingChanges(), state_.diagnostics);
    if (snapshot_)
        state_.diagnostic_panel =
            BuildDiagnosticPanelProjection(workspace_.DescribeDocuments(*snapshot_),
                                           state_.diagnostics, state_.diagnostic_panel.filter);
}

void EditorSession::RebuildProjection() {
    if (!snapshot_)
        return;
    const auto documents = workspace_.DescribeDocuments(*snapshot_);
    const auto current =
        std::find_if(documents.begin(), documents.end(), [this](const auto& document) {
            return document.id == workspace_.CurrentDocumentId();
        });
    const auto adapter_id = current == documents.end() || current->type_id.empty()
                                ? std::string(workspace_.CurrentDocumentId())
                                : current->type_id;
    const auto* adapter = adapters_.Find(adapter_id);
    if (adapter == nullptr || !snapshot_)
        return;
    state_.form = BuildFormProjection(*adapter, workspace_.CurrentDocument());
    state_.form.document_id = std::string(workspace_.CurrentDocumentId());
    state_.preview = BuildWorkspacePreview(workspace_.Diagnose(*snapshot_));
    state_.tabs =
        BuildDocumentTabsProjection(documents, state_.form.document_id, workspace_.PendingChanges(),
                                    state_.preview.diagnostics);
    state_.diff = BuildDiffProjection(workspace_.PendingChanges());
    state_.revision = snapshot_->revision;
    const auto previous_selection = state_.selection;
    state_.navigation.reset();
    if (workspace_.CurrentDocumentId() == "core.navigation") {
        try {
            const auto grid = core::ParseNavigationGrid(workspace_.CurrentDocument());
            NavigationProjection navigation;
            navigation.document_id = "core.navigation";
            navigation.width = grid.width();
            navigation.height = grid.height();
            const auto count = std::min<std::size_t>(static_cast<std::size_t>(grid.width()) *
                                                         static_cast<std::size_t>(grid.height()),
                                                     NavigationProjection::kMaxProjectedCells);
            navigation.cells.reserve(count);
            for (std::size_t index = 0; index < count; ++index) {
                const int x = static_cast<int>(index % static_cast<std::size_t>(grid.width()));
                const int y = static_cast<int>(index / static_cast<std::size_t>(grid.width()));
                navigation.cells.push_back({x, y, grid.IsWalkable({x, y}), false, {}});
            }
            state_.navigation = std::move(navigation);
            if (previous_selection && previous_selection->document_id == "core.navigation") {
                const auto prefix = std::string("/walkable/");
                if (previous_selection->object_path.rfind(prefix, 0) == 0) {
                    try {
                        (void) SelectNavigationCell(
                            std::stoull(previous_selection->object_path.substr(prefix.size())));
                    } catch (...) {
                        state_.selection.reset();
                    }
                }
            }
        } catch (const std::exception&) {
            state_.navigation.reset();
        }
    }
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
    const auto plugin_diagnostics = LoadPluginEditorAdapters();
    workspace_ = project::ProjectWorkspace(root_, adapters_, plugins_);
    workspace_.SetExternalDocuments(external_documents_);
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
    state_.selection = SelectionTarget{state_.form.document_id, "/", "document"};
    focus_context_.ClearFocusables();
    for (std::size_t index = 0; index < state_.form.fields.size(); ++index)
        (void) focus_context_.RegisterFocusable(index + 1, index);
    SyncTextField(true);
    if (plugin_diagnostics.empty()) {
        SetDiagnostics(state_.preview.diagnostics);
    } else {
        auto diagnostics = plugin_diagnostics;
        diagnostics.insert(diagnostics.end(), state_.preview.diagnostics.begin(),
                           state_.preview.diagnostics.end());
        SetDiagnostics(std::move(diagnostics));
    }
    return state_.preview.valid;
}

std::vector<project::Diagnostic> EditorSession::LoadPluginEditorAdapters() {
    std::vector<project::Diagnostic> diagnostics;
    external_documents_.clear();
    nlohmann::json project_document;
    if (!ReadJson(root_ / "project.json", project_document, diagnostics))
        return diagnostics;
    const auto parsed_project = plugin::ParseProjectManifest(project_document);
    if (!parsed_project) {
        AddPluginDiagnostic(diagnostics, *parsed_project.error);
        return diagnostics;
    }
    std::unordered_set<std::string> loaded_types;
    std::unordered_set<std::string> external_ids;
    std::unordered_set<std::string> external_paths;
    const auto ensure_read_only_adapter = [&]() {
        if (adapters_.Find(std::string(kReadOnlyPluginDocumentType)) != nullptr)
            return true;
        const auto result = adapters_.Register(project::DocumentAdapter{
            .type_id = std::string(kReadOnlyPluginDocumentType),
            .fields = {},
            .validate = [](const nlohmann::json&) { return std::vector<project::Diagnostic>{}; },
            .normalize_edit = {}});
        diagnostics.insert(diagnostics.end(), result.diagnostics.begin(), result.diagnostics.end());
        return static_cast<bool>(result);
    };
    const auto discover_read_only = [&](std::string_view plugin_id,
                                        const std::vector<std::string>& roots) {
        if (!ensure_read_only_adapter())
            return;
        std::size_t discovered = 0;
        std::error_code error;
        for (const auto& root : roots) {
            if (discovered >= 128)
                break;
            const auto root_path = root_ / root;
            if (!std::filesystem::exists(root_path, error))
                continue;
            const auto add_file = [&](const std::filesystem::path& path) {
                if (discovered >= 128 || path.extension() != ".json")
                    return;
                const auto relative = path.lexically_relative(root_).generic_string();
                if (!external_paths.insert(relative).second)
                    return;
                const auto document_id =
                    "plugin:readonly:" + std::string(plugin_id) + ":" + relative;
                if (!external_ids.insert(document_id).second)
                    return;
                external_documents_.push_back(project::DocumentDescriptor{
                    .id = document_id,
                    .path = relative,
                    .label_key = "plugin." + std::string(plugin_id) + ".document",
                    .editable = false,
                    .type_id = std::string(kReadOnlyPluginDocumentType),
                    .category_key = "editor.project.category.plugins"});
                diagnostics.push_back({"editor.document.read_only", relative});
                ++discovered;
            };
            if (std::filesystem::is_regular_file(root_path, error)) {
                add_file(root_path);
                continue;
            }
            if (!std::filesystem::is_directory(root_path, error))
                continue;
            std::error_code iterator_error;
            for (std::filesystem::recursive_directory_iterator it(root_path, iterator_error), end;
                 it != end && !iterator_error; it.increment(iterator_error)) {
                if (it->is_regular_file(iterator_error) && !iterator_error)
                    add_file(it->path());
                if (discovered >= 128)
                    break;
            }
            if (iterator_error)
                diagnostics.push_back({"editor.document_root.enumeration", root});
        }
    };
    for (const auto& id : parsed_project.manifest->plugins) {
        const auto plugin_root = root_ / "plugins" / id;
        const auto sidecar_path = plugin_root / "plugin.editor.json";
        std::error_code error;
        std::optional<plugin::PluginManifest> manifest;
        if (plugins_ != nullptr)
            manifest = plugins_->FindManifest(id);
        if (!manifest.has_value()) {
            nlohmann::json manifest_document;
            if (!ReadJson(plugin_root / "plugin.json", manifest_document, diagnostics))
                continue;
            const auto parsed_manifest = plugin::ParseManifest(manifest_document);
            if (!parsed_manifest) {
                AddPluginDiagnostic(diagnostics, *parsed_manifest.error);
                continue;
            }
            manifest = parsed_manifest.manifest;
        }
        if (!std::filesystem::exists(sidecar_path, error)) {
            diagnostics.push_back({"editor.plugin.sidecar_missing", sidecar_path.string()});
            discover_read_only(id, manifest->data_roots);
            continue;
        }
        nlohmann::json sidecar_document;
        if (!ReadJson(sidecar_path, sidecar_document, diagnostics)) {
            discover_read_only(id, manifest->data_roots);
            continue;
        }
        const auto extension = plugin::ParseEditorExtension(sidecar_document);
        if (!extension) {
            AddPluginDiagnosticAt(diagnostics, *extension.error, sidecar_path);
            discover_read_only(id, manifest->data_roots);
            continue;
        }
        if (const auto contract_error =
                plugin::ValidateEditorExtension(*extension.extension, *manifest);
            contract_error.has_value()) {
            AddPluginDiagnosticAt(diagnostics, *contract_error, sidecar_path);
            discover_read_only(id, manifest->data_roots);
            continue;
        }
        const auto resource_errors =
            plugin::ValidateEditorExtensionResources(*extension.extension, *manifest, plugin_root);
        for (const auto& resource_error : resource_errors)
            AddPluginDiagnosticAt(diagnostics, resource_error, sidecar_path);
        if (!resource_errors.empty()) {
            discover_read_only(id, manifest->data_roots);
            continue;
        }
        for (const auto& document : extension.extension->documents) {
            nlohmann::json descriptor_document;
            const auto descriptor_path = plugin_root / document.descriptor;
            if (!ReadJson(descriptor_path, descriptor_document, diagnostics)) {
                discover_read_only(id, manifest->data_roots);
                continue;
            }
            const auto descriptor = plugin::ParseEditorDescriptor(descriptor_document);
            if (!descriptor) {
                AddPluginDiagnosticAt(diagnostics, *descriptor.error, descriptor_path);
                discover_read_only(id, manifest->data_roots);
                continue;
            }
            if (descriptor.descriptor->type_id != document.type_id) {
                AddPluginDiagnosticAt(
                    diagnostics,
                    plugin::PluginError{"editor_descriptor.type_id",
                                        "descriptor type_id does not match sidecar",
                                        document.type_id},
                    descriptor_path);
                discover_read_only(id, manifest->data_roots);
                continue;
            }
            if (!loaded_types.insert(document.type_id).second) {
                AddPluginDiagnosticAt(diagnostics,
                                      plugin::PluginError{"editor.document.duplicate_type",
                                                          "editor document type is already loaded",
                                                          document.type_id},
                                      descriptor_path);
                discover_read_only(id, manifest->data_roots);
                continue;
            }
            const auto result =
                project::RegisterEditorDescriptor(adapters_, *descriptor.descriptor, {});
            diagnostics.insert(diagnostics.end(), result.diagnostics.begin(),
                               result.diagnostics.end());
            if (!result) {
                discover_read_only(id, manifest->data_roots);
                continue;
            }

            for (const auto& root : document.roots) {
                const auto root_path = root_ / root;
                if (!std::filesystem::exists(root_path, error)) {
                    AddPluginDiagnostic(
                        diagnostics, plugin::PluginError{"editor.document_root.missing",
                                                         "plugin document root is missing", root});
                    continue;
                }
                std::size_t discovered = 0;
                std::error_code iterator_error;
                for (std::filesystem::recursive_directory_iterator it(root_path, iterator_error),
                     end;
                     it != end && !iterator_error; it.increment(iterator_error)) {
                    if (discovered >= 128)
                        break;
                    if (!it->is_regular_file(iterator_error) || iterator_error ||
                        it->path().extension() != ".json")
                        continue;
                    const auto relative = it->path().lexically_relative(root_).generic_string();
                    const auto document_id = "plugin:" + document.type_id + ":" + relative;
                    if (!external_paths.insert(relative).second)
                        continue;
                    if (!external_ids.insert(document_id).second)
                        continue;
                    external_documents_.push_back(project::DocumentDescriptor{
                        .id = document_id,
                        .path = relative,
                        .label_key = "plugin." + id + ".document",
                        .editable = true,
                        .type_id = document.type_id,
                        .category_key = "editor.project.category.plugins"});
                    ++discovered;
                }
                if (iterator_error)
                    diagnostics.push_back({"editor.document_root.enumeration", root});
            }
        }
    }
    return diagnostics;
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
    state_.selection = SelectionTarget{std::string(document_id), "/", "document"};
    RebuildProjection();
    focus_context_.ClearFocusables();
    for (std::size_t index = 0; index < state_.form.fields.size(); ++index)
        (void) focus_context_.RegisterFocusable(index + 1, index);
    SyncTextField(true);
    SetDiagnostics(state_.preview.diagnostics);
    return true;
}

bool EditorSession::SelectNavigationCell(std::size_t index) {
    if (!state_.open || !state_.navigation || index >= state_.navigation->cells.size())
        return false;
    auto& navigation = *state_.navigation;
    if (navigation.selected)
        navigation.cells[*navigation.selected].selected = false;
    navigation.selected = index;
    navigation.cells[index].selected = true;
    state_.selection =
        SelectionTarget{"core.navigation", "/walkable/" + std::to_string(index), "navigation.cell"};
    return true;
}

bool EditorSession::ToggleSelectedNavigationWalkable() {
    if (!state_.selection || !state_.navigation || !state_.navigation->selected)
        return false;
    const auto index = *state_.navigation->selected;
    const auto result = workspace_.Apply({"core.navigation", "/walkable/" + std::to_string(index),
                                          !state_.navigation->cells[index].walkable});
    if (!result) {
        SetDiagnostics(result.diagnostics);
        return false;
    }
    state_.dirty = true;
    state_.revision = result.revision;
    snapshot_->revision = result.revision;
    RebuildProjection();
    SetDiagnostics({});
    return true;
}

void EditorSession::StopPreview() {
    preview_process_.Stop();
    state_.preview.process_running = preview_process_.state().running;
    state_.preview.process_exit_code = preview_process_.state().exit_code;
    state_.preview.process_error = preview_process_.state().error;
    state_.preview.standard_output = preview_process_.state().standard_output;
    state_.preview.standard_error = preview_process_.state().standard_error;
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
    if (!state_.open)
        return false;
    if (IsReadOnlyDocument(state_)) {
        SetDiagnostics({{"project.edit.document_read_only", state_.form.document_id}});
        return false;
    }
    if (state_.form.fields.empty() || state_.selected_field >= state_.form.fields.size())
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
    if (!state_.open)
        return false;
    if (IsReadOnlyDocument(state_)) {
        SetDiagnostics({{"project.edit.document_read_only", state_.form.document_id}});
        return false;
    }
    if (IsNavigationCellSelection(state_.selection) || state_.form.fields.empty() ||
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
        const auto [end, error] =
            std::from_chars(command.text.data(), command.text.data() + command.text.size(), parsed);
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
    if (!state_.open)
        return false;
    if (IsReadOnlyDocument(state_)) {
        SetDiagnostics({{"project.edit.document_read_only", state_.form.document_id}});
        return false;
    }
    if (IsNavigationCellSelection(state_.selection) || state_.form.fields.empty() ||
        state_.selected_field >= state_.form.fields.size())
        return false;
    const auto& field = state_.form.fields[state_.selected_field];
    if (field.read_only || field.value_type != "string")
        return false;
    std::vector<ui::UiCommand> commands;
    const bool applied =
        text_field_.Apply({.type = ui::UiEventType::kTextComposition, .text = std::string(value)},
                          commands, state_.selected_field + 1);
    SyncTextField(false);
    return applied;
}

bool EditorSession::ApplySelectedKey(std::string_view key) {
    if (!state_.open)
        return false;
    if (IsReadOnlyDocument(state_)) {
        SetDiagnostics({{"project.edit.document_read_only", state_.form.document_id}});
        return false;
    }
    if (IsNavigationCellSelection(state_.selection) || state_.form.fields.empty() ||
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
    if (!state_.open)
        return false;
    if (IsReadOnlyDocument(state_)) {
        SetDiagnostics({{"project.edit.document_read_only", state_.form.document_id}});
        return false;
    }
    if (state_.form.fields.empty() || state_.selected_field >= state_.form.fields.size() ||
        (delta != -1 && delta != 1))
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
    if (!state_.open)
        return false;
    if (IsReadOnlyDocument(state_)) {
        SetDiagnostics({{"project.edit.document_read_only", state_.form.document_id}});
        return false;
    }
    if (state_.form.fields.empty() || state_.selected_field >= state_.form.fields.size())
        return false;
    const auto& field = state_.form.fields[state_.selected_field];
    if (field.read_only || field.value_type != "boolean" || !field.value.is_boolean())
        return false;
    return ApplySelected(!field.value.get<bool>());
}

bool EditorSession::CycleSelectedChoice(int direction) {
    if (!state_.open)
        return false;
    if (IsReadOnlyDocument(state_)) {
        SetDiagnostics({{"project.edit.document_read_only", state_.form.document_id}});
        return false;
    }
    if (state_.form.fields.empty() || state_.selected_field >= state_.form.fields.size() ||
        (direction != -1 && direction != 1))
        return false;
    const auto& field = state_.form.fields[state_.selected_field];
    if (field.read_only || field.value_type != "select" || !field.value.is_string() ||
        field.choices.empty())
        return false;
    const auto current =
        std::find(field.choices.begin(), field.choices.end(), field.value.get<std::string>());
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
        RebuildProjection();
        SetDiagnostics({});
        return true;
    }
    const auto result = workspace_.Commit(*plan.token);
    if (!result) {
        SetDiagnostics(result.diagnostics);
        return false;
    }
    state_.dirty = false;
    RebuildProjection();
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
