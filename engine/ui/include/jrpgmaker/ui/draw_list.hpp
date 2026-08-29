#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <glm/vec4.hpp>

#include "jrpgmaker/ui/widget.hpp"

namespace jrpgmaker::ui {

struct DrawRect {
    Rect rect;
    std::string recipe;
    std::string state = "normal";
};

struct DrawText {
    struct EditDecoration {
        std::size_t selection_start = 0;
        std::size_t selection_end = 0;
        std::size_t caret = 0;
        std::string selection_recipe;
        std::string caret_recipe;
        bool enabled = false;
    };

    Rect rect;
    std::string text_key;
    std::unordered_map<std::string, std::string> arguments;
    std::optional<EditDecoration> edit;
};

struct DrawGlyph {
    Rect rect;
    Rect uv;
    glm::vec4 color{1.0f};
};

using DrawPrimitive = std::variant<DrawRect, DrawText, DrawGlyph>;

class DrawList final {
public:
    static constexpr std::size_t kMaxPrimitives = 4096;

    [[nodiscard]] bool Add(DrawPrimitive primitive) {
        if (primitives_.size() >= kMaxPrimitives)
            return false;
        primitives_.push_back(std::move(primitive));
        return true;
    }

    [[nodiscard]] const std::vector<DrawPrimitive>& primitives() const { return primitives_; }
    [[nodiscard]] std::size_t size() const { return primitives_.size(); }

private:
    std::vector<DrawPrimitive> primitives_;
};

} // namespace jrpgmaker::ui
