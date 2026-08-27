#include "jrpgmaker/plugins/sample_unlit/unlit.hpp"

#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace jrpgmaker::plugins::sample_unlit {
namespace {

std::optional<glm::vec4> DecodeBaseColor(const render::OpaqueMaterialParameters& bytes) {
    if (bytes.empty())
        return std::nullopt;
    std::vector<std::uint8_t> encoded;
    encoded.reserve(bytes.size());
    for (const std::byte byte : bytes)
        encoded.push_back(static_cast<std::uint8_t>(byte));
    try {
        const auto material = nlohmann::json::from_cbor(encoded);
        if (!material.is_object() || !material.contains("base_color") ||
            !material["base_color"].is_array() || material["base_color"].size() != 4) {
            return std::nullopt;
        }
        return glm::vec4(
            material["base_color"][0].get<float>(), material["base_color"][1].get<float>(),
            material["base_color"][2].get<float>(), material["base_color"][3].get<float>());
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

} // namespace

render::RenderStyleDescriptor Adapter::Descriptor() const {
    return {.id = "sample.unlit", .version = 1, .budget = {}};
}

render::MaterialValidation Adapter::ValidateMaterial(const nlohmann::json& material) const {
    if (!material.is_object() || material.value("schema", 0) != 1 ||
        !material.contains("base_color") || !material["base_color"].is_array() ||
        material["base_color"].size() != 4) {
        return {.ok = false, .error = "sample.unlit material requires schema 1 base_color[4]"};
    }
    for (const auto& component : material["base_color"]) {
        if (!component.is_number() || component.get<float>() < 0.0f ||
            component.get<float>() > 1.0f) {
            return {.ok = false, .error = "sample.unlit base_color components must be in [0,1]"};
        }
    }
    return {};
}

render::RenderPlan Adapter::BuildPlan(const render::SceneSnapshot& snapshot) const {
    render::RenderPlan plan{.view_projection = snapshot.view_projection, .passes = {}};
    render::RenderPass pass{.id = "sample.unlit.opaque",
                            .clear_color = {0.08f, 0.09f, 0.11f, 1.0f},
                            .clear_target = true,
                            .pipeline = "unlit",
                            .draws = {}};
    for (const auto& renderable : snapshot.renderables) {
        if (const auto color = DecodeBaseColor(renderable.material_parameters); color.has_value()) {
            pass.clear_color = *color;
        }
        pass.draws.push_back({.mesh = renderable.mesh,
                              .material = renderable.material,
                              .world = renderable.world,
                              .material_parameters = renderable.material_parameters});
    }
    plan.passes.push_back(std::move(pass));
    return plan;
}

} // namespace jrpgmaker::plugins::sample_unlit
