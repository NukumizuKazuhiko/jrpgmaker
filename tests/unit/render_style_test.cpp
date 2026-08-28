#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include "jrpgmaker/render/style.hpp"
#include "jrpgmaker/rhi/command_list.hpp"

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
                                  .material_parameters = renderable.material_parameters,
                                  .sampled_texture = renderable.sampled_texture});
        }
        plan.passes.push_back(std::move(pass));
        return plan;
    }
};

class RecordingCommandList final : public jrpgmaker::rhi::ICommandList {
public:
    void Begin() override {}
    void End() override {}
    void BeginRendering(jrpgmaker::rhi::TextureHandle, const jrpgmaker::rhi::ClearColor&,
                        bool) override {}
    void EndRendering() override { ++end_rendering_calls; }
    void SetPipeline(jrpgmaker::rhi::PipelineHandle) override {}
    void Draw(std::uint32_t, std::uint32_t) override {}
    void SetVertexBuffer(jrpgmaker::rhi::BufferHandle, std::uint32_t) override {}
    void SetIndexBuffer(jrpgmaker::rhi::BufferHandle, bool) override {}
    void DrawIndexed(std::uint32_t, std::uint32_t) override { ++draws; }
    void SetPushConstants(const void*, std::uint32_t) override {}
    void SetSampledTexture(jrpgmaker::rhi::TextureHandle, jrpgmaker::rhi::SamplerHandle) override {
        ++sampled_textures;
    }
    void SetVertexUniformBuffer(jrpgmaker::rhi::BufferHandle, std::uint32_t) override {}

    std::size_t draws = 0;
    std::size_t sampled_textures = 0;
    std::size_t end_rendering_calls = 0;
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
                                    .material_parameters = {std::byte{0x2a}, std::byte{0x7f}},
                                    .sampled_texture = {}});

    const EchoStyle style;
    const auto built_plan = jrpgmaker::render::BuildRenderPlan(style, snapshot);
    REQUIRE(built_plan);
    const auto& plan = *built_plan.plan;

    REQUIRE(plan.passes.size() == 1);
    REQUIRE(plan.passes[0].draws.size() == 1);
    REQUIRE(plan.passes[0].draws[0].mesh == "hero.mesh");
    REQUIRE(plan.passes[0].draws[0].material == "hero.material");
    REQUIRE(plan.passes[0].draws[0].material_parameters ==
            snapshot.renderables[0].material_parameters);
    REQUIRE(plan.passes[0].draws[0].sampled_texture.empty());
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
                                      .material_parameters = {},
                                      .sampled_texture = {}}}});
    const jrpgmaker::render::RenderResourceBudget budget{
        .max_passes = 1, .max_draws = 1, .max_material_bytes = 4};

    REQUIRE(jrpgmaker::render::ValidateRenderPlan(plan, budget).ok);

    plan.passes[0].draws.push_back({.mesh = "mesh2",
                                    .material = "material2",
                                    .world = glm::mat4(1.0f),
                                    .material_parameters = {},
                                    .sampled_texture = {}});
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

TEST_CASE("render plan executor binds generic sampled resources", "[render][style][p8]") {
    jrpgmaker::render::RenderPlan plan;
    plan.passes.push_back({.id = "textured",
                           .clear_color = glm::vec4(0.0f),
                           .clear_target = true,
                           .pipeline = "textured",
                           .draws = {{.mesh = "mesh",
                                      .material = "material",
                                      .world = glm::mat4(1.0f),
                                      .material_parameters = {},
                                      .sampled_texture = "hero.albedo"}}});
    RecordingCommandList command_list;
    const auto result = jrpgmaker::render::RenderPlanExecutor::Record(
        plan, jrpgmaker::rhi::TextureHandle{static_cast<std::uint64_t>(1)}, command_list,
        {.resolve_pipeline =
             [](const auto&) {
                 return std::optional{
                     jrpgmaker::rhi::PipelineHandle{static_cast<std::uint64_t>(1)}};
             },
         .resolve_mesh =
             [](const auto&) {
                 return std::optional{jrpgmaker::render::RenderMeshBinding{
                     .vertex_buffer = jrpgmaker::rhi::BufferHandle{static_cast<std::uint64_t>(1)},
                     .index_buffer = jrpgmaker::rhi::BufferHandle{static_cast<std::uint64_t>(2)},
                     .stride_bytes = 20,
                     .index_count = 3,
                     .indices_are_32_bit = true}};
             },
         .resolve_sampled_texture =
             [](const auto&) {
                 return std::optional{jrpgmaker::render::RenderSampledTextureBinding{
                     .texture = jrpgmaker::rhi::TextureHandle{static_cast<std::uint64_t>(3)},
                     .sampler = jrpgmaker::rhi::SamplerHandle{static_cast<std::uint64_t>(4)}}};
             },
         .validate_material = {},
         .bind_draw_resources = {}},
        jrpgmaker::render::RenderResourceBudget{});
    REQUIRE(result.ok);
    REQUIRE(command_list.draws == 1u);
    REQUIRE(command_list.sampled_textures == 1u);
}

