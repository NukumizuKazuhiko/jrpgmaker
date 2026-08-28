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
    std::size_t presentation_bytes = 0;
    for (const PresentationCommand& command : output.presentation_commands) {
        if (command.id.empty())
            return Invalid("battle.presentation", "presentation command is empty");
        if (presentation_bytes > kMaxBattlePayloadBytes ||
            command.payload.size() > kMaxBattlePayloadBytes - presentation_bytes) {
            return Invalid("battle.presentation",
                           "presentation command payload exceeds the 64 KiB limit");
        }
        presentation_bytes += command.payload.size();
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

BattleSessionCreateResult CreateBattleSession(IBattlePlugin& plugin,
                                              const BattleLaunchContext& context) {
    if (const auto error = ValidateBattleLaunch(context); error.has_value())
        return {.session = nullptr, .error = error};
    try {
        auto created = plugin.CreateSession(context);
        if (!created)
            return {.session = nullptr,
                    .error = created.error.value_or(PluginError{
                        "battle.session_create_failed", "battle plugin did not create a session",
                        context.encounter_id})};
        return created;
    } catch (...) {
        return {.session = nullptr,
                .error = PluginError{"battle.session_create_exception",
                                     "battle plugin session creation threw an exception",
                                     context.encounter_id}};
    }
}

BattleAdvanceResult AdvanceBattleSession(IBattleSession& session, const BattleFrameInput& input) {
    if (const auto error = ValidateBattleInput(input); error.has_value())
        return {.ok = false, .output = {}, .error = error};
    try {
        const auto advanced = session.Advance(input);
        if (!advanced)
            return {.ok = false,
                    .output = advanced.output,
                    .error = advanced.error.value_or(PluginError{
                        "battle.session_advance_failed", "battle plugin did not advance", ""})};
        if (const auto error = ValidateBattleOutput(advanced.output); error.has_value())
            return {.ok = false, .output = advanced.output, .error = error};
        return advanced;
    } catch (...) {
        return {.ok = false,
                .output = {},
                .error = PluginError{"battle.session_advance_exception",
                                     "battle plugin session advance threw an exception", ""}};
    }
}

} // namespace jrpgmaker::plugin
