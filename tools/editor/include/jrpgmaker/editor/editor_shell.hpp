#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "jrpgmaker/ui/editor_resources.hpp"

namespace jrpgmaker::editor {

enum class EditorAction : std::uint8_t {
    kOpen,
    kSave,
    kRefresh,
    kSelectNext,
    kSelectPrevious,
    kConfirm,
    kCancel,
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

private:
    std::unordered_map<std::string, EditorAction> bindings_;
};

[[nodiscard]] InputMap BuildInputMap(const ui::EditorActionMap& resource);

struct ShellNode {
    std::string id;
    std::string type;
    std::string label_key;
    std::string recipe;
    std::vector<std::size_t> children;
};

struct ShellProjection {
    std::string layout_id;
    std::vector<ShellNode> nodes;
};

[[nodiscard]] std::optional<ShellProjection>
BuildShellProjection(const ui::EditorLayout& layout);

} // namespace jrpgmaker::editor
