#include "jrpgmaker/plugins/sample_instant/instant.hpp"

#include <algorithm>
#include <memory>

namespace jrpgmaker::plugins::sample_instant {
namespace {

class Session final : public plugin::IBattleSession {
public:
    plugin::BattleAdvanceResult Advance(const plugin::BattleFrameInput& input) override {
        if (const auto error = plugin::ValidateBattleInput(input); error.has_value()) {
            return {.ok = false, .output = {}, .error = error};
        }
        const bool resolve =
            input.cancel_requested || std::find(input.action_ids.begin(), input.action_ids.end(),
                                                "extension.confirm") != input.action_ids.end();
        finished_ = resolve;
        return {.ok = true,
                .output = {.finished = finished_,
                           .result_key =
                               finished_ ? (input.cancel_requested ? "cancelled" : "resolved") : "",
                           .presentation_commands = {finished_ ? "battle.close" : "battle.wait"},
                           .opaque_payload = {}},
                .error = std::nullopt};
    }

    plugin::BattleSnapshot Snapshot() const override {
        return {.active = !finished_, .finished = finished_, .opaque_payload = {}};
    }

private:
    bool finished_ = false;
};

} // namespace

plugin::BattleSessionCreateResult
Adapter::CreateSession(const plugin::BattleLaunchContext& context) {
    if (const auto error = plugin::ValidateBattleLaunch(context); error.has_value()) {
        return {.session = nullptr, .error = error};
    }
    return {.session = std::make_unique<Session>(), .error = std::nullopt};
}

} // namespace jrpgmaker::plugins::sample_instant
