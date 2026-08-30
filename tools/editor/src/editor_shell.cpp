#include "jrpgmaker/editor/editor_shell.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace jrpgmaker::editor {

namespace {

std::string BindingName(std::string_view key_name, bool control, bool shift, bool alt) {
    std::string result;
    if (control)
        result += "Ctrl+";
    if (shift)
        result += "Shift+";
    if (alt)
        result += "Alt+";
    result += key_name;
    return result;
}

} // namespace

bool InputMap::Add(KeyBinding binding) {
    if (binding.key_name.empty() || bindings_.size() >= kMaxBindings ||
        bindings_.contains(binding.key_name))
        return false;
    bindings_.emplace(std::move(binding.key_name), binding.action);
    return true;
}

std::optional<EditorAction> InputMap::Translate(std::string_view key_name, bool pressed) const {
    if (!pressed)
        return std::nullopt;
    const auto it = bindings_.find(std::string(key_name));
    return it == bindings_.end() ? std::nullopt : std::optional<EditorAction>(it->second);
}

std::optional<EditorAction> InputMap::Translate(std::string_view key_name, bool pressed,
                                                bool control, bool shift, bool alt) const {
    return Translate(BindingName(key_name, control, shift, alt), pressed);
}

InputMap BuildInputMap(const ui::EditorActionMap& resource) {
    InputMap result;
    const std::unordered_map<std::string, EditorAction> actions = {
        {"open", EditorAction::kOpen},
        {"save", EditorAction::kSave},
        {"refresh", EditorAction::kRefresh},
        {"select_next", EditorAction::kSelectNext},
        {"select_previous", EditorAction::kSelectPrevious},
        {"confirm", EditorAction::kConfirm},
        {"cancel", EditorAction::kCancel},
        {"preview", EditorAction::kPreview},
        {"stop_preview", EditorAction::kStopPreview},
        {"increment", EditorAction::kIncrement},
        {"decrement", EditorAction::kDecrement},
        {"toggle", EditorAction::kToggle},
        {"choice_next", EditorAction::kChoiceNext},
        {"choice_previous", EditorAction::kChoicePrevious}};
    for (const auto& [id, keys] : resource.actions) {
        const auto action = actions.find(id);
        if (action == actions.end())
            continue;
        for (const auto& key : keys)
            (void) result.Add({key, action->second});
    }
    return result;
}

namespace {

std::optional<std::size_t> Flatten(const ui::EditorLayoutNode& source,
                                   ShellProjection& projection) {
    if (source.id.empty() || projection.nodes.size() >= ui::kMaxEditorLayoutNodes)
        return std::nullopt;
    const auto index = projection.nodes.size();
    projection.nodes.push_back(ShellNode{source.id,
                                         source.type,
                                         source.label_key,
                                         source.command,
                                         source.recipe,
                                         source.bounds,
                                         {}});
    for (const auto& child : source.children) {
        const auto child_index = Flatten(child, projection);
        if (!child_index)
            return std::nullopt;
        projection.nodes[index].children.push_back(*child_index);
    }
    return index;
}

} // namespace

std::optional<ShellProjection> BuildShellProjection(const ui::EditorLayout& layout) {
    if (layout.id.empty())
        return std::nullopt;
    ShellProjection projection{.layout_id = layout.id, .nodes = {}};
    if (!Flatten(layout.root, projection))
        return std::nullopt;
    return projection;
}

