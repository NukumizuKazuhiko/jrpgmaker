#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "jrpgmaker/ui/editor_resources.hpp"
#include "jrpgmaker/ui/draw_list.hpp"
#include "jrpgmaker/editor/form_projection.hpp"

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
    kIncrement,
    kDecrement,
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
    std::string recipe;
    ui::Rect bounds;
    std::vector<std::size_t> children;
};

struct ShellProjection {
    std::string layout_id;
    std::vector<ShellNode> nodes;
};

[[nodiscard]] std::optional<ShellProjection>
BuildShellProjection(const ui::EditorLayout& layout);

[[nodiscard]] ui::DrawList BuildShellDrawList(const ShellProjection& projection);

[[nodiscard]] ui::DrawList BuildDocumentTabsDrawList(const DocumentTabsProjection& projection,
                                                      ui::Rect bounds);

[[nodiscard]] ui::DrawList BuildFormDrawList(const FormProjection& projection, ui::Rect bounds,
                                              float row_height, std::size_t selected_field);

[[nodiscard]] ui::DrawList BuildPreviewDrawList(const PreviewProjection& projection,
                                                 ui::Rect bounds, float row_height);

} // namespace jrpgmaker::editor