TEST_CASE("render plan executor isolates adapter exceptions", "[render][style][p11]") {
    jrpgmaker::render::RenderPlan plan;
    plan.passes.push_back({.id = "opaque",
                           .clear_color = glm::vec4(0.0f),
                           .clear_target = true,
                           .pipeline = "unlit",
                           .draws = {{.mesh = "mesh",
                                      .material = "material",
                                      .world = glm::mat4(1.0f),
                                      .material_parameters = {},
                                      .sampled_texture = {}}}});
    RecordingCommandList command_list;
    const auto result = jrpgmaker::render::RenderPlanExecutor::Record(
        plan, jrpgmaker::rhi::TextureHandle{static_cast<std::uint64_t>(1)}, command_list,
        {.resolve_pipeline =
             [](const auto&) {
                 return std::optional{
                     jrpgmaker::rhi::PipelineHandle{static_cast<std::uint64_t>(1)}};
             },
         .resolve_mesh =
             [](const auto&) {
                 return std::optional{jrpgmaker::render::RenderMeshBinding{
                     .vertex_buffer = jrpgmaker::rhi::BufferHandle{static_cast<std::uint64_t>(1)},
                     .index_buffer = jrpgmaker::rhi::BufferHandle{static_cast<std::uint64_t>(2)},
                     .stride_bytes = 20,
                     .index_count = 3,
                     .indices_are_32_bit = true}};
             },
         .resolve_sampled_texture = {},
         .validate_material = {},
         .bind_draw_resources = [](auto&, const auto&,
                                   const auto&) -> jrpgmaker::render::RenderPlanValidation {
             throw std::runtime_error("adapter failure");
         }},
        jrpgmaker::render::RenderResourceBudget{});
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error == "render plan execution failed");
    REQUIRE(command_list.end_rendering_calls == 1u);
}

TEST_CASE("render resource catalog validates stable pipeline and mesh IDs", "[render][p6]") {
    const auto result = jrpgmaker::render::ParseRenderResourceCatalog(
        {{"schema", 1},
         {"pipelines", nlohmann::json::array({"unlit", "ui"})},
         {"meshes", nlohmann::json::array({"character", "ui"})},
         {"textures", nlohmann::json::array({"character.albedo"})}});
    REQUIRE(result);
    REQUIRE(result.catalog->pipeline_ids.size() == 2);
    REQUIRE(result.catalog->mesh_ids.size() == 2);
    const auto duplicate = jrpgmaker::render::ParseRenderResourceCatalog(
        {{"schema", 1},
         {"pipelines", nlohmann::json::array({"unlit", "unlit"})},
         {"meshes", nlohmann::json::array({"character"})},
         {"textures", nlohmann::json::array()}});
    REQUIRE_FALSE(duplicate);

    const auto missing_textures = jrpgmaker::render::ParseRenderResourceCatalog(
        {{"schema", 1},
         {"pipelines", nlohmann::json::array({"unlit"})},
         {"meshes", nlohmann::json::array({"character"})}});
    REQUIRE_FALSE(missing_textures);

    const auto duplicate_textures = jrpgmaker::render::ParseRenderResourceCatalog(
        {{"schema", 1},
         {"pipelines", nlohmann::json::array({"unlit"})},
         {"meshes", nlohmann::json::array({"character"})},
         {"textures", nlohmann::json::array({"character.albedo", "character.albedo"})}});
    REQUIRE_FALSE(duplicate_textures);
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
