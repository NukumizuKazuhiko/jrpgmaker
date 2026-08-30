#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "jrpgmaker/ui/draw_list.hpp"
#include "jrpgmaker/ui/interaction.hpp"

namespace jrpgmaker::ui {

enum class UiLayoutMode : std::uint8_t { kAbsolute, kVertical };

struct UiNode {
    std::uint64_t id = 0;
    std::string label_key;
    std::string recipe;
    Rect bounds;
    UiLayoutMode layout = UiLayoutMode::kAbsolute;
    float preferred_height = 0.0f;
    bool visible = true;
    bool enabled = true;
    std::vector<UiNode> children;
};

class UiTree final {
public:
    static constexpr std::size_t kMaxNodes = 256;
    static constexpr std::size_t kMaxCommands = 64;

    [[nodiscard]] bool SetRoot(UiNode root);
    [[nodiscard]] bool Layout(Rect viewport);
    [[nodiscard]] std::vector<UiCommand> Dispatch(const UiEvent& event);
    [[nodiscard]] DrawList BuildDrawList() const;
    [[nodiscard]] const UiNode* Find(std::uint64_t id) const;
    [[nodiscard]] const UiContext& focus() const { return focus_; }

private:
    static bool Validate(const UiNode& node, std::size_t& count, std::vector<std::uint64_t>& ids);
    void LayoutNode(UiNode& node, Rect available);
    static const UiNode* FindNode(const UiNode& node, std::uint64_t id);
    static void DrawNode(const UiNode& node, DrawList& draw_list);
    static const UiNode* HitNode(const UiNode& node, float x, float y);

    UiNode root_;
    bool has_root_ = false;
    UiContext focus_;
    std::unordered_map<std::uint64_t, Rect> layout_specs_;
};

} // namespace jrpgmaker::ui
