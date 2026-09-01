#include "jrpgmaker/editor/editor_workspace_controller.hpp"

#include <algorithm>
#include <charconv>
#include <string>
#include <vector>

namespace jrpgmaker::editor {
namespace {

bool Contains(const ui::Rect& bounds, float x, float y) {
    return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y &&
           y < bounds.y + bounds.height;
}

void Append(ui::DrawList& target, const ui::DrawList& source) {
    for (const auto& primitive : source.primitives())
        (void) target.Add(primitive);
}

std::uint64_t StableMenuId(std::string_view value) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto character : value) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 1099511628211ull;
    }
    return hash == 0 ? 1 : hash;
}

bool HasDocument(const EditorSessionState* state, std::string_view document_id) {
    if (state == nullptr || !state->open)
        return false;
    return std::any_of(
        state->tabs.tabs.begin(), state->tabs.tabs.end(),
        [document_id](const auto& document) { return document.document_id == document_id; });
}

bool HasCategory(const EditorSessionState* state, std::string_view category_key) {
    if (state == nullptr || !state->open)
        return false;
    return std::any_of(
        state->tabs.tabs.begin(), state->tabs.tabs.end(),
        [category_key](const auto& document) { return document.category_key == category_key; });
}

bool SameDiagnostics(const std::vector<project::Diagnostic>& left,
                     const std::vector<project::Diagnostic>& right) {
    if (left.size() != right.size())
        return false;
    return std::equal(left.begin(), left.end(), right.begin(),
                      [](const auto& lhs, const auto& rhs) {
                          return lhs.code == rhs.code && lhs.path == rhs.path;
                      });
}

bool SelectDocumentAndRefresh(EditorSession* session, std::string_view document_id) {
    if (session == nullptr || document_id.empty())
        return false;
    const auto diagnostics = session->state().diagnostics;
    const bool selected = session->SelectDocument(document_id);
    return selected || !SameDiagnostics(diagnostics, session->state().diagnostics);
}

bool CommandEnabled(std::string_view command, const EditorSessionState* state) {
    if (command == "file.open" || command.rfind("window.focus.", 0) == 0)
        return true;
    if (command == "file.save" || command == "file.refresh" || command == "run.preview")
        return state != nullptr && state->open &&
               (command != "run.preview" || !state->preview.process_running);
    if (command == "run.stop")
        return state != nullptr && state->preview.process_running;
    if (command.rfind("document.", 0) == 0) {
        if (command == "document.plugin")
            return HasCategory(state, "editor.project.category.plugins");
        return HasDocument(state, command.substr(std::string_view{"document."}.size()));
    }
    return false;
}

std::string PanelLabelKey(std::string_view panel_id) {
    if (panel_id == "workspace.project")
        return "editor.workspace.project";
    if (panel_id == "workspace.hierarchy")
        return "editor.workspace.hierarchy";
    if (panel_id == "workspace.scene")
        return "editor.workspace.scene";
    if (panel_id == "workspace.form")
        return "editor.workspace.inspector";
    if (panel_id == "workspace.diagnostics")
        return "editor.workspace.diagnostics";
    return {};
}

std::optional<ui::MenuItem> MakeMenuItem(const ShellProjection& shell, std::size_t index,
                                         const EditorSessionState* state) {
    if (index >= shell.nodes.size())
        return std::nullopt;
    const auto& node = shell.nodes[index];
    ui::MenuItem item{.id = StableMenuId(node.id),
                      .label_key = node.label_key,
                      .command = node.command,
                      .enabled = node.command.empty() || CommandEnabled(node.command, state),
                      .children = {}};
    for (const auto child : node.children) {
        const auto child_item = MakeMenuItem(shell, child, state);
        if (child_item)
            item.children.push_back(*child_item);
    }
    if (item.command.empty() && item.children.empty())
        return std::nullopt;
    if (!item.command.empty() && !item.children.empty())
        return std::nullopt;
    return item;
}

} // namespace

EditorWorkspaceController::EditorWorkspaceController(EditorWorkspaceConfig config)
    : config_(std::move(config)), shell_(BuildShellProjection(config_.layout)) {
    RebuildMenuModel();
}

bool EditorWorkspaceController::Resize(float width, float height) {
    if (!shell_ || !(width > 0.0f) || !(height > 0.0f))
        return false;
    ReflowShellProjection(*shell_, width, height);
    if (const auto* menu = FindPanel("workspace.menu"))
        (void) menu_.Layout(menu->bounds, config_.menu);
    return true;
}

