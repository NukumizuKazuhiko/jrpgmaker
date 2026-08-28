#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "jrpgmaker/plugin/plugin.hpp"
#include "jrpgmaker/rhi/command_list.hpp"
#include "jrpgmaker/rhi/handles.hpp"

namespace jrpgmaker::render {

// Opaque project-owned material data. The render core stores and forwards the
// bytes; only a style adapter may interpret their schema.
using OpaqueMaterialParameters = std::vector<std::byte>;

struct Renderable {
    std::string mesh;
    std::string material;
    glm::mat4 world{1.0f};
    OpaqueMaterialParameters material_parameters;
    // Style-neutral sampled resource id. The selected style decides how its
    // shader interprets the sampled slot; render core only binds the resource.
    std::string sampled_texture;
};

// The scene snapshot is the only scene input crossing the style seam. It is a
// value object so adapters cannot mutate domain or scene state while building
// a frame.
struct SceneSnapshot {
    glm::mat4 view_projection{1.0f};
    std::vector<Renderable> renderables;
};

struct RenderDraw {
    std::string mesh;
    std::string material;
    glm::mat4 world{1.0f};
    OpaqueMaterialParameters material_parameters;
    std::string sampled_texture;
};

struct RenderPass {
    std::string id;
    glm::vec4 clear_color{0.0f};
    bool clear_target = true;
    std::string pipeline;
    std::vector<RenderDraw> draws;
};

struct RenderPlan {
    glm::mat4 view_projection{1.0f};
    std::vector<RenderPass> passes;
};

struct RenderResourceBudget {
    std::size_t max_passes = 64;
    std::size_t max_draws = 4096;
    std::size_t max_material_bytes = 4 * 1024 * 1024;
};

struct RenderResourceCatalog {
    std::vector<std::string> pipeline_ids;
    std::vector<std::string> mesh_ids;
    std::vector<std::string> texture_ids;
};

struct RenderResourceCatalogParseResult {
    std::optional<RenderResourceCatalog> catalog;
    std::string error;
    explicit operator bool() const { return catalog.has_value(); }
};

struct RenderPlanValidation {
    bool ok = true;
    std::string error;
};

struct RenderPlanBuildResult {
    std::optional<RenderPlan> plan;
    std::optional<plugin::PluginError> error;
    explicit operator bool() const { return plan.has_value(); }
};

struct RenderMeshBinding {
    rhi::BufferHandle vertex_buffer = rhi::BufferHandle::kInvalid;
    rhi::BufferHandle index_buffer = rhi::BufferHandle::kInvalid;
    std::uint32_t stride_bytes = 0;
    std::uint32_t index_count = 0;
    bool indices_are_32_bit = true;
};

struct RenderSampledTextureBinding {
    rhi::TextureHandle texture = rhi::TextureHandle::kInvalid;
    rhi::SamplerHandle sampler = rhi::SamplerHandle::kInvalid;
};

struct RenderPlanResolver {
    std::function<std::optional<rhi::PipelineHandle>(const RenderPass&)> resolve_pipeline;
    std::function<std::optional<RenderMeshBinding>(const RenderDraw&)> resolve_mesh;
    std::function<std::optional<RenderSampledTextureBinding>(const RenderDraw&)>
        resolve_sampled_texture;
    std::function<RenderPlanValidation(const RenderDraw&)> validate_material;
    std::function<RenderPlanValidation(rhi::ICommandList&, const RenderDraw&, const glm::mat4&)>
        bind_draw_resources;
};

class RenderPlanExecutor {
public:
    [[nodiscard]] static RenderPlanValidation
    Record(const RenderPlan& plan, rhi::TextureHandle color_target, rhi::ICommandList& command_list,
           const RenderPlanResolver& resolver, const RenderResourceBudget& budget);
};

[[nodiscard]] RenderPlanValidation ValidateRenderPlan(const RenderPlan& plan,
                                                      const RenderResourceBudget& budget);

[[nodiscard]] RenderResourceCatalogParseResult
ParseRenderResourceCatalog(const nlohmann::json& document);

struct RenderStyleDescriptor {
    std::string id;
    std::uint32_t version = 1;
    RenderResourceBudget budget;
};

struct MaterialValidation {
    bool ok = true;
    std::string error;
};

class IRenderStyleAdapter : public plugin::IPlugin {
public:
    virtual ~IRenderStyleAdapter() = default;

    IRenderStyleAdapter(const IRenderStyleAdapter&) = delete;
    IRenderStyleAdapter& operator=(const IRenderStyleAdapter&) = delete;

    [[nodiscard]] virtual RenderStyleDescriptor Descriptor() const = 0;
    // Material instances are intentionally interpreted by the selected style.
    [[nodiscard]] virtual MaterialValidation
    ValidateMaterial(const nlohmann::json& material) const = 0;
    [[nodiscard]] virtual RenderPlan BuildPlan(const SceneSnapshot& snapshot) const = 0;

protected:
    IRenderStyleAdapter() = default;
};

[[nodiscard]] RenderPlanBuildResult BuildRenderPlan(const IRenderStyleAdapter& adapter,
                                                    const SceneSnapshot& snapshot);

} // namespace jrpgmaker::render
