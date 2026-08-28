#include "jrpgmaker/render/ui_draw_adapter.hpp"

#include <cmath>
#include <string_view>
#include <variant>

namespace jrpgmaker::render {
namespace {

constexpr std::size_t kVerticesPerRect = 4;
constexpr std::size_t kIndicesPerRect = 6;

bool ValidRect(const ui::Rect& rect) {
    return rect.x >= 0.0f && rect.y >= 0.0f && rect.width >= 0.0f &&
           rect.height >= 0.0f && std::isfinite(rect.x) && std::isfinite(rect.y) &&
           std::isfinite(rect.width) && std::isfinite(rect.height);
}

glm::vec4 ToColor(const ui::EditorColor& color) {
    constexpr float kByteScale = 1.0f / 255.0f;
    return {static_cast<float>(color.r) * kByteScale, static_cast<float>(color.g) * kByteScale,
            static_cast<float>(color.b) * kByteScale, static_cast<float>(color.a) * kByteScale};
}

void AddDiagnostic(UiDrawPacket& packet, std::string_view code, std::size_t index) {
    packet.diagnostics.push_back({std::string(code), index});
}

} // namespace

UiDrawPacket BuildUiDrawPacket(const ui::DrawList& draw_list, const ui::EditorTheme& theme,
                               UiViewport viewport) {
    UiDrawPacket packet;
    if (!(viewport.width > 0.0f) || !(viewport.height > 0.0f) ||
        !std::isfinite(viewport.width) || !std::isfinite(viewport.height)) {
        AddDiagnostic(packet, "ui.viewport.invalid", 0);
        return packet;
    }

    packet.vertices.reserve(draw_list.size() * kVerticesPerRect);
    packet.indices.reserve(draw_list.size() * kIndicesPerRect);
    packet.text_keys.reserve(draw_list.size());

    for (std::size_t primitive_index = 0; primitive_index < draw_list.primitives().size();
         ++primitive_index) {
        const auto& primitive = draw_list.primitives()[primitive_index];
        if (const auto* text = std::get_if<ui::DrawText>(&primitive)) {
            if (!ValidRect(text->rect))
                AddDiagnostic(packet, "ui.text.rect_invalid", primitive_index);
            if (text->text_key.empty())
                AddDiagnostic(packet, "ui.text.key_required", primitive_index);
            packet.text_keys.push_back(text->text_key);
            continue;
        }

        const auto& rect = std::get<ui::DrawRect>(primitive);
        if (!ValidRect(rect.rect)) {
            AddDiagnostic(packet, "ui.rect.invalid", primitive_index);
            continue;
        }
        if (rect.rect.x + rect.rect.width > viewport.width ||
            rect.rect.y + rect.rect.height > viewport.height) {
            AddDiagnostic(packet, "ui.rect.out_of_viewport", primitive_index);
            continue;
        }

        const auto recipe = theme.recipes.find(rect.recipe);
        if (recipe == theme.recipes.end()) {
            AddDiagnostic(packet, "ui.recipe.unknown", primitive_index);
            continue;
        }
        const auto state = recipe->second.states.find(rect.state);
        if (state == recipe->second.states.end()) {
            AddDiagnostic(packet, "ui.recipe.state_unknown", primitive_index);
            continue;
        }
        const auto semantic = theme.semantic_tokens.find(state->second);
        if (semantic == theme.semantic_tokens.end()) {
            AddDiagnostic(packet, "ui.semantic_token.unknown", primitive_index);
            continue;
        }
        const auto color = theme.colors.find(semantic->second);
        if (color == theme.colors.end()) {
            AddDiagnostic(packet, "ui.color.unknown", primitive_index);
            continue;
        }

        const float left = 2.0f * rect.rect.x / viewport.width - 1.0f;
        const float right = 2.0f * (rect.rect.x + rect.rect.width) / viewport.width - 1.0f;
        const float top = 1.0f - 2.0f * rect.rect.y / viewport.height;
        const float bottom = 1.0f - 2.0f * (rect.rect.y + rect.rect.height) / viewport.height;
        const auto rgba = ToColor(color->second);
        const auto base = static_cast<std::uint32_t>(packet.vertices.size());
        packet.vertices.push_back({{left, top}, rgba});
        packet.vertices.push_back({{right, top}, rgba});
        packet.vertices.push_back({{right, bottom}, rgba});
        packet.vertices.push_back({{left, bottom}, rgba});
        packet.indices.insert(packet.indices.end(), {base, base + 1, base + 2,
                                                      base, base + 2, base + 3});
    }
    return packet;
}

} // namespace jrpgmaker::render
