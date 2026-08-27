#include "jrpgmaker/plugin/battle.hpp"

#include <cmath>

namespace jrpgmaker::plugin {

namespace {

std::optional<PluginError> Invalid(const char* code, const char* message) {
    return PluginError{code, message, "battle"};
}

} // namespace

std::optional<PluginError> ValidateBattleLaunch(const BattleLaunchContext& context) {
    if (context.encounter_id.empty())
        return Invalid("battle.encounter", "encounter id is empty");
    if (context.opaque_payload.size() > kMaxBattlePayloadBytes) {
        return Invalid("battle.payload", "launch payload exceeds the 64 KiB limit");
    }
    return std::nullopt;
}

std::optional<PluginError> ValidateBattleInput(const BattleFrameInput& input) {
    if (!std::isfinite(input.delta_seconds) || input.delta_seconds < 0.0) {
        return Invalid("battle.delta", "delta_seconds must be finite and non-negative");
    }
    if (input.action_ids.size() > kMaxBattleActionsPerFrame) {
        return Invalid("battle.actions", "frame contains too many actions");
    }
    for (const std::string& action : input.action_ids) {
        if (action.empty())
            return Invalid("battle.action", "action id is empty");
    }
    if (input.opaque_payload.size() > kMaxBattlePayloadBytes) {
        return Invalid("battle.payload", "input payload exceeds the 64 KiB limit");
    }
    return std::nullopt;
}

std::optional<PluginError> ValidateBattleOutput(const BattleFrameOutput& output) {
    if (output.finished && output.result_key.empty()) {
        return Invalid("battle.result", "finished output requires a result key");
    }
    if (output.presentation_commands.size() > kMaxBattlePresentationCommands) {
        return Invalid("battle.presentation", "frame contains too many presentation commands");
    }
    for (const std::string& command : output.presentation_commands) {
        if (command.empty())
            return Invalid("battle.presentation", "presentation command is empty");
    }
    if (output.opaque_payload.size() > kMaxBattlePayloadBytes) {
        return Invalid("battle.payload", "output payload exceeds the 64 KiB limit");
    }
    return std::nullopt;
}

std::optional<PluginError> ValidateBattleSnapshot(const BattleSnapshot& snapshot) {
    if (snapshot.finished && snapshot.active) {
        return Invalid("battle.state", "finished snapshot cannot remain active");
    }
    if (snapshot.opaque_payload.size() > kMaxBattlePayloadBytes) {
        return Invalid("battle.payload", "snapshot payload exceeds the 64 KiB limit");
    }
    return std::nullopt;
}

} // namespace jrpgmaker::plugin
