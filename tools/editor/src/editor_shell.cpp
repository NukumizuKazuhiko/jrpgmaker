#include "jrpgmaker/editor/editor_shell.hpp"

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
        {"open", EditorAction::kOpen},           {"save", EditorAction::kSave},
        {"refresh", EditorAction::kRefresh},     {"select_next", EditorAction::kSelectNext},
        {"select_previous", EditorAction::kSelectPrevious},
        {"confirm", EditorAction::kConfirm},     {"cancel", EditorAction::kCancel},
        {"preview", EditorAction::kPreview}};
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
    projection.nodes.push_back(
        ShellNode{source.id, source.type, source.label_key, source.recipe, source.bounds, {}});
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

ui::DrawList BuildShellDrawList(const ShellProjection& projection) {
    ui::DrawList draw_list;
    for (const auto& node : projection.nodes) {
        if (!node.recipe.empty())
            (void) draw_list.Add(ui::DrawRect{node.bounds, node.recipe});
        if (!node.label_key.empty())
            (void) draw_list.Add(ui::DrawText{node.bounds, node.label_key, {}});
    }
    return draw_list;
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
        const char* state = tab.active ? (tab.dirty ? "active_dirty" : "active")
                                       : (tab.dirty ? "dirty" : "normal");
        (void) draw_list.Add(ui::DrawRect{tab_bounds, "tab", state});
        (void) draw_list.Add(ui::DrawText{tab_bounds, tab.label_key, {}});
    }
    return draw_list;
}

ui::DrawList BuildFormDrawList(const FormProjection& projection, ui::Rect bounds,
                               float row_height, std::size_t selected_field) {
    ui::DrawList draw_list;
    if (!(row_height > 0.0f) || projection.fields.size() > ui::kMaxEditorLayoutNodes)
        return draw_list;
    for (std::size_t index = 0; index < projection.fields.size(); ++index) {
        const auto& field = projection.fields[index];
        const ui::Rect row{bounds.x, bounds.y + row_height * static_cast<float>(index), bounds.width,
                           row_height};
        const auto state = index == selected_field ? "focused" : "normal";
        const auto recipe = field.recipe.empty() ? "input" : field.recipe;
        (void) draw_list.Add(ui::DrawRect{row, recipe, state});
        if (!field.label_key.empty())
            (void) draw_list.Add(ui::DrawText{row, field.label_key, {}});
        if (!field.value.is_null()) {
            const std::string value = field.value.is_string()
                                          ? field.value.get<std::string>()
                                          : field.value.dump();
            (void) draw_list.Add(ui::DrawText{
                {row.x + row.width * 0.5f, row.y, row.width * 0.5f, row.height},
                "editor.value", {{"value", value}}});
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
            const ui::Rect row{bounds.x, bounds.y + row_height * static_cast<float>(index), bounds.width,
                               row_height};
            (void) draw_list.Add(ui::DrawRect{row, "input", "disabled"});
            if (!projection.diagnostics[index].code.empty())
                (void) draw_list.Add(ui::DrawText{
                    row, "editor.diagnostic.code", {{"code", projection.diagnostics[index].code}}});
        }
        return draw_list;
    }
    for (std::size_t index = 0; index < projection.metrics.size(); ++index) {
        const auto& metric = projection.metrics[index];
        const ui::Rect row{bounds.x, bounds.y + row_height * static_cast<float>(index), bounds.width,
                           row_height};
        (void) draw_list.Add(ui::DrawRect{row, "panel", "normal"});
        if (!metric.label_key.empty())
            (void) draw_list.Add(ui::DrawText{row, metric.label_key, {}});
        (void) draw_list.Add(ui::DrawText{{bounds.x + bounds.width * 0.5f, row.y,
                                           bounds.width * 0.5f, row.height},
                                          "editor.value", {{"value", metric.value.dump()}}});
    }
    if (projection.process_running || projection.process_exit_code != 0 ||
        !projection.process_error.empty()) {
        const auto row = ui::Rect{bounds.x, bounds.y + row_height *
                                             static_cast<float>(projection.metrics.size()),
                                  bounds.width, row_height};
        (void) draw_list.Add(ui::DrawRect{row, "input", "disabled"});
        const auto value = projection.process_running
                               ? "running"
                               : projection.process_error.empty()
                                     ? std::to_string(projection.process_exit_code)
                                     : projection.process_error;
        (void) draw_list.Add(ui::DrawText{row, "editor.preview.process", {{"value", value}}});
    }
    return draw_list;
}

} // namespace jrpgmaker::editor
