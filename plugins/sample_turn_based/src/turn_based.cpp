#include "jrpgmaker/plugins/sample_turn_based/turn_based.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace jrpgmaker::plugins::sample_turn_based {
namespace {

plugin::PluginValidationResult
ValidateEncounterData(const plugin::PluginValidationContext& context) {
    const auto data = context.read_file("plugins/sample_turn_based/data/encounters_demo.json");
    if (!data)
        return {.issues = {data.error.value_or(plugin::PluginError{
                    "sample.turn_based.data", "encounter data could not be read", "data"})}};
    try {
        const std::string text(reinterpret_cast<const char*>(data.bytes.data()), data.bytes.size());
        const auto document = nlohmann::json::parse(text);
        if (!document.is_object() || document.value("schema", 0) != 1 ||
            !document.contains("encounters") || !document["encounters"].is_array() ||
            document["encounters"].empty()) {
            throw std::invalid_argument(
                "sample.turn_based encounter data requires schema 1 and encounters");
        }
        std::unordered_set<std::string> ids;
        for (const auto& encounter : document["encounters"]) {
            if (!encounter.is_object() || !encounter.contains("id") ||
                !encounter["id"].is_string() || encounter["id"].get<std::string>().empty() ||
                !encounter.contains("required_actions") ||
                !encounter["required_actions"].is_number_integer() ||
                encounter["required_actions"].get<int>() <= 0 ||
                !ids.insert(encounter["id"].get<std::string>()).second) {
                throw std::invalid_argument(
                    "sample.turn_based encounter requires unique id and positive required_actions");
            }
        }
    } catch (const std::exception& error) {
        return {.issues = {{"sample.turn_based.data", error.what(),
                            "plugins/sample_turn_based/data/encounters_demo.json"}}};
    }
    return {};
}

class Session final : public plugin::IBattleSession {
public:
    plugin::BattleAdvanceResult Advance(const plugin::BattleFrameInput& input) override {
        if (const auto error = plugin::ValidateBattleInput(input); error.has_value()) {
            return {.ok = false, .output = {}, .error = error};
        }
        if (std::find(input.action_ids.begin(), input.action_ids.end(), "extension.confirm") !=
            input.action_ids.end()) {
            ++steps_;
        }
        finished_ = input.cancel_requested || steps_ >= 2;
        return {.ok = true,
                .output = {.finished = finished_,
                           .result_key = finished_
                                             ? (input.cancel_requested ? "cancelled" : "turns_done")
                                             : "",
                           .presentation_commands = {{.id = finished_ ? "turn.close" : "turn.next",
                                                      .payload = {}}},
                           .opaque_payload = {}},
                .error = std::nullopt};
    }

    plugin::BattleSnapshot Snapshot() const override {
        return {.active = !finished_, .finished = finished_, .opaque_payload = {}};
    }

private:
    int steps_ = 0;
    bool finished_ = false;
};

} // namespace

plugin::PluginValidationResult
Adapter::ValidateData(const plugin::PluginValidationContext& context) const {
    return ValidateEncounterData(context);
}

plugin::BattleSessionCreateResult
Adapter::CreateSession(const plugin::BattleLaunchContext& context) {
    if (const auto error = plugin::ValidateBattleLaunch(context); error.has_value()) {
        return {.session = nullptr, .error = error};
    }
    return {.session = std::make_unique<Session>(), .error = std::nullopt};
}

} // namespace jrpgmaker::plugins::sample_turn_based
