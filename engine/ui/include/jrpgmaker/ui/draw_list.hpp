#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "jrpgmaker/ui/widget.hpp"

namespace jrpgmaker::ui {

struct DrawRect {
    Rect rect;
    std::string recipe;
    std::string state = "normal";
};

struct DrawText {
    Rect rect;
    std::string text_key;
};

using DrawPrimitive = std::variant<DrawRect, DrawText>;

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
