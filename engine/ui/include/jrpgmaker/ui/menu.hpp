#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "jrpgmaker/ui/draw_list.hpp"
#include "jrpgmaker/ui/interaction.hpp"

namespace jrpgmaker::ui {

struct MenuItem {
    std::uint64_t id = 0;
    std::string label_key;
    std::string command;
    bool enabled = true;
    std::vector<MenuItem> children;
};

struct MenuBarModel {
    static constexpr std::size_t kMaxItems = 128;
    std::vector<MenuItem> roots;
};

struct MenuLayoutConfig {
    float root_width = 92.0f;
    float row_height = 28.0f;
    float popup_width = 240.0f;
};

struct MenuInteractionResult {
    bool changed = false;
    std::vector<UiCommand> commands;
};

class MenuController final {
public:
    [[nodiscard]] bool SetModel(MenuBarModel model);
    [[nodiscard]] bool Layout(Rect bounds, MenuLayoutConfig config);
    [[nodiscard]] MenuInteractionResult PointerMove(float x, float y);
    [[nodiscard]] MenuInteractionResult PointerDown(float x, float y);
    [[nodiscard]] MenuInteractionResult KeyDown(std::string_view key);
    void Close();

    [[nodiscard]] bool open() const { return !open_path_.empty(); }
    [[nodiscard]] const std::vector<std::size_t>& open_path() const { return open_path_; }
    [[nodiscard]] DrawList BuildDrawList(std::string_view bar_recipe, std::string_view popup_recipe,
                                         std::string_view item_recipe) const;

private:
    struct ItemBounds {
        std::vector<std::size_t> path;
        Rect bounds;
    };

    [[nodiscard]] bool Validate(const MenuItem& item, std::size_t& count) const;
    [[nodiscard]] const MenuItem* ItemAt(const std::vector<std::size_t>& path) const;
    [[nodiscard]] ItemBounds* Hit(float x, float y);
    [[nodiscard]] const ItemBounds* Hit(float x, float y) const;
    [[nodiscard]] MenuInteractionResult ActivatePath(const std::vector<std::size_t>& path);
    void RebuildBounds();
    void BuildBounds(const std::vector<MenuItem>& items, const std::vector<std::size_t>& parent,
                     Rect bounds);
    void DrawItems(const std::vector<MenuItem>& items, const std::vector<std::size_t>& parent,
                   DrawList& list, std::string_view recipe) const;

    MenuBarModel model_;
    MenuLayoutConfig config_;
    Rect bar_bounds_;
    std::vector<std::size_t> open_path_;
    std::vector<std::size_t> hover_path_;
    std::vector<ItemBounds> bounds_;
};

} // namespace jrpgmaker::ui
