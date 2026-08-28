#include "jrpgmaker/editor/editor_shell.hpp"

#include <utility>

namespace jrpgmaker::editor {

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

InputMap BuildInputMap(const ui::EditorActionMap& resource) {
    InputMap result;
    const std::unordered_map<std::string, EditorAction> actions = {
        {"open", EditorAction::kOpen},           {"save", EditorAction::kSave},
        {"refresh", EditorAction::kRefresh},     {"select_next", EditorAction::kSelectNext},
        {"select_previous", EditorAction::kSelectPrevious},
        {"confirm", EditorAction::kConfirm},     {"cancel", EditorAction::kCancel}};
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
    ShellProjection projection{.layout_id = layout.id};
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
            (void) draw_list.Add(ui::DrawText{node.bounds, node.label_key});
    }
    return draw_list;
}

} // namespace jrpgmaker::editor
