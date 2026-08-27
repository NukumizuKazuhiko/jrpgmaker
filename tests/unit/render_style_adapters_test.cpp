#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/plugins/sample_style/style.hpp"
#include "jrpgmaker/plugins/sample_unlit/unlit.hpp"

TEST_CASE("two render style adapters produce replaceable plans", "[render][style][p6]") {
    jrpgmaker::render::SceneSnapshot snapshot;
    snapshot.renderables.push_back({.mesh = "hero.mesh",
                                    .material = "hero.material",
                                    .world = glm::mat4(1.0f),
                                    .material_parameters = {}});

    const jrpgmaker::plugins::sample_unlit::Adapter unlit;
    const jrpgmaker::plugins::sample_style::Adapter style;
    const auto unlit_plan = unlit.BuildPlan(snapshot);
    const auto style_plan = style.BuildPlan(snapshot);

    REQUIRE(unlit.Descriptor().id == "sample.unlit");
    REQUIRE(style.Descriptor().id == "sample.style");
    REQUIRE(unlit_plan.passes.size() == 1);
    REQUIRE(style_plan.passes.size() == 1);
    REQUIRE(unlit_plan.passes[0].id != style_plan.passes[0].id);
    REQUIRE(unlit_plan.passes[0].pipeline != style_plan.passes[0].pipeline);
    REQUIRE(unlit_plan.passes[0].clear_color != style_plan.passes[0].clear_color);
    REQUIRE(unlit_plan.passes[0].draws.size() == style_plan.passes[0].draws.size());
    REQUIRE(unlit_plan.passes[0].draws[0].mesh == style_plan.passes[0].draws[0].mesh);
}

TEST_CASE("style adapters own their material schemas", "[render][style][p6]") {
    const jrpgmaker::plugins::sample_unlit::Adapter unlit;
    const jrpgmaker::plugins::sample_style::Adapter style;
    REQUIRE(unlit.ValidateMaterial({{"schema", 1}, {"base_color", {1.0, 0.5, 0.0, 1.0}}}).ok);
    REQUIRE_FALSE(unlit.ValidateMaterial({{"schema", 1}, {"accent", {1.0, 0.5, 0.0, 1.0}}}).ok);
    REQUIRE(style.ValidateMaterial({{"schema", 1}, {"accent", {1.0, 0.5, 0.0, 1.0}}}).ok);
    REQUIRE_FALSE(style.ValidateMaterial({{"schema", 1}, {"accent", {2.0, 0.0, 0.0, 1.0}}}).ok);
}

TEST_CASE("style adapters consume opaque project material parameters", "[render][style][p6]") {
    const auto encode = [](const nlohmann::json& value) {
        const auto cbor = nlohmann::json::to_cbor(value);
        std::vector<std::byte> bytes;
        bytes.reserve(cbor.size());
        for (const std::uint8_t byte : cbor)
            bytes.push_back(static_cast<std::byte>(byte));
        return bytes;
    };
    const auto unlit_parameters = encode({{"schema", 1}, {"base_color", {0.7, 0.2, 0.1, 1.0}}});
    const auto style_parameters = encode({{"schema", 1}, {"accent", {0.1, 0.8, 0.3, 1.0}}});

    jrpgmaker::render::SceneSnapshot unlit_snapshot;
    unlit_snapshot.renderables.push_back({.mesh = "hero.mesh",
                                          .material = "hero.material",
                                          .world = glm::mat4(1.0f),
                                          .material_parameters = unlit_parameters});
    jrpgmaker::render::SceneSnapshot style_snapshot;
    style_snapshot.renderables.push_back({.mesh = "hero.mesh",
                                          .material = "hero.material",
                                          .world = glm::mat4(1.0f),
                                          .material_parameters = style_parameters});

    const auto unlit_plan = jrpgmaker::plugins::sample_unlit::Adapter().BuildPlan(unlit_snapshot);
    const auto style_plan = jrpgmaker::plugins::sample_style::Adapter().BuildPlan(style_snapshot);
    REQUIRE(unlit_plan.passes[0].clear_color == glm::vec4(0.7f, 0.2f, 0.1f, 1.0f));
    REQUIRE(style_plan.passes[0].clear_color == glm::vec4(0.1f, 0.8f, 0.3f, 1.0f));
    REQUIRE(unlit_plan.passes[0].draws[0].material_parameters == unlit_parameters);
    REQUIRE(style_plan.passes[0].draws[0].material_parameters == style_parameters);
}