void ReflowShellProjection(ShellProjection& projection, float width, float height) {
    if (!(width > 0.0f) || !(height > 0.0f))
        return;
    const float left = std::clamp(width * 0.22f, 220.0f, 360.0f);
    const float status = 26.0f;
    const float diagnostics = std::clamp(height * 0.18f, 96.0f, 170.0f);
    const auto set = [&projection](std::string_view id, ui::Rect bounds) {
        for (auto& node : projection.nodes)
            if (node.id == id) {
                node.bounds = bounds;
                return;
            }
    };
    set("workspace.root", {0, 0, width, height});
    constexpr float menu = 28.0f;
    constexpr float toolbar = 34.0f;
    const float top = menu + toolbar;
    set("workspace.menu", {0, 0, width, menu});
    set("workspace.toolbar", {0, menu, width, toolbar});
    set("workspace.tree", {0, top, left, height - top});
    const float tree_height = height - top;
    const float project_height = std::clamp(tree_height * 0.52f, 150.0f, tree_height - 120.0f);
    set("workspace.project", {0, top, left, project_height});
    set("workspace.hierarchy", {0, top + project_height, left, tree_height - project_height});
    set("workspace.content", {left, top, width - left, height - top});
    constexpr float tab_height = 34.0f;
    set("workspace.tabs", {left, top, width - left, tab_height});
    const float content_top = top + 34.0f;
    const float content_height = height - top - diagnostics - status - 48.0f;
    set("workspace.scene", {left + 16, content_top, (width - left - 48) * 0.68f, content_height});
    const float scene_width = (width - left - 48) * 0.68f;
    set("workspace.form",
        {left + 32 + scene_width, content_top, width - left - scene_width - 48, content_height});
    set("workspace.diagnostics", {left, height - diagnostics - status, width - left, diagnostics});
    set("workspace.status", {left, height - status, width - left, status});
}

std::optional<std::size_t> HitNavigationCell(const NavigationProjection& projection, float x,
                                             float y) {
    for (std::size_t i = 0; i < projection.cells.size(); ++i) {
        const auto& cell = projection.cells[i];
        if (x >= cell.bounds.x && x < cell.bounds.x + cell.bounds.width && y >= cell.bounds.y &&
            y < cell.bounds.y + cell.bounds.height)
            return i;
    }
    return std::nullopt;
}

void LayoutNavigation(NavigationProjection& projection, ui::Rect bounds) {
    if (projection.width <= 0 || projection.height <= 0)
        return;
    const float cell = std::min(bounds.width / static_cast<float>(projection.width),
                                bounds.height / static_cast<float>(projection.height));
    const float ox = bounds.x + (bounds.width - cell * projection.width) * 0.5f;
    const float oy = bounds.y + (bounds.height - cell * projection.height) * 0.5f;
    for (auto& item : projection.cells)
        item.bounds = {ox + cell * item.x, oy + cell * item.y, cell - 1.0f, cell - 1.0f};
}

ui::DrawList BuildNavigationDrawList(const NavigationProjection& projection) {
    ui::DrawList list;
    for (const auto& cell : projection.cells) {
        (void) list.Add(ui::DrawRect{cell.bounds, cell.selected ? "selection" : "input",
                                     cell.walkable ? "normal" : "disabled"});
    }
    return list;
}

ui::DrawList BuildNavigationInspectorDrawList(const NavigationProjection& projection,
                                              ui::Rect bounds) {
    ui::DrawList list;
    if (!projection.selected || *projection.selected >= projection.cells.size())
        return list;
    const auto& cell = projection.cells[*projection.selected];
    const auto row = bounds.height / 3.0f;
    (void) list.Add(ui::DrawText{{bounds.x, bounds.y, bounds.width, row},
                                 "editor.navigation.selection",
                                 {{"x", std::to_string(cell.x)}, {"y", std::to_string(cell.y)}},
                                 std::nullopt});
    (void) list.Add(ui::DrawText{{bounds.x, bounds.y + row, bounds.width, row},
                                 "editor.navigation.walkable_state",
                                 {{"value", cell.walkable ? "true" : "false"}},
                                 std::nullopt});
    (void) list.Add(ui::DrawText{{bounds.x, bounds.y + row * 2.0f, bounds.width, row},
                                 "editor.navigation.toggle_hint",
                                 {},
                                 std::nullopt});
    return list;
}