bool EditorWorkspaceController::OpenProject(const std::filesystem::path& root) {
    if (root.empty())
        return false;
    if (!session_)
        session_ = std::make_unique<EditorSession>(root);
    project_filter_.clear();
    project_filter_text_.SetText({});
    project_filter_focused_ = false;
    focused_panel_ = "workspace.scene";
    const auto opened = session_->Open(root);
    RebuildMenuModel();
    return opened;
}

void EditorWorkspaceController::RebuildMenuModel() {
    if (!shell_)
        return;
    const auto* menu = FindPanel("workspace.menu");
    if (menu == nullptr)
        return;
    ui::MenuBarModel model;
    for (const auto child : menu->children) {
        const auto item = MakeMenuItem(*shell_, child, state());
        if (item)
            model.roots.push_back(*item);
    }
    (void) menu_.SetModel(std::move(model));
    (void) menu_.Layout(menu->bounds, config_.menu);
}

const ShellNode* EditorWorkspaceController::FindPanel(std::string_view id) const {
    if (!shell_)
        return nullptr;
    const auto panel = std::find_if(shell_->nodes.begin(), shell_->nodes.end(),
                                    [id](const auto& node) { return node.id == id; });
    return panel == shell_->nodes.end() ? nullptr : &*panel;
}

std::optional<ui::Rect> EditorWorkspaceController::panel_bounds(std::string_view id) const {
    const auto* panel = FindPanel(id);
    return panel == nullptr ? std::nullopt : std::optional<ui::Rect>{panel->bounds};
}

const EditorSessionState* EditorWorkspaceController::state() const {
    return session_ == nullptr ? nullptr : &session_->state();
}

EditorInteractionResult EditorWorkspaceController::PointerMove(float x, float y) {
    if (!menu_.open()) {
        std::optional<EditorAction> hovered;
        if (const auto* toolbar = FindPanel("workspace.toolbar");
            toolbar != nullptr && Contains(toolbar->bounds, x, y))
            hovered = HitToolbar(
                BuildToolbarProjection(toolbar->bounds, state() != nullptr && state()->open,
                                       state() != nullptr && state()->preview.process_running),
                x, y);
        const bool changed = hovered_toolbar_ != hovered || pressed_toolbar_.has_value();
        hovered_toolbar_ = hovered;
        pressed_toolbar_.reset();
        return {changed, EditorHostRequest::kNone};
    }
    const auto result = menu_.PointerMove(x, y);
    return {result.changed, EditorHostRequest::kNone};
}

EditorInteractionResult EditorWorkspaceController::DispatchMenuCommand(std::string_view command) {
    if (command == "file.open")
        return {false, EditorHostRequest::kOpenProjectDialog};
    if (session_ == nullptr)
        return {};
    if (command == "file.save")
        return {session_->Save(), EditorHostRequest::kNone};
    if (command == "file.refresh")
        return {session_->Refresh(), EditorHostRequest::kNone};
    if (command == "run.preview")
        return {session_->StartPreview(config_.runtime_executable), EditorHostRequest::kNone};
    if (command == "run.stop") {
        session_->StopPreview();
        return {true, EditorHostRequest::kNone};
    }
    if (command.rfind("document.", 0) == 0) {
        auto document_id = command.substr(std::string_view{"document."}.size());
        if (command == "document.plugin") {
            const auto plugin =
                std::find_if(session_->state().tabs.tabs.begin(), session_->state().tabs.tabs.end(),
                             [](const auto& tab) {
                                 return tab.category_key == "editor.project.category.plugins";
                             });
            if (plugin == session_->state().tabs.tabs.end())
                return {};
            document_id = plugin->document_id;
        }
        return {SelectDocumentAndRefresh(session_.get(), document_id), EditorHostRequest::kNone};
    }
    constexpr std::string_view focus_prefix = "window.focus.";
    if (command.rfind(focus_prefix, 0) == 0) {
        const auto panel_id = std::string(command.substr(focus_prefix.size()));
        const auto* panel = FindPanel(panel_id);
        if (panel == nullptr)
            return {};
        focused_panel_ = panel_id;
        return {true, EditorHostRequest::kNone};
    }
    return {};
}

