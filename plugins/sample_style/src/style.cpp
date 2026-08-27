#include "jrpgmaker/plugins/sample_style/style.hpp"

#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace jrpgmaker::plugins::sample_style {
namespace {

std::optional<glm::vec4> DecodeAccent(const render::OpaqueMaterialParameters& bytes) {
    if (bytes.empty())
        return std::nullopt;
    std::vector<std::uint8_t> encoded;
    encoded.reserve(bytes.size());
    for (const std::byte byte : bytes)
        encoded.push_back(static_cast<std::uint8_t>(byte));
    try {
        const auto material = nlohmann::json::from_cbor(encoded);
        if (!material.is_object() || !material.contains("accent") ||
            !material["accent"].is_array() || material["accent"].size() != 4) {
            return std::nullopt;
        }
        return glm::vec4(material["accent"][0].get<float>(), material["accent"][1].get<float>(),
                         material["accent"][2].get<float>(), material["accent"][3].get<float>());
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

} // namespace

render::RenderStyleDescriptor Adapter::Descriptor() const {
    return {.id = "sample.style", .version = 1, .budget = {}};
}

render::MaterialValidation Adapter::ValidateMaterial(const nlohmann::json& material) const {
    if (!material.is_object() || material.value("schema", 0) != 1 || !material.contains("accent") ||
        !material["accent"].is_array() || material["accent"].size() != 4) {
        return {.ok = false, .error = "sample.style material requires schema 1 accent[4]"};
    }
    for (const auto& component : material["accent"]) {
        if (!component.is_number() || component.get<float>() < 0.0f ||
            component.get<float>() > 1.0f) {
            return {.ok = false, .error = "sample.style accent components must be in [0,1]"};
        }
    }
    return {};
}

render::RenderPlan Adapter::BuildPlan(const render::SceneSnapshot& snapshot) const {
    render::RenderPlan plan{.view_projection = snapshot.view_projection, .passes = {}};
    render::RenderPass pass{.id = "sample.style.accent",
                            .clear_color = {0.20f, 0.06f, 0.16f, 1.0f},
                            .clear_target = true,
                            .pipeline = "accent",
                            .draws = {}};
    for (const auto& renderable : snapshot.renderables) {
        if (const auto color = DecodeAccent(renderable.material_parameters); color.has_value()) {
            pass.clear_color = *color;
        }
        if (!renderable.sampled_texture.empty()) {
            // The plugin owns the decision to consume a generic sampled
            // resource. The app maps this style-owned pipeline to its
            // textured implementation, while the resource ID remains opaque.
            pass.pipeline = "accent_textured";
        }
        pass.draws.push_back({.mesh = renderable.mesh,
                              .material = renderable.material,
                              .world = renderable.world,
                              .material_parameters = renderable.material_parameters,
                              .sampled_texture = renderable.sampled_texture});
    }
    plan.passes.push_back(std::move(pass));
    return plan;
}

} // namespace jrpgmaker::plugins::sample_style
