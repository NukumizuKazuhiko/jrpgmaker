#include "jrpgmaker/ui/ui_tree.hpp"

#include <algorithm>
#include <utility>

namespace jrpgmaker::ui {

bool UiTree::Validate(const UiNode& node, std::size_t& count, std::vector<std::uint64_t>& ids) {
    if (node.id == 0 || ++count > kMaxNodes ||
        std::find(ids.begin(), ids.end(), node.id) != ids.end())
        return false;
    ids.push_back(node.id);
    for (const auto& child : node.children)
        if (!Validate(child, count, ids))
            return false;
    return true;
}

bool UiTree::SetRoot(UiNode root) {
    std::size_t count = 0;
    std::vector<std::uint64_t> ids;
    if (!Validate(root, count, ids))
        return false;
    root_ = std::move(root);
    has_root_ = true;
    layout_specs_.clear();
    const auto remember_layout_spec = [this](const UiNode& node, const auto& self) -> void {
        layout_specs_.emplace(node.id, node.bounds);
        for (const auto& child : node.children)
            self(child, self);
    };
    remember_layout_spec(root_, remember_layout_spec);
    focus_.ClearFocusables();
    std::size_t tab_index = 0;
    const auto register_focus = [this, &tab_index](const UiNode& node, const auto& self) -> void {
        if (node.visible && node.enabled)
            (void) focus_.RegisterFocusable(node.id, tab_index++);
        for (const auto& child : node.children)
            self(child, self);
    };
    register_focus(root_, register_focus);
    return true;
}

void UiTree::LayoutNode(UiNode& node, Rect available) {
    const auto spec = layout_specs_.find(node.id);
    const Rect layout_spec = spec != layout_specs_.end() ? spec->second : Rect{};
    node.bounds = {available.x + layout_spec.x, available.y + layout_spec.y,
                   std::max(0.0f, layout_spec.width > 0 ? layout_spec.width : available.width),
                   std::max(0.0f, layout_spec.height > 0 ? layout_spec.height : available.height)};
    if (node.layout != UiLayoutMode::kVertical)
        return;
    float cursor = node.bounds.y;
    for (auto& child : node.children) {
        if (!child.visible)
            continue;
        const float h = child.preferred_height > 0.0f ? child.preferred_height : node.bounds.height;
        LayoutNode(child, {node.bounds.x, cursor, node.bounds.width, h});
        cursor += h;
    }
}

bool UiTree::Layout(Rect viewport) {
    if (!has_root_ || viewport.width <= 0.0f || viewport.height <= 0.0f)
        return false;
    root_.bounds = {};
    LayoutNode(root_, viewport);
    return true;
}

const UiNode* UiTree::FindNode(const UiNode& node, std::uint64_t id) {
    if (node.id == id)
        return &node;
    for (const auto& child : node.children)
        if (const auto* found = FindNode(child, id))
            return found;
    return nullptr;
}

const UiNode* UiTree::Find(std::uint64_t id) const {
    return has_root_ ? FindNode(root_, id) : nullptr;
}

const UiNode* UiTree::HitNode(const UiNode& node, float x, float y) {
    if (!node.visible || x < node.bounds.x || y < node.bounds.y ||
        x >= node.bounds.x + node.bounds.width || y >= node.bounds.y + node.bounds.height)
        return nullptr;
    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it)
        if (const auto* found = HitNode(*it, x, y))
            return found;
    return &node;
}

std::vector<UiCommand> UiTree::Dispatch(const UiEvent& event) {
    std::vector<UiCommand> commands;
    if (!has_root_)
        return commands;
    const auto* target =
        event.widget_id != 0 ? Find(event.widget_id) : HitNode(root_, event.x, event.y);
    if (target == nullptr || !target->enabled || !target->visible)
        return commands;
    if (event.type == UiEventType::kPointerDown) {
        (void) focus_.SetFocus(target->id);
        commands.push_back({UiCommandType::kActivate, target->id, {}});
    }
    return commands;
}

void UiTree::DrawNode(const UiNode& node, DrawList& draw_list) {
    if (!node.visible)
        return;
    if (!node.recipe.empty())
        (void) draw_list.Add(
            DrawRect{node.bounds, node.recipe, node.enabled ? "normal" : "disabled"});
    if (!node.label_key.empty())
        (void) draw_list.Add(DrawText{node.bounds, node.label_key, {}, std::nullopt});
    for (const auto& child : node.children)
        DrawNode(child, draw_list);
}

DrawList UiTree::BuildDrawList() const {
    DrawList result;
    if (has_root_)
        DrawNode(root_, result);
    return result;
}

} // namespace jrpgmaker::ui