EditorInteractionResult EditorWorkspaceController::DispatchCommand(std::string_view command,
                                                                   std::string_view argument) {
    if (command == "document.select")
        return SelectDocumentAndRefresh(session_.get(), argument)
                   ? EditorInteractionResult{true, EditorHostRequest::kNone}
                   : EditorInteractionResult{};
    if (command == "navigation.select") {
        std::size_t index = 0;
        const auto [end, error] =
            std::from_chars(argument.data(), argument.data() + argument.size(), index);
        if (error != std::errc{} || end != argument.data() + argument.size())
            return {};
        return session_ != nullptr && session_->SelectNavigationCell(index)
                   ? EditorInteractionResult{true, EditorHostRequest::kNone}
                   : EditorInteractionResult{};
    }
    if (command == "navigation.toggle")
        return session_ != nullptr && session_->ToggleSelectedNavigationWalkable()
                   ? EditorInteractionResult{true, EditorHostRequest::kNone}
                   : EditorInteractionResult{};
    if (command == "project.filter")
        return SetProjectFilter(argument) ? EditorInteractionResult{true, EditorHostRequest::kNone}
                                          : EditorInteractionResult{};
    if (command == "window.focus") {
        const auto* panel = FindPanel(argument);
        if (panel == nullptr)
            return {};
        focused_panel_ = std::string(argument);
        return {true, EditorHostRequest::kNone};
    }
    return DispatchMenuCommand(command);
}

bool EditorWorkspaceController::SetProjectFilter(std::string_view filter) {
    if (project_filter_ == filter)
        return false;
    project_filter_ = filter;
    project_filter_text_.SetText(project_filter_);
    return true;
}

EditorInteractionResult EditorWorkspaceController::KeyDown(std::string_view key) {
    if (!menu_.open())
        return {};
    const auto result = menu_.KeyDown(key);
    EditorInteractionResult interaction{result.changed, EditorHostRequest::kNone};
    for (const auto& command : result.commands) {
        const auto dispatched = DispatchMenuCommand(command.text);
        interaction.changed = interaction.changed || dispatched.changed;
        if (dispatched.host_request != EditorHostRequest::kNone)
            interaction.host_request = dispatched.host_request;
    }
    if (!result.commands.empty())
        RebuildMenuModel();
    return interaction;
}

