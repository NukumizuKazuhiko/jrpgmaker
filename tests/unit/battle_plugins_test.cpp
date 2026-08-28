#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "jrpgmaker/domain/encounter.hpp"
#include "jrpgmaker/domain/event_runner.hpp"
#include "jrpgmaker/plugin/battle.hpp"
#include "jrpgmaker/plugins/sample_instant/instant.hpp"
#include "jrpgmaker/plugins/sample_turn_based/turn_based.hpp"

namespace {

class ThrowingBattleSession final : public jrpgmaker::plugin::IBattleSession {
public:
    jrpgmaker::plugin::BattleAdvanceResult
    Advance(const jrpgmaker::plugin::BattleFrameInput&) override {
        throw std::runtime_error("session failure");
    }

    jrpgmaker::plugin::BattleSnapshot Snapshot() const override { return {}; }
};

class ThrowingBattlePlugin final : public jrpgmaker::plugin::IBattlePlugin {
public:
    jrpgmaker::plugin::BattleSessionCreateResult
    CreateSession(const jrpgmaker::plugin::BattleLaunchContext&) override {
        throw std::runtime_error("create failure");
    }
};

} // namespace

TEST_CASE("instant and turn based samples use the same battle seam", "[plugin][battle][p5]") {
    jrpgmaker::plugins::sample_instant::Adapter instant;
    jrpgmaker::plugins::sample_turn_based::Adapter turn_based;

    auto instant_created = instant.CreateSession({.encounter_id = "demo", .opaque_payload = {}});
    auto turn_created = turn_based.CreateSession({.encounter_id = "demo", .opaque_payload = {}});
    REQUIRE(instant_created);
    REQUIRE(turn_created);

    auto instant_session = std::move(instant_created.session);
    auto turn_session = std::move(turn_created.session);
    REQUIRE(instant_session
                ->Advance({.delta_seconds = 0.0,
                           .action_ids = {"extension.confirm"},
                           .opaque_payload = {},
                           .cancel_requested = false})
                .output.result_key == "resolved");
    REQUIRE(turn_session
                ->Advance({.delta_seconds = 0.0,
                           .action_ids = {"extension.confirm"},
                           .opaque_payload = {},
                           .cancel_requested = false})
                .output.finished == false);
    REQUIRE(turn_session
                ->Advance({.delta_seconds = 0.0,
                           .action_ids = {"extension.confirm"},
                           .opaque_payload = {},
                           .cancel_requested = false})
                .output.result_key == "turns_done");
}

TEST_CASE("battle sample sessions reject invalid launch contexts", "[plugin][battle][p5]") {
    jrpgmaker::plugins::sample_instant::Adapter instant;
    const auto created = instant.CreateSession({});
    REQUIRE_FALSE(created);
    REQUIRE(created.error.has_value());
    REQUIRE(created.error->code == "battle.encounter");
}

TEST_CASE("battle host wrappers isolate plugin exceptions", "[plugin][battle][p11]") {
    ThrowingBattlePlugin plugin;
    const auto created = jrpgmaker::plugin::CreateBattleSession(
        plugin, {.encounter_id = "demo", .opaque_payload = {}});
    REQUIRE_FALSE(created);
    REQUIRE(created.error.has_value());
    REQUIRE(created.error->code == "battle.session_create_exception");

    ThrowingBattleSession session;
    const auto advanced = jrpgmaker::plugin::AdvanceBattleSession(
        session,
        {.delta_seconds = 0.0, .action_ids = {}, .opaque_payload = {}, .cancel_requested = false});
    REQUIRE_FALSE(advanced);
    REQUIRE(advanced.error.has_value());
    REQUIRE(advanced.error->code == "battle.session_advance_exception");
}

TEST_CASE("sample battle validators reject malformed private data", "[plugin][p9]") {
    const jrpgmaker::plugin::PluginManifest manifest{.schema = 1,
                                                     .id = "sample.instant",
                                                     .type = jrpgmaker::plugin::PluginType::kBattle,
                                                     .version = 1,
                                                     .engine_contract =
                                                         jrpgmaker::plugin::kPluginEngineContract,
                                                     .data_roots = {},
                                                     .capabilities = {}};
    const jrpgmaker::plugin::PluginValidationContext context{
        .manifest = manifest, .read_file = [](std::string_view) {
            const std::string invalid = R"json({"schema":1,"encounters":[{}]})json";
            std::vector<std::byte> bytes;
            bytes.reserve(invalid.size());
            for (const char value : invalid)
                bytes.push_back(static_cast<std::byte>(value));
            return jrpgmaker::plugin::PluginDataReadResult{.bytes = std::move(bytes), .error = {}};
        }};
    const jrpgmaker::plugins::sample_instant::Adapter plugin;
    const auto result = plugin.ValidateData(context);
    REQUIRE(result.issues.size() == 1);
    REQUIRE(result.issues.front().code == "sample.instant.data");
}

TEST_CASE("encounter result enters EventRunner without event bus reentry",
          "[domain][plugin][battle][p5]") {
    const auto events = jrpgmaker::domain::ParseEventScriptText(R"json(
        {"schema":1,"events":[
          {"id":"reward","instructions":[{"op":"dialog","speaker":"guide","text_key":"reward.text"}]}
        ]}
    )json");
    const auto points = jrpgmaker::domain::ParseEncounterPoints(nlohmann::json::parse(R"json(
        {"schema":1,"encounters":[{"id":"point","position":[0,0,0],"radius":1,
          "encounter_id":"demo","results":{"resolved":"reward"}}]}
    )json"));
    jrpgmaker::core::EventBus bus;
    std::vector<jrpgmaker::domain::EncounterRequested> requests;
    bus.Subscribe<jrpgmaker::domain::EncounterRequested>(
        [&requests](const auto& request) { requests.push_back(request); });
    jrpgmaker::domain::EncounterSystem encounters_system(points, bus);
    encounters_system.Update({0.0f, 0.0f, 0.0f});
    REQUIRE(requests.size() == 1);

    jrpgmaker::plugins::sample_instant::Adapter plugin;
    auto session_result =
        plugin.CreateSession({.encounter_id = requests.front().encounter_id, .opaque_payload = {}});
    REQUIRE(session_result);
    auto battle = std::move(session_result.session);
    const auto battle_output = battle->Advance({.delta_seconds = 0.0,
                                                .action_ids = {"extension.confirm"},
                                                .opaque_payload = {},
                                                .cancel_requested = false});
    REQUIRE(battle_output.ok);
    REQUIRE(battle_output.output.finished);
    REQUIRE(battle_output.output.result_key == "resolved");

    jrpgmaker::domain::FlagStore flags;
    jrpgmaker::domain::EventRunner runner(events, flags, bus);
    REQUIRE(runner.Start(requests.front().result_event_ids.at(battle_output.output.result_key)));
    runner.Tick(0.0);
    REQUIRE(runner.IsDialogPending());
    REQUIRE(runner.active_event_id() == "reward");
    runner.AdvanceDialog();
    REQUIRE(runner.IsFinished());
}
