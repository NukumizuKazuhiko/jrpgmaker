#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/render/ui_draw_adapter.hpp"

namespace {

jrpgmaker::ui::EditorTheme Theme() {
    jrpgmaker::ui::EditorTheme theme;
    theme.colors.emplace("accent", jrpgmaker::ui::EditorColor{255, 0, 128, 255});
    theme.semantic_tokens.emplace("button.normal", "accent");
    theme.recipes.emplace("button", jrpgmaker::ui::EditorThemeRecipe{
                                      {{"normal", "button.normal"}}});
    return theme;
}

} // namespace

TEST_CASE("ui draw adapter builds ordered ndc geometry", "[render][ui]") {
    jrpgmaker::ui::DrawList list;
    REQUIRE(list.Add(jrpgmaker::ui::DrawRect{{10.0f, 20.0f, 30.0f, 40.0f}, "button"}));
    REQUIRE(list.Add(jrpgmaker::ui::DrawText{{0.0f, 0.0f, 10.0f, 10.0f}, "editor.title", {}}));

    const auto packet = jrpgmaker::render::BuildUiDrawPacket(list, Theme(), {100.0f, 100.0f});
    REQUIRE(packet.ok());
    REQUIRE(packet.vertices.size() == 4);
    REQUIRE(packet.indices.size() == 6);
    REQUIRE(packet.vertices.front().position == glm::vec3{-0.8f, 0.6f, 0.0f});
    REQUIRE(packet.vertices.front().color == glm::vec4{1.0f, 0.0f, 128.0f / 255.0f, 1.0f});
    REQUIRE(packet.text_keys == std::vector<std::string>{"editor.title"});
}

TEST_CASE("ui draw adapter reports invalid theme and geometry", "[render][ui]") {
    jrpgmaker::ui::DrawList list;
    REQUIRE(list.Add(jrpgmaker::ui::DrawRect{{0.0f, 0.0f, 2.0f, 2.0f}, "missing"}));
    REQUIRE(list.Add(jrpgmaker::ui::DrawRect{{99.0f, 0.0f, 2.0f, 2.0f}, "button"}));
    REQUIRE(list.Add(jrpgmaker::ui::DrawText{{}, "", {}}));

    const auto packet = jrpgmaker::render::BuildUiDrawPacket(list, Theme(), {100.0f, 100.0f});
    REQUIRE_FALSE(packet.ok());
    REQUIRE(packet.diagnostics.size() == 3);
    REQUIRE(packet.diagnostics[0].code == "ui.recipe.unknown");
    REQUIRE(packet.diagnostics[1].code == "ui.rect.out_of_viewport");
    REQUIRE(packet.diagnostics[2].code == "ui.text.key_required");
}

TEST_CASE("ui draw adapter rejects invalid viewport", "[render][ui]") {
    jrpgmaker::ui::DrawList list;
    const auto packet = jrpgmaker::render::BuildUiDrawPacket(list, Theme(), {0.0f, 100.0f});
    REQUIRE_FALSE(packet.ok());
    REQUIRE(packet.diagnostics.front().code == "ui.viewport.invalid");
}

TEST_CASE("ui text adapter converts glyph quads to ndc", "[render][ui][text]") {
    jrpgmaker::ui::DrawList list;
    REQUIRE(list.Add(jrpgmaker::ui::DrawGlyph{{10.0f, 20.0f, 8.0f, 12.0f},
                                              {0.1f, 0.2f, 0.25f, 0.5f},
                                              {1.0f, 0.5f, 0.25f, 1.0f}}));
    const auto packet = jrpgmaker::render::BuildUiTextDrawPacket(list, {100.0f, 100.0f});
    REQUIRE(packet.ok());
    REQUIRE(packet.vertices.size() == 4);
    REQUIRE(packet.indices.size() == 6);
    REQUIRE(packet.vertices.front().position == glm::vec3{-0.8f, 0.6f, 0.0f});
    REQUIRE(packet.vertices.front().uv == glm::vec2{0.1f, 0.2f});
}
