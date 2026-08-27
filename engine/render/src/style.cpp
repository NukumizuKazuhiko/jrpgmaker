#include "jrpgmaker/render/style.hpp"

#include <unordered_set>

namespace jrpgmaker::render {

RenderResourceCatalogParseResult ParseRenderResourceCatalog(const nlohmann::json& document) {
    if (!document.is_object() || document.value("schema", 0) != 1 ||
        !document.contains("pipelines") || !document.contains("meshes") ||
        !document.contains("textures") || !document["pipelines"].is_array() ||
        !document["meshes"].is_array() || !document["textures"].is_array()) {
        return {.catalog = std::nullopt,
                .error =
                    "render resource catalog requires schema 1, pipelines, meshes, and textures"};
    }
    RenderResourceCatalog catalog;
    std::unordered_set<std::string> ids;
    for (const auto& value : document["pipelines"]) {
        if (!value.is_string() || value.get<std::string>().empty() ||
            !ids.insert(value.get<std::string>()).second) {
            return {.catalog = std::nullopt,
                    .error = "render resource pipeline IDs must be unique non-empty strings"};
        }
        catalog.pipeline_ids.push_back(value.get<std::string>());
    }
    ids.clear();
    for (const auto& value : document["meshes"]) {
        if (!value.is_string() || value.get<std::string>().empty() ||
            !ids.insert(value.get<std::string>()).second) {
            return {.catalog = std::nullopt,
                    .error = "render resource mesh IDs must be unique non-empty strings"};
        }
        catalog.mesh_ids.push_back(value.get<std::string>());
    }
    if (catalog.pipeline_ids.empty() || catalog.mesh_ids.empty()) {
        return {.catalog = std::nullopt,
                .error = "render resource catalog must contain pipelines and meshes"};
    }
    ids.clear();
    for (const auto& value : document["textures"]) {
        if (!value.is_string() || value.get<std::string>().empty() ||
            !ids.insert(value.get<std::string>()).second) {
            return {.catalog = std::nullopt,
                    .error = "render resource texture IDs must be unique non-empty strings"};
        }
        catalog.texture_ids.push_back(value.get<std::string>());
    }
    return {.catalog = std::move(catalog), .error = std::string{}};
}

RenderPlanValidation ValidateRenderPlan(const RenderPlan& plan,
                                        const RenderResourceBudget& budget) {
    if (plan.passes.size() > budget.max_passes) {
        return {.ok = false, .error = "render plan exceeds pass budget"};
    }

    std::size_t draw_count = 0;
    std::size_t material_bytes = 0;
    for (const RenderPass& pass : plan.passes) {
        if (pass.id.empty()) {
            return {.ok = false, .error = "render pass id must not be empty"};
        }
        draw_count += pass.draws.size();
        if (draw_count > budget.max_draws) {
            return {.ok = false, .error = "render plan exceeds draw budget"};
        }
        for (const RenderDraw& draw : pass.draws) {
            const std::size_t bytes = draw.material_parameters.size();
            if (material_bytes > budget.max_material_bytes ||
                bytes > budget.max_material_bytes - material_bytes) {
                return {.ok = false, .error = "render plan exceeds material budget"};
            }
            material_bytes += bytes;
        }
    }

    return {};
}

RenderPlanValidation RenderPlanExecutor::Record(const RenderPlan& plan,
                                                rhi::TextureHandle color_target,
                                                rhi::ICommandList& command_list,
                                                const RenderPlanResolver& resolver,
                                                const RenderResourceBudget& budget) {
    const auto valid = ValidateRenderPlan(plan, budget);
    if (!valid.ok) {
        return valid;
    }
    if (!resolver.resolve_pipeline || !resolver.resolve_mesh) {
        return {.ok = false, .error = "render plan resolver is incomplete"};
    }
    for (const RenderPass& pass : plan.passes) {
        const auto pipeline = resolver.resolve_pipeline(pass);
        if (!pipeline.has_value() || *pipeline == rhi::PipelineHandle::kInvalid) {
            return {.ok = false, .error = "render pass pipeline could not be resolved"};
        }
        command_list.BeginRendering(
            color_target,
            {pass.clear_color.r, pass.clear_color.g, pass.clear_color.b, pass.clear_color.a},
            pass.clear_target);
        command_list.SetPipeline(*pipeline);
        for (const RenderDraw& draw : pass.draws) {
            if (resolver.validate_material) {
                const auto material = resolver.validate_material(draw);
                if (!material.ok) {
                    command_list.EndRendering();
                    return material;
                }
            }
            const auto mesh = resolver.resolve_mesh(draw);
            if (!mesh.has_value() || mesh->vertex_buffer == rhi::BufferHandle::kInvalid ||
                mesh->index_buffer == rhi::BufferHandle::kInvalid || mesh->stride_bytes == 0 ||
                mesh->index_count == 0) {
                command_list.EndRendering();
                return {.ok = false, .error = "render draw mesh could not be resolved"};
            }
            command_list.SetVertexBuffer(mesh->vertex_buffer, mesh->stride_bytes);
            command_list.SetIndexBuffer(mesh->index_buffer, mesh->indices_are_32_bit);
            if (resolver.bind_draw_resources) {
                const auto bound =
                    resolver.bind_draw_resources(command_list, draw, plan.view_projection);
                if (!bound.ok) {
                    command_list.EndRendering();
                    return bound;
                }
            }
            if (!draw.sampled_texture.empty()) {
                if (!resolver.resolve_sampled_texture) {
                    command_list.EndRendering();
                    return {.ok = false, .error = "sampled texture resolver is incomplete"};
                }
                const auto sampled = resolver.resolve_sampled_texture(draw);
                if (!sampled.has_value() || sampled->texture == rhi::TextureHandle::kInvalid ||
                    sampled->sampler == rhi::SamplerHandle::kInvalid) {
                    command_list.EndRendering();
                    return {.ok = false,
                            .error = "render draw sampled texture could not be resolved"};
                }
                command_list.SetSampledTexture(sampled->texture, sampled->sampler);
            }
            command_list.DrawIndexed(mesh->index_count, 1);
        }
        command_list.EndRendering();
    }
    return {};
}

} // namespace jrpgmaker::render