ui::DrawList BuildShellDrawList(const ShellProjection& projection) {
    ui::DrawList draw_list;
    for (const auto& node : projection.nodes) {
        if (node.type == "MenuBar" || node.type == "Menu" || node.type == "MenuItem")
            continue;
        if (!node.recipe.empty())
            (void) draw_list.Add(ui::DrawRect{node.bounds, node.recipe});
        if (!node.label_key.empty())
            (void) draw_list.Add(ui::DrawText{node.bounds, node.label_key, {}, std::nullopt});
    }
    return draw_list;
}

ui::DrawList BuildStatusBarDrawList(const StatusBarProjection& projection, ui::Rect bounds,
                                    std::string_view recipe) {
    ui::DrawList draw_list;
    if (bounds.width <= 0.0f || bounds.height <= 0.0f || recipe.empty())
        return draw_list;
    (void) draw_list.Add(ui::DrawRect{bounds, std::string(recipe), "normal"});
    const auto key = !projection.open   ? "editor.status.closed"
                     : projection.dirty ? "editor.status.dirty"
                                        : "editor.status.clean";
    const auto status_bounds =
        projection.focused_panel_label_key.empty()
            ? bounds
            : ui::Rect{bounds.x, bounds.y, bounds.width * 0.55f, bounds.height};
    (void) draw_list.Add(ui::DrawText{
        status_bounds, key, {{"revision", std::to_string(projection.revision)}}, std::nullopt});
    if (!projection.focused_panel_label_key.empty()) {
        (void) draw_list.Add(ui::DrawText{
            {bounds.x + bounds.width * 0.55f, bounds.y, bounds.width * 0.2f, bounds.height},
            "editor.status.focused",
            {},
            std::nullopt});
        (void) draw_list.Add(ui::DrawText{
            {bounds.x + bounds.width * 0.75f, bounds.y, bounds.width * 0.25f, bounds.height},
            projection.focused_panel_label_key,
            {},
            std::nullopt});
    }
    return draw_list;
}

ui::DrawList BuildToolbarDrawList(ui::Rect bounds, bool project_open, bool preview_running,
                                  std::optional<EditorAction> hovered,
                                  std::optional<EditorAction> pressed,
                                  std::optional<EditorAction> focused) {
    const auto projection =
        BuildToolbarProjection(bounds, project_open, preview_running, hovered, pressed, focused);
    ui::DrawList draw_list;
    for (const auto& button : projection.buttons) {
        const auto state = !button.enabled  ? "disabled"
                           : button.pressed ? "pressed"
                           : button.focused ? "focused"
                           : button.hovered ? "hover"
                                            : "normal";
        (void) draw_list.Add(ui::DrawRect{button.bounds, "button", state});
        (void) draw_list.Add(ui::DrawText{button.bounds, button.label_key, {}, std::nullopt});
    }
    return draw_list;
}

ToolbarProjection BuildToolbarProjection(ui::Rect bounds, bool project_open, bool preview_running,
                                         std::optional<EditorAction> hovered,
                                         std::optional<EditorAction> pressed,
                                         std::optional<EditorAction> focused) {
    ToolbarProjection projection;
    if (!(bounds.width > 0.0f) || !(bounds.height > 0.0f))
        return projection;
    constexpr std::array<std::string_view, 4> labels = {"editor.action.open", "editor.action.save",
                                                        "editor.action.run_preview",
                                                        "editor.action.stop_preview"};
    constexpr std::array actions = {EditorAction::kOpen, EditorAction::kSave,
                                    EditorAction::kPreview, EditorAction::kStopPreview};
    const float width = bounds.width / static_cast<float>(labels.size());
    for (std::size_t index = 0; index < labels.size(); ++index) {
        const bool enabled =
            index == 0 || (project_open && (index == 1 || (index == 2 && !preview_running) ||
                                            (index == 3 && preview_running)));
        projection.buttons.push_back(
            {actions[index],
             std::string(labels[index]),
             {bounds.x + width * static_cast<float>(index), bounds.y, width, bounds.height},
             enabled,
             hovered && *hovered == actions[index],
             pressed && *pressed == actions[index],
             focused && *focused == actions[index]});
    }
    return projection;
}

