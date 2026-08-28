#pragma once

#include "jrpgmaker/plugin/battle.hpp"

namespace jrpgmaker::plugins::sample_turn_based {

class Adapter final : public plugin::IBattlePlugin {
public:
    [[nodiscard]] plugin::PluginValidationResult
    ValidateData(const plugin::PluginValidationContext& context) const override;

    [[nodiscard]] plugin::BattleSessionCreateResult
    CreateSession(const plugin::BattleLaunchContext& context) override;
};

} // namespace jrpgmaker::plugins::sample_turn_based