EditorInteractionResult EditorWorkspaceController::PointerDown(float x, float y) {
    if (menu_.open() || (FindPanel("workspace.menu") != nullptr &&
                         Contains(FindPanel("workspace.menu")->bounds, x, y))) {
        const auto result = menu_.PointerDown(x, y);
        EditorInteractionResult interaction{result.changed, EditorHostRequest::kNone};
        for (const auto& command : result.commands) {
            const auto dispatched = DispatchMenuCommand(command.text);
            interaction.changed = interaction.changed || dispatched.changed;
            if (dispatched.host_request != EditorHostRequest::kNone)
                interaction.host_request = dispatched.host_request;
        }
        if (!result.commands.empty())
            RebuildMenuModel();
        return interaction;
    }
    project_filter_focused_ = false;
    if (const auto* toolbar = FindPanel("workspace.toolbar");
        toolbar != nullptr && Contains(toolbar->bounds, x, y)) {
        const auto button = HitToolbar(
            BuildToolbarProjection(toolbar->bounds, state() != nullptr && state()->open,
                                   state() != nullptr && state()->preview.process_running),
            x, y);
        if (!button)
            return {};
        pressed_toolbar_ = button;
        focused_toolbar_ = button;
        if (session_ == nullptr)
            return *button == EditorAction::kOpen
                       ? EditorInteractionResult{false, EditorHostRequest::kOpenProjectDialog}
                       : EditorInteractionResult{};
        const auto result = Dispatch(*button);
        RebuildMenuModel();
        return result;
    }
    if (session_ == nullptr)
        return {};
    if (const auto* project = FindPanel("workspace.project");
        project != nullptr && Contains(project->bounds, x, y)) {
        project_filter_focused_ = false;
        const auto row =
            static_cast<std::size_t>((y - project->bounds.y) / config_.form_row_height);
        if (!(config_.form_row_height > 0.0f))
            return {};
        const auto project_rows =
            BuildProjectPanelProjection(session_->state().tabs, project_filter_);
        if (row >= project_rows.rows.size())
            return {};
        const auto& item = project_rows.rows[row];
        if (item.kind == ProjectPanelRowKind::kFilter) {
            project_filter_focused_ = true;
            project_filter_text_.SetText(project_filter_);
            project_filter_text_.SelectAll();
            return {true, EditorHostRequest::kNone};
        }
        if (item.kind != ProjectPanelRowKind::kDocument ||
            item.document_index >= session_->state().tabs.tabs.size())
            return {};
        return {SelectDocumentAndRefresh(
                    session_.get(), session_->state().tabs.tabs[item.document_index].document_id),
                EditorHostRequest::kNone};
    }
    if (const auto* tabs = FindPanel("workspace.tabs");
        tabs != nullptr && Contains(tabs->bounds, x, y) && !session_->state().tabs.tabs.empty()) {
        const auto width =
            tabs->bounds.width / static_cast<float>(session_->state().tabs.tabs.size());
        const auto index = static_cast<std::size_t>((x - tabs->bounds.x) / width);
        if (index < session_->state().tabs.tabs.size())
            return {SelectDocumentAndRefresh(session_.get(),
                                             session_->state().tabs.tabs[index].document_id),
                    EditorHostRequest::kNone};
    }
    if (const auto* hierarchy = FindPanel("workspace.hierarchy");
        hierarchy != nullptr && Contains(hierarchy->bounds, x, y) && session_->state().navigation) {
        const auto content_y = y - hierarchy->bounds.y - config_.form_row_height * 2.0f;
        if (content_y < 0.0f || !(config_.form_row_height > 0.0f))
            return {};
        const auto index = static_cast<std::size_t>(content_y / config_.form_row_height);
        return {session_->SelectNavigationCell(index), EditorHostRequest::kNone};
    }
    if (const auto* diagnostics = FindPanel("workspace.diagnostics");
        diagnostics != nullptr && Contains(diagnostics->bounds, x, y)) {
        if (!(config_.diagnostic_row_height > 0.0f))
            return {};
        const auto index =
            static_cast<std::size_t>((y - diagnostics->bounds.y) / config_.diagnostic_row_height);
        return {session_->LocateDiagnostic(index), EditorHostRequest::kNone};
    }
    if (const auto* scene = FindPanel("workspace.scene");
        scene != nullptr && Contains(scene->bounds, x, y) && session_->state().navigation) {
        auto navigation = *session_->state().navigation;
        LayoutNavigation(navigation, scene->bounds);
        const auto hit = HitNavigationCell(navigation, x, y);
        return {hit && session_->SelectNavigationCell(*hit), EditorHostRequest::kNone};
    }
    if (const auto* form = FindPanel("workspace.form");
        form != nullptr && Contains(form->bounds, x, y) && !session_->state().navigation &&
        config_.form_row_height > 0.0f) {
        const auto index = static_cast<std::size_t>((y - form->bounds.y) / config_.form_row_height);
        return {session_->SelectField(index), EditorHostRequest::kNone};
    }
    return {};
}

EditorInteractionResult EditorWorkspaceController::Dispatch(EditorAction action) {
    if (action == EditorAction::kOpen)
        return {false, EditorHostRequest::kOpenProjectDialog};
    if (session_ == nullptr)
        return {};
    switch (action) {
    case EditorAction::kOpen:
        break;
    case EditorAction::kSave:
        return {session_->Save(), EditorHostRequest::kNone};
    case EditorAction::kRefresh:
        return {session_->Refresh(), EditorHostRequest::kNone};
    case EditorAction::kSelectNext:
        return {session_->SelectNext(), EditorHostRequest::kNone};
    case EditorAction::kSelectPrevious:
        return {session_->SelectPrevious(), EditorHostRequest::kNone};
    case EditorAction::kConfirm:
        return {session_->ApplySelectedKey("Enter"), EditorHostRequest::kNone};
    case EditorAction::kCancel:
        return {session_->ApplySelectedKey("Escape"), EditorHostRequest::kNone};
    case EditorAction::kPreview:
        return {session_->StartPreview(config_.runtime_executable), EditorHostRequest::kNone};
    case EditorAction::kStopPreview:
        session_->StopPreview();
        return {true, EditorHostRequest::kNone};
    case EditorAction::kIncrement:
        return {session_->AdjustSelectedInteger(1), EditorHostRequest::kNone};
    case EditorAction::kDecrement:
        return {session_->AdjustSelectedInteger(-1), EditorHostRequest::kNone};
    case EditorAction::kToggle:
        return {session_->ToggleSelectedNavigationWalkable() || session_->ToggleSelectedBoolean(),
                EditorHostRequest::kNone};
    case EditorAction::kChoiceNext:
        return {session_->CycleSelectedChoice(1), EditorHostRequest::kNone};
    case EditorAction::kChoicePrevious:
        return {session_->CycleSelectedChoice(-1), EditorHostRequest::kNone};
    }
    return {};
}