std::optional<EditorAction> HitToolbar(const ToolbarProjection& projection, float x, float y) {
    for (const auto& button : projection.buttons)
        if (button.enabled && x >= button.bounds.x && x < button.bounds.x + button.bounds.width &&
            y >= button.bounds.y && y < button.bounds.y + button.bounds.height)
            return button.action;
    return std::nullopt;
}

ui::DrawList BuildDocumentTabsDrawList(const DocumentTabsProjection& projection, ui::Rect bounds) {
    ui::DrawList draw_list;
    if (projection.tabs.empty() || bounds.width <= 0.0f || bounds.height <= 0.0f)
        return draw_list;
    const float tab_width = bounds.width / static_cast<float>(projection.tabs.size());
    for (std::size_t index = 0; index < projection.tabs.size(); ++index) {
        const auto& tab = projection.tabs[index];
        const ui::Rect tab_bounds{bounds.x + tab_width * static_cast<float>(index), bounds.y,
                                  tab_width, bounds.height};
        const char* state =
            tab.active ? (tab.dirty ? "pressed" : "focused") : (tab.dirty ? "hover" : "normal");
        (void) draw_list.Add(ui::DrawRect{tab_bounds, "tab", state});
        (void) draw_list.Add(ui::DrawText{tab_bounds, tab.label_key, {}, std::nullopt});
    }
    return draw_list;
}

ui::DrawList BuildProjectDrawList(const DocumentTabsProjection& projection, ui::Rect bounds,
                                  float row_height, std::string_view filter) {
    ui::DrawList draw_list;
    if (!(row_height > 0.0f) || bounds.width <= 0.0f || bounds.height <= 0.0f)
        return draw_list;
    const auto panel = BuildProjectPanelProjection(projection, filter);
    const auto max_rows = static_cast<std::size_t>(std::floor(bounds.height / row_height));
    const auto count = std::min(panel.rows.size(), max_rows);
    for (std::size_t index = 0; index < count; ++index) {
        const auto row = ui::Rect{bounds.x, bounds.y + row_height * static_cast<float>(index),
                                  bounds.width, row_height};
        const auto& item = panel.rows[index];
        if (item.kind == ProjectPanelRowKind::kFilter) {
            (void) draw_list.Add(ui::DrawRect{row, "input", "focused"});
            (void) draw_list.Add(ui::DrawText{
                row, "editor.project.search", {{"value", panel.filter}}, std::nullopt});
        } else if (item.kind == ProjectPanelRowKind::kCategory) {
            (void) draw_list.Add(ui::DrawRect{row, "panel", "normal"});
            (void) draw_list.Add(ui::DrawText{row, item.category_key, {}, std::nullopt});
        } else if (item.document_index < projection.tabs.size()) {
            const auto& document = projection.tabs[item.document_index];
            const auto state = document.active ? "focused" : (document.dirty ? "hover" : "normal");
            (void) draw_list.Add(ui::DrawRect{row, "input", state});
            (void) draw_list.Add(ui::DrawText{row, document.label_key, {}, std::nullopt});
            (void) draw_list.Add(
                ui::DrawText{{row.x + row.width * 0.42f, row.y, row.width * 0.33f, row.height},
                             "editor.value",
                             {{"value", document.path}},
                             std::nullopt});
            (void) draw_list.Add(
                ui::DrawText{{row.x + row.width * 0.75f, row.y, row.width * 0.25f, row.height},
                             "editor.project.resource_meta",
                             {{"type", document.type_id},
                              {"diagnostics", std::to_string(document.diagnostic_count)}},
                             std::nullopt});
        }
    }
    return draw_list;
}

