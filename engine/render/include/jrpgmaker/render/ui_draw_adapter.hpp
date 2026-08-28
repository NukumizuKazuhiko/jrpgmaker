#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "jrpgmaker/ui/draw_list.hpp"
#include "jrpgmaker/ui/editor_resources.hpp"

namespace jrpgmaker::render {

struct UiVertex {
    glm::vec2 position;
    glm::vec4 color;
};

struct UiViewport {
    float width = 0.0f;
    float height = 0.0f;
};

struct UiDrawDiagnostic {
    std::string code;
    std::size_t primitive_index = 0;
};

struct UiDrawPacket {
    std::vector<UiVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::string> text_keys;
    std::vector<UiDrawDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const { return diagnostics.empty(); }
};

// Converts screen-space editor primitives into an ordered NDC upload packet.
// Text remains a key so glyph shaping and localization stay owned by the text
// pipeline; this adapter only owns primitive ordering and geometry conversion.
[[nodiscard]] UiDrawPacket BuildUiDrawPacket(const ui::DrawList& draw_list,
                                             const ui::EditorTheme& theme,
                                             UiViewport viewport);

} // namespace jrpgmaker::render
