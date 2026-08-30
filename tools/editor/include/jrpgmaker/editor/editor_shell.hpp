#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "jrpgmaker/editor/form_projection.hpp"
#include "jrpgmaker/ui/draw_list.hpp"
#include "jrpgmaker/ui/editor_resources.hpp"
#include "jrpgmaker/ui/menu.hpp"

namespace jrpgmaker::editor {

enum class EditorAction : std::uint8_t {
    kOpen,
    kSave,
    kRefresh,
    kSelectNext,
    kSelectPrevious,
    kConfirm,
    kCancel,
    kPreview,
    kStopPreview,
    kIncrement,
    kDecrement,
    kToggle,
    kChoiceNext,
    kChoicePrevious,
};

struct KeyBinding {
    std::string key_name;
    EditorAction action = EditorAction::kConfirm;
};

class InputMap final {
public:
    static constexpr std::size_t kMaxBindings = 64;

    [[nodiscard]] bool Add(KeyBinding binding);
    [[nodiscard]] std::optional<EditorAction> Translate(std::string_view key_name,
                                                        bool pressed) const;
    [[nodiscard]] std::optional<EditorAction> Translate(std::string_view key_name, bool pressed,
                                                        bool control, bool shift, bool alt) const;

private:
    std::unordered_map<std::string, EditorAction> bindings_;
};

[[nodiscard]] InputMap BuildInputMap(const ui::EditorActionMap& resource);

struct ShellNode {
    std::string id;
    std::string type;
    std::string label_key;
    std::string command;
    std::string recipe;
    ui::Rect bounds;
    std::vector<std::size_t> children;
};

struct ShellProjection {
    std::string layout_id;
    std::vector<ShellNode> nodes;
};

struct ToolbarButtonProjection {
    EditorAction action = EditorAction::kOpen;
    std::string label_key;
    ui::Rect bounds;
    bool enabled = true;
    bool hovered = false;
    bool pressed = false;
    bool focused = false;
};

struct ToolbarProjection {
    std::vector<ToolbarButtonProjection> buttons;
};

void ReflowShellProjection(ShellProjection& projection, float width, float height);

struct NavigationCellProjection {
    int x = 0;
    int y = 0;
    bool walkable = false;
    bool selected = false;
    ui::Rect bounds;
};

struct NavigationProjection {
    static constexpr std::size_t kMaxProjectedCells = 2048;

    std::string document_id;
    int width = 0;
    int height = 0;
    std::vector<NavigationCellProjection> cells;
    std::optional<std::size_t> selected;
};

[[nodiscard]] std::optional<std::size_t> HitNavigationCell(const NavigationProjection& projection,
                                                           float x, float y);
void LayoutNavigation(NavigationProjection& projection, ui::Rect bounds);
[[nodiscard]] ui::DrawList BuildNavigationDrawList(const NavigationProjection& projection);
[[nodiscard]] ui::DrawList BuildNavigationInspectorDrawList(const NavigationProjection& projection,
                                                            ui::Rect bounds);

struct StatusBarProjection {
    bool open = false;
    bool dirty = false;
    std::uint64_t revision = 0;
    std::string focused_panel_label_key;
};

struct TextFieldVisualProjection {
    bool active = false;
    std::size_t selection_start = 0;
    std::size_t selection_end = 0;
    std::size_t caret = 0;
    std::string selection_recipe;
    std::string caret_recipe;
};

[[nodiscard]] std::optional<ShellProjection> BuildShellProjection(const ui::EditorLayout& layout);

[[nodiscard]] ui::DrawList BuildShellDrawList(const ShellProjection& projection);

[[nodiscard]] ui::DrawList BuildStatusBarDrawList(const StatusBarProjection& projection,
                                                  ui::Rect bounds, std::string_view recipe);

[[nodiscard]] ui::DrawList BuildToolbarDrawList(ui::Rect bounds, bool project_open,
                                                bool preview_running,
                                                std::optional<EditorAction> hovered = std::nullopt,
                                                std::optional<EditorAction> pressed = std::nullopt,
                                                std::optional<EditorAction> focused = std::nullopt);
[[nodiscard]] ToolbarProjection
BuildToolbarProjection(ui::Rect bounds, bool project_open, bool preview_running,
                       std::optional<EditorAction> hovered = std::nullopt,
                       std::optional<EditorAction> pressed = std::nullopt,
                       std::optional<EditorAction> focused = std::nullopt);
[[nodiscard]] std::optional<EditorAction> HitToolbar(const ToolbarProjection& projection, float x,
                                                     float y);

[[nodiscard]] ui::DrawList BuildDocumentTabsDrawList(const DocumentTabsProjection& projection,
                                                     ui::Rect bounds);

[[nodiscard]] ui::DrawList BuildProjectDrawList(const DocumentTabsProjection& projection,
                                                ui::Rect bounds, float row_height,
                                                std::string_view filter = {});

[[nodiscard]] ui::DrawList BuildHierarchyDrawList(const NavigationProjection& projection,
                                                  ui::Rect bounds, float row_height);

[[nodiscard]] ui::DrawList
BuildDiagnosticsDrawList(const std::vector<project::Diagnostic>& diagnostics,
                         const DiffProjection& diff, const PreviewProjection& preview,
                         ui::Rect bounds, float row_height);

[[nodiscard]] ui::DrawList BuildFormDrawList(const FormProjection& projection, ui::Rect bounds,
                                             float row_height, std::size_t selected_field,
                                             const TextFieldVisualProjection* text_field = nullptr);

[[nodiscard]] ui::DrawList BuildPreviewDrawList(const PreviewProjection& projection,
                                                ui::Rect bounds, float row_height);

} // namespace jrpgmaker::editor