ui::DrawList BuildHierarchyDrawList(const NavigationProjection& projection, ui::Rect bounds,
                                    float row_height) {
    ui::DrawList draw_list;
    if (!(row_height > 0.0f))
        return draw_list;
    const auto max_rows = static_cast<std::size_t>(
        std::max(0.0f, std::floor((bounds.height - row_height) / row_height)));
    if (max_rows == 0)
        return draw_list;
    const ui::Rect map_row{bounds.x, bounds.y + row_height, bounds.width, row_height};
    (void) draw_list.Add(ui::DrawRect{map_row, "input", "focused"});
    (void) draw_list.Add(ui::DrawText{map_row,
                                      "editor.hierarchy.navigation",
                                      {{"width", std::to_string(projection.width)},
                                       {"height", std::to_string(projection.height)}},
                                      std::nullopt});
    const auto cell_rows = max_rows - 1;
    const auto count = std::min(projection.cells.size(), cell_rows);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& cell = projection.cells[index];
        const ui::Rect row{bounds.x, bounds.y + row_height * static_cast<float>(index + 2),
                           bounds.width, row_height};
        (void) draw_list.Add(ui::DrawRect{row, "input", cell.selected ? "focused" : "normal"});
        (void) draw_list.Add(ui::DrawText{row,
                                          "editor.hierarchy.cell",
                                          {{"x", std::to_string(cell.x)},
                                           {"y", std::to_string(cell.y)},
                                           {"walkable", cell.walkable ? "true" : "false"}},
                                          std::nullopt});
    }
    return draw_list;
}

ui::DrawList BuildDiagnosticsDrawList(const std::vector<project::Diagnostic>& diagnostics,
                                      const DiffProjection& diff, const PreviewProjection& preview,
                                      ui::Rect bounds, float row_height) {
    ui::DrawList draw_list;
    if (!(row_height > 0.0f))
        return draw_list;
    const auto max_rows =
        static_cast<std::size_t>(std::max(0.0f, std::floor(bounds.height / row_height)));
    std::size_t row_index = 0;
    const auto add = [&draw_list, &bounds, row_height, max_rows, &row_index](
                         std::string key, std::unordered_map<std::string, std::string> arguments,
                         std::string state) {
        if (row_index >= max_rows)
            return;
        const ui::Rect row{bounds.x, bounds.y + row_height * static_cast<float>(row_index),
                           bounds.width, row_height};
        (void) draw_list.Add(ui::DrawRect{row, "input", std::move(state)});
        (void) draw_list.Add(ui::DrawText{row, std::move(key), std::move(arguments), std::nullopt});
        ++row_index;
    };
    for (const auto& diagnostic : diagnostics)
        add("editor.diagnostic.code", {{"code", diagnostic.code}, {"path", diagnostic.path}},
            "disabled");
    for (const auto& change : diff.changes)
        add("editor.diff.change",
            {{"document", change.document_id},
             {"path", change.field_path},
             {"before", change.before.dump()},
             {"after", change.after.dump()}},
            "hover");
    if (preview.process_running || preview.process_exit_code != 0 || !preview.process_error.empty())
        add("editor.preview.process",
            {{"value", preview.process_running         ? "running"
                       : preview.process_error.empty() ? std::to_string(preview.process_exit_code)
                                                       : preview.process_error}},
            "normal");
    return draw_list;
}

ui::DrawList BuildFormDrawList(const FormProjection& projection, ui::Rect bounds, float row_height,
                               std::size_t selected_field,
                               const TextFieldVisualProjection* text_field) {
    ui::DrawList draw_list;
    if (!(row_height > 0.0f) || projection.fields.size() > ui::kMaxEditorLayoutNodes)
        return draw_list;
    for (std::size_t index = 0; index < projection.fields.size(); ++index) {
        const auto& field = projection.fields[index];
        const ui::Rect row{bounds.x, bounds.y + row_height * static_cast<float>(index),
                           bounds.width, row_height};
        const auto state = index == selected_field ? "focused" : "normal";
        // Field roles (text, number, resource, ...) are adapter metadata, not
        // theme recipe names.  Keep the form renderer on the one shared input
        // recipe so every legal project can render with the committed theme.
        (void) draw_list.Add(ui::DrawRect{row, "input", state});
        if (!field.label_key.empty())
            (void) draw_list.Add(ui::DrawText{row, field.label_key, {}, std::nullopt});
        if (!field.value.is_null()) {
            const std::string value =
                field.value.is_string() ? field.value.get<std::string>() : field.value.dump();
            ui::DrawText value_text{{row.x + row.width * 0.5f, row.y, row.width * 0.5f, row.height},
                                    "editor.value",
                                    {{"value", value}},
                                    std::nullopt};
            if (text_field != nullptr && text_field->active && index == selected_field &&
                (field.value_type == "string" || field.value_type == "integer"))
                value_text.edit = ui::DrawText::EditDecoration{
                    text_field->selection_start,  text_field->selection_end, text_field->caret,
                    text_field->selection_recipe, text_field->caret_recipe,  true};
            (void) draw_list.Add(std::move(value_text));
        }
    }
    return draw_list;
}