bool EditorWorkspaceController::ApplyText(std::string_view text) {
    if (project_filter_focused_) {
        std::vector<ui::UiCommand> commands;
        const ui::UiEvent event{ui::UiEventType::kTextInput, 1, std::string(text)};
        if (!project_filter_text_.Apply(event, commands, 1))
            return false;
        project_filter_ = project_filter_text_.text();
        return true;
    }
    return session_ != nullptr && session_->ApplySelectedText(text);
}

bool EditorWorkspaceController::ApplyComposition(std::string_view text) {
    if (project_filter_focused_) {
        std::vector<ui::UiCommand> commands;
        const ui::UiEvent event{ui::UiEventType::kTextComposition, 1, std::string(text)};
        (void) project_filter_text_.Apply(event, commands, 1);
        return true;
    }
    return session_ != nullptr && session_->ApplySelectedComposition(text);
}

bool EditorWorkspaceController::ApplyTextKey(std::string_view key) {
    if (project_filter_focused_) {
        std::vector<ui::UiCommand> commands;
        const ui::UiEvent event{ui::UiEventType::kKeyDown, 1, std::string(key)};
        if (key == "Escape") {
            project_filter_focused_ = false;
            return true;
        }
        if (!project_filter_text_.Apply(event, commands, 1))
            return false;
        project_filter_ = project_filter_text_.text();
        return true;
    }
    return session_ != nullptr && session_->ApplySelectedKey(key);
}

bool EditorWorkspaceController::Poll() {
    return session_ != nullptr && session_->PollPreview();
}

ui::DrawList EditorWorkspaceController::BuildDrawList() const {
    ui::DrawList draw_list;
    if (!shell_)
        return draw_list;
    draw_list = BuildShellDrawList(*shell_);
    if (const auto* toolbar = FindPanel("workspace.toolbar"))
        Append(draw_list, BuildToolbarDrawList(
                              toolbar->bounds, session_ != nullptr && session_->state().open,
                              session_ != nullptr && session_->state().preview.process_running,
                              hovered_toolbar_, pressed_toolbar_, focused_toolbar_));
    const auto* status = FindPanel("workspace.status");
    if (session_ == nullptr) {
        if (status != nullptr)
            Append(draw_list, BuildStatusBarDrawList({}, status->bounds, status->recipe));
        return draw_list;
    }
    const auto& state = session_->state();
    if (const auto* project = FindPanel("workspace.project"))
        Append(draw_list, BuildProjectDrawList(state.tabs, project->bounds, config_.form_row_height,
                                               project_filter_));
    if (const auto* tabs = FindPanel("workspace.tabs"))
        Append(draw_list, BuildDocumentTabsDrawList(state.tabs, tabs->bounds));
    if (const auto* hierarchy = FindPanel("workspace.hierarchy");
        hierarchy != nullptr && state.navigation)
        Append(draw_list, BuildHierarchyDrawList(*state.navigation, hierarchy->bounds,
                                                 config_.form_row_height));
    if (const auto* form = FindPanel("workspace.form")) {
        if (state.navigation) {
            Append(draw_list, BuildNavigationInspectorDrawList(*state.navigation, form->bounds));
        } else {
            const TextFieldVisualProjection text_field{true,
                                                       state.text_selection_start,
                                                       state.text_selection_end,
                                                       state.text_caret,
                                                       "selection",
                                                       "caret"};
            Append(draw_list, BuildFormDrawList(state.form, form->bounds, config_.form_row_height,
                                                state.selected_field, &text_field));
        }
    }
    if (const auto* scene = FindPanel("workspace.scene"); scene != nullptr && state.navigation) {
        auto navigation = *state.navigation;
        LayoutNavigation(navigation, scene->bounds);
        Append(draw_list, BuildNavigationDrawList(navigation));
    }
    if (const auto* diagnostics = FindPanel("workspace.diagnostics"))
        Append(draw_list,
               BuildDiagnosticsDrawList(state.diagnostics, state.diff, state.preview,
                                        diagnostics->bounds, config_.diagnostic_row_height));
    if (status != nullptr)
        Append(draw_list, BuildStatusBarDrawList({state.open, state.dirty, state.revision,
                                                  PanelLabelKey(focused_panel_)},
                                                 status->bounds, status->recipe));
    if (FindPanel("workspace.menu") != nullptr)
        Append(draw_list, menu_.BuildDrawList("button", "input", "button"));
    return draw_list;
}

} // namespace jrpgmaker::editor
