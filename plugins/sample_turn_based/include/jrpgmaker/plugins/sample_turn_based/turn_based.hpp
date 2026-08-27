#pragma once

#include "jrpgmaker/plugin/battle.hpp"

namespace jrpgmaker::plugins::sample_turn_based {

class Adapter final : public plugin::IBattlePlugin {
public:
    [[nodiscard]] plugin::BattleSessionCreateResult
    CreateSession(const plugin::BattleLaunchContext& context) override;
};

} // namespace jrpgmaker::plugins::sample_turn_based
