#include "jrpgmaker/ui/menu.hpp"

#include <algorithm>
#include <utility>

namespace jrpgmaker::ui {
namespace {

bool SamePath(const std::vector<std::size_t>& left, const std::vector<std::size_t>& right) {
    return left == right;
}

} // namespace

bool MenuController::Validate(const MenuItem& item, std::size_t& count) const {
    if (++count > MenuBarModel::kMaxItems || item.id == 0 || item.label_key.empty())
        return false;
    if (!item.children.empty() && !item.command.empty())
        return false;
    for (const auto& child : item.children)
        if (!Validate(child, count))
            return false;
    return true;
}

bool MenuController::SetModel(MenuBarModel model) {
    std::size_t count = 0;
    for (const auto& root : model.roots)
        if (!Validate(root, count))
            return false;
    model_ = std::move(model);
    Close();
    bounds_.clear();
    return true;
}

bool MenuController::Layout(Rect bounds, MenuLayoutConfig config) {
    if (!(bounds.width > 0.0f) || !(bounds.height > 0.0f) || !(config.root_width > 0.0f) ||
        !(config.row_height > 0.0f) || !(config.popup_width > 0.0f))
        return false;
    bar_bounds_ = bounds;
    config_ = config;
    RebuildBounds();
    return true;
}

void MenuController::BuildBounds(const std::vector<MenuItem>& items,
                                 const std::vector<std::size_t>& parent, Rect bounds) {
    for (std::size_t index = 0; index < items.size(); ++index) {
        auto path = parent;
        path.push_back(index);
        bounds_.push_back({path,
                           {bounds.x, bounds.y + config_.row_height * index, config_.popup_width,
                            config_.row_height}});
    }
}

void MenuController::RebuildBounds() {
    bounds_.clear();
    if (model_.roots.empty())
        return;
    const auto width =
        std::min(config_.root_width, bar_bounds_.width / static_cast<float>(model_.roots.size()));
    for (std::size_t index = 0; index < model_.roots.size(); ++index)
        bounds_.push_back(
            {{index}, {bar_bounds_.x + width * index, bar_bounds_.y, width, bar_bounds_.height}});
    for (std::size_t depth = 1; depth <= open_path_.size(); ++depth) {
        const std::vector<std::size_t> parent_path(open_path_.begin(), open_path_.begin() + depth);
        const auto* parent = ItemAt(parent_path);
        if (parent == nullptr || parent->children.empty())
            return;
        const auto parent_bounds =
            std::find_if(bounds_.begin(), bounds_.end(),
                         [&parent_path](const auto& value) { return value.path == parent_path; });
        if (parent_bounds == bounds_.end())
            return;
        const auto popup =
            depth == 1
                ? Rect{parent_bounds->bounds.x,
                       parent_bounds->bounds.y + parent_bounds->bounds.height, config_.popup_width,
                       config_.row_height * static_cast<float>(parent->children.size())}
                : Rect{parent_bounds->bounds.x + parent_bounds->bounds.width,
                       parent_bounds->bounds.y, config_.popup_width,
                       config_.row_height * static_cast<float>(parent->children.size())};
        BuildBounds(parent->children, parent_path, popup);
    }
}

const MenuItem* MenuController::ItemAt(const std::vector<std::size_t>& path) const {
    const std::vector<MenuItem>* items = &model_.roots;
    const MenuItem* item = nullptr;
    for (const auto index : path) {
        if (index >= items->size())
            return nullptr;
        item = &(*items)[index];
        items = &item->children;
    }
    return item;
}

MenuController::ItemBounds* MenuController::Hit(float x, float y) {
    for (auto& item : bounds_)
        if (x >= item.bounds.x && x < item.bounds.x + item.bounds.width && y >= item.bounds.y &&
            y < item.bounds.y + item.bounds.height)
            return &item;
    return nullptr;
}

const MenuController::ItemBounds* MenuController::Hit(float x, float y) const {
    for (const auto& item : bounds_)
        if (x >= item.bounds.x && x < item.bounds.x + item.bounds.width && y >= item.bounds.y &&
            y < item.bounds.y + item.bounds.height)
            return &item;
    return nullptr;
}

MenuInteractionResult MenuController::ActivatePath(const std::vector<std::size_t>& path) {
    MenuInteractionResult result{true, {}};
    const auto* item = ItemAt(path);
    if (item == nullptr)
        return {};
    if (!item->enabled)
        return result;
    if (!item->children.empty()) {
        open_path_ = path;
        hover_path_.clear();
        RebuildBounds();
        return result;
    }
    if (!item->command.empty())
        result.commands.push_back({UiCommandType::kActivate, item->id, item->command});
    Close();
    return result;
}

MenuInteractionResult MenuController::PointerMove(float x, float y) {
    const auto* hit = Hit(x, y);
    if (hit == nullptr)
        return {};
    if (open_path_.empty())
        return {};
    const auto* item = ItemAt(hit->path);
    if (item == nullptr)
        return {};
    const bool changed = !SamePath(hover_path_, hit->path);
    hover_path_ = hit->path;
    if (!item->children.empty()) {
        open_path_ = hit->path;
        RebuildBounds();
    }
    return {changed, {}};
}

MenuInteractionResult MenuController::PointerDown(float x, float y) {
    if (const auto* hit = Hit(x, y))
        return ActivatePath(hit->path);
    if (x >= bar_bounds_.x && x < bar_bounds_.x + bar_bounds_.width && y >= bar_bounds_.y &&
        y < bar_bounds_.y + bar_bounds_.height) {
        const auto* hit_bar = Hit(x, y);
        if (hit_bar != nullptr)
            return ActivatePath(hit_bar->path);
    }
    Close();
    return {true, {}};
}

MenuInteractionResult MenuController::KeyDown(std::string_view key) {
    if (key == "Escape") {
        const bool was_open = open();
        Close();
        return {was_open, {}};
    }
    if (!open()) {
        if (key != "Alt")
            return {};
        if (model_.roots.empty())
            return {};
        open_path_ = {0};
        RebuildBounds();
        return {true, {}};
    }
    if (open_path_.size() == 1 && (key == "Down" || key == "Up")) {
        const auto* root = ItemAt(open_path_);
        if (root == nullptr || root->children.empty())
            return {};
        const auto child = key == "Down" ? 0u : root->children.size() - 1;
        open_path_.push_back(child);
        RebuildBounds();
        return {true, {}};
    }
    const auto parent_path =
        open_path_.size() > 1 ? std::vector<std::size_t>(open_path_.begin(), open_path_.end() - 1)
                              : std::vector<std::size_t>{};
    const auto* parent = parent_path.empty() ? nullptr : ItemAt(parent_path);
    const auto count = parent_path.empty() ? model_.roots.size() : parent->children.size();
    if (count == 0)
        return {};
    const auto current = open_path_.back();
    if (key == "Down" || key == "Up") {
        const auto next = key == "Down" ? (current + 1) % count : (current + count - 1) % count;
        open_path_.back() = next;
        RebuildBounds();
        return {true, {}};
    }
    const auto* item = ItemAt(open_path_);
    if (key == "Right" && item != nullptr && !item->children.empty()) {
        open_path_.push_back(0);
        RebuildBounds();
        return {true, {}};
    }
    if (key == "Left" && open_path_.size() > 1) {
        open_path_.pop_back();
        RebuildBounds();
        return {true, {}};
    }
    if (key == "Left" && open_path_.size() == 1) {
        Close();
        return {true, {}};
    }
    if (key == "Enter")
        return ActivatePath(open_path_);
    return {};
}

void MenuController::Close() {
    open_path_.clear();
    hover_path_.clear();
}

void MenuController::DrawItems(const std::vector<MenuItem>& items,
                               const std::vector<std::size_t>& parent, DrawList& list,
                               std::string_view recipe) const {
    for (std::size_t index = 0; index < items.size(); ++index) {
        auto path = parent;
        path.push_back(index);
        const auto bounds = std::find_if(bounds_.begin(), bounds_.end(),
                                         [&path](const auto& value) { return value.path == path; });
        if (bounds == bounds_.end())
            continue;
        const auto* item = &items[index];
        const bool focused = SamePath(path, hover_path_) || SamePath(path, open_path_);
        const auto state = !item->enabled ? "disabled" : focused ? "hover" : "normal";
        (void) list.Add(DrawRect{bounds->bounds, std::string(recipe), state});
        (void) list.Add(DrawText{bounds->bounds, item->label_key, {}, std::nullopt});
    }
}

DrawList MenuController::BuildDrawList(std::string_view bar_recipe, std::string_view popup_recipe,
                                       std::string_view item_recipe) const {
    DrawList list;
    DrawItems(model_.roots, {}, list, bar_recipe);
    const auto recipe = popup_recipe.empty() ? item_recipe : popup_recipe;
    for (std::size_t depth = 1; depth <= open_path_.size(); ++depth) {
        const std::vector<std::size_t> parent_path(open_path_.begin(), open_path_.begin() + depth);
        const auto* item = ItemAt(parent_path);
        if (item != nullptr)
            DrawItems(item->children, parent_path, list, recipe);
    }
    return list;
}

} // namespace jrpgmaker::ui
