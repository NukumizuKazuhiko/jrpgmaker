#pragma once

#include "jrpgmaker/render/style.hpp"

namespace jrpgmaker::plugins::sample_unlit {

class Adapter final : public render::IRenderStyleAdapter {
public:
    [[nodiscard]] render::RenderStyleDescriptor Descriptor() const override;
    [[nodiscard]] render::MaterialValidation
    ValidateMaterial(const nlohmann::json& material) const override;
    [[nodiscard]] render::RenderPlan
    BuildPlan(const render::SceneSnapshot& snapshot) const override;
};

} // namespace jrpgmaker::plugins::sample_unlit