ui::DrawList BuildPreviewDrawList(const PreviewProjection& projection, ui::Rect bounds,
                                  float row_height) {
    ui::DrawList draw_list;
    if (!(row_height > 0.0f))
        return draw_list;
    if (!projection.valid) {
        for (std::size_t index = 0; index < projection.diagnostics.size(); ++index) {
            const ui::Rect row{bounds.x, bounds.y + row_height * static_cast<float>(index),
                               bounds.width, row_height};
            (void) draw_list.Add(ui::DrawRect{row, "input", "disabled"});
            if (!projection.diagnostics[index].code.empty())
                (void) draw_list.Add(ui::DrawText{row,
                                                  "editor.diagnostic.code",
                                                  {{"code", projection.diagnostics[index].code},
                                                   {"path", projection.diagnostics[index].path}},
                                                  std::nullopt});
        }
        return draw_list;
    }
    for (std::size_t index = 0; index < projection.metrics.size(); ++index) {
        const auto& metric = projection.metrics[index];
        const ui::Rect row{bounds.x, bounds.y + row_height * static_cast<float>(index),
                           bounds.width, row_height};
        (void) draw_list.Add(ui::DrawRect{row, "panel", "normal"});
        if (!metric.label_key.empty())
            (void) draw_list.Add(ui::DrawText{row, metric.label_key, {}, std::nullopt});
        (void) draw_list.Add(
            ui::DrawText{{bounds.x + bounds.width * 0.5f, row.y, bounds.width * 0.5f, row.height},
                         "editor.value",
                         {{"value", metric.value.dump()}},
                         std::nullopt});
    }
    if (projection.process_running || projection.process_exit_code != 0 ||
        !projection.process_error.empty() || !projection.standard_output.empty() ||
        !projection.standard_error.empty()) {
        const auto row = ui::Rect{
            bounds.x, bounds.y + row_height * static_cast<float>(projection.metrics.size()),
            bounds.width, row_height};
        (void) draw_list.Add(ui::DrawRect{row, "input", "disabled"});
        const auto value = projection.process_running ? "running"
                           : projection.process_error.empty()
                               ? std::to_string(projection.process_exit_code)
                               : projection.process_error;
        (void) draw_list.Add(
            ui::DrawText{row, "editor.preview.process", {{"value", value}}, std::nullopt});
    }
    const auto add_log = [&draw_list, &bounds, row_height, &projection](std::string_view key,
                                                                        const std::string& value,
                                                                        std::size_t row_index) {
        if (value.empty())
            return;
        constexpr std::size_t kMaxPreviewLogDisplayBytes = 4096;
        const auto bounded = value.substr(0, kMaxPreviewLogDisplayBytes);
        const ui::Rect row{bounds.x, bounds.y + row_height * static_cast<float>(row_index),
                           bounds.width, row_height};
        (void) draw_list.Add(ui::DrawRect{row, "input", "disabled"});
        (void) draw_list.Add(
            ui::DrawText{row, std::string(key), {{"value", bounded}}, std::nullopt});
    };
    std::size_t log_row = projection.metrics.size() + 1;
    add_log("editor.preview.stdout", projection.standard_output, log_row++);
    add_log("editor.preview.stderr", projection.standard_error, log_row);
    return draw_list;
}

} // namespace jrpgmaker::editor
