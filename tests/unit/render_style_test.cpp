#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include "jrpgmaker/render/style.hpp"

namespace {

class EchoStyle final : public jrpgmaker::render::IRenderStyleAdapter {
public:
    jrpgmaker::render::RenderStyleDescriptor Descriptor() const override {
        return {.id = "test.echo",
                .version = 1,
                .budget = {.max_passes = 2, .max_draws = 4, .max_material_bytes = 16}};
    }

    jrpgmaker::render::MaterialValidation ValidateMaterial(const nlohmann::json&) const override {
        return {};
    }

    jrpgmaker::render::RenderPlan
    BuildPlan(const jrpgmaker::render::SceneSnapshot& snapshot) const override {
        jrpgmaker::render::RenderPlan plan;
        jrpgmaker::render::RenderPass pass{.id = "opaque",
                                           .clear_color = glm::vec4(0.0f),
                                           .clear_target = true,
                                           .pipeline = "",
                                           .draws = {}};
        for (const auto& renderable : snapshot.renderables) {
            pass.draws.push_back({.mesh = renderable.mesh,
                                  .material = renderable.material,
                                  .world = renderable.world,
                                  .material_parameters = renderable.material_parameters});
        }
        plan.passes.push_back(std::move(pass));
        return plan;
    }
};

std::unique_ptr<jrpgmaker::plugin::IPlugin> CreateEchoStyle() {
    return std::make_unique<EchoStyle>();
}

} // namespace

TEST_CASE("render style adapter receives a generic scene snapshot", "[render][style]") {
    jrpgmaker::render::SceneSnapshot snapshot;
    snapshot.renderables.push_back({.mesh = "hero.mesh",
                                    .material = "hero.material",
                                    .world = glm::translate(glm::mat4(1.0f), {1.0f, 2.0f, 3.0f}),
                                    .material_parameters = {std::byte{0x2a}, std::byte{0x7f}}});

    const EchoStyle style;
    const jrpgmaker::render::RenderPlan plan = style.BuildPlan(snapshot);

    REQUIRE(plan.passes.size() == 1);
    REQUIRE(plan.passes[0].draws.size() == 1);
    REQUIRE(plan.passes[0].draws[0].mesh == "hero.mesh");
    REQUIRE(plan.passes[0].draws[0].material == "hero.material");
    REQUIRE(plan.passes[0].draws[0].material_parameters ==
            snapshot.renderables[0].material_parameters);
    REQUIRE(plan.passes[0].draws[0].world[3] == glm::vec4(1.0f, 2.0f, 3.0f, 1.0f));
}

TEST_CASE("render plan validation enforces the style resource budget", "[render][style]") {
    jrpgmaker::render::RenderPlan plan;
    plan.passes.push_back({.id = "opaque",
                           .clear_color = glm::vec4(0.0f),
                           .clear_target = true,
                           .pipeline = "",
                           .draws = {{.mesh = "mesh",
                                      .material = "material",
                                      .world = glm::mat4(1.0f),
                                      .material_parameters = {}}}});
    const jrpgmaker::render::RenderResourceBudget budget{
        .max_passes = 1, .max_draws = 1, .max_material_bytes = 4};

    REQUIRE(jrpgmaker::render::ValidateRenderPlan(plan, budget).ok);

    plan.passes[0].draws.push_back({.mesh = "mesh2",
                                    .material = "material2",
                                    .world = glm::mat4(1.0f),
                                    .material_parameters = {}});
    const auto too_many_draws = jrpgmaker::render::ValidateRenderPlan(plan, budget);
    REQUIRE_FALSE(too_many_draws.ok);
    REQUIRE(too_many_draws.error == "render plan exceeds draw budget");
}

TEST_CASE("render plan validation rejects unnamed passes", "[render][style]") {
    jrpgmaker::render::RenderPlan plan;
    plan.passes.push_back({});

    const auto result =
        jrpgmaker::render::ValidateRenderPlan(plan, jrpgmaker::render::RenderResourceBudget{});
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error == "render pass id must not be empty");
}

TEST_CASE("render resource catalog validates stable pipeline and mesh IDs", "[render][p6]") {
    const auto result = jrpgmaker::render::ParseRenderResourceCatalog(
        {{"schema", 1},
         {"pipelines", nlohmann::json::array({"unlit", "ui"})},
         {"meshes", nlohmann::json::array({"character", "ui"})}});
    REQUIRE(result);
    REQUIRE(result.catalog->pipeline_ids.size() == 2);
    REQUIRE(result.catalog->mesh_ids.size() == 2);
    const auto duplicate = jrpgmaker::render::ParseRenderResourceCatalog(
        {{"schema", 1},
         {"pipelines", nlohmann::json::array({"unlit", "unlit"})},
         {"meshes", nlohmann::json::array({"character"})}});
    REQUIRE_FALSE(duplicate);
}

TEST_CASE("render style adapters use the common plugin registry", "[render][style][plugin]") {
    jrpgmaker::plugin::PluginRegistry registry;
    const auto parsed = jrpgmaker::plugin::ParseManifest(
        {{"schema", 1},
         {"id", "test.echo"},
         {"type", "render_style"},
         {"version", 1},
         {"engine_contract", 1},
         {"data_roots", nlohmann::json::array()},
         {"capabilities", nlohmann::json::array({"render_plan"})}});
    REQUIRE(parsed);
    REQUIRE_FALSE(registry.Register(*parsed.manifest, CreateEchoStyle));

    const auto created = registry.Create("test.echo", jrpgmaker::plugin::PluginType::kRenderStyle);
    REQUIRE(created);
    REQUIRE(dynamic_cast<jrpgmaker::render::IRenderStyleAdapter*>(created.instance.get()) !=
            nullptr);
}
