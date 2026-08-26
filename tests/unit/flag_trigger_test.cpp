#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/core/event_bus.hpp"
#include "jrpgmaker/domain/event_runner.hpp"
#include "jrpgmaker/domain/flag_store.hpp"
#include "jrpgmaker/domain/flag_trigger.hpp"

using jrpgmaker::core::EventBus;
using jrpgmaker::domain::FlagStore;
using jrpgmaker::domain::FlagTrigger;
using jrpgmaker::domain::FlagTriggerSystem;
using jrpgmaker::domain::FlagTriggerTable;
using jrpgmaker::domain::ParseFlagTriggers;

namespace {

FlagTriggerTable MakeTriggers(const char* text) {
    return ParseFlagTriggers(nlohmann::json::parse(text));
}

} // namespace

TEST_CASE("flag trigger parses a well-formed table", "[domain][flag_trigger]") {
    const FlagTriggerTable table = MakeTriggers(R"({
        "schema": 1,
        "triggers": [
            {"flag": "a.b", "target_event_id": "evt_a"},
            {"flag": "c", "target_event_id": "evt_c"}
        ]
    })");
    REQUIRE(table.triggers.size() == 2);
    REQUIRE(table.triggers[0].flag == "a.b");
    REQUIRE(table.triggers[0].target_event_id == "evt_a");
    REQUIRE(table.triggers[1].flag == "c");
}

TEST_CASE("flag trigger rejects duplicate flag bindings", "[domain][flag_trigger]") {
    REQUIRE_THROWS_AS(MakeTriggers(R"({
        "schema": 1,
        "triggers": [
            {"flag": "a", "target_event_id": "e1"},
            {"flag": "a", "target_event_id": "e2"}
        ]
    })"),
                      std::invalid_argument);
}

TEST_CASE("flag trigger rejects empty flag or target event id", "[domain][flag_trigger]") {
    REQUIRE_THROWS_AS(MakeTriggers(R"({
        "schema": 1,
        "triggers": [{"flag": "", "target_event_id": "e"}]
    })"),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(MakeTriggers(R"({
        "schema": 1,
        "triggers": [{"flag": "a", "target_event_id": ""}]
    })"),
                      std::invalid_argument);
}

TEST_CASE("flag trigger fires once on a rising edge", "[domain][flag_trigger]") {
    // Regression coverage for indexed trigger lookup on every supported STL.
    EventBus bus;
    std::vector<std::string> fired;
    const FlagTriggerTable table = MakeTriggers(R"({
        "schema": 1,
        "triggers": [{"flag": "quest.done", "target_event_id": "reward"}]
    })");
    FlagTriggerSystem system(table, bus,
                             [&](const std::string& target) { fired.push_back(target); });

    bus.Publish(jrpgmaker::domain::FlagChanged{"quest.done", true});
    bus.Publish(jrpgmaker::domain::FlagChanged{"quest.done", true});
    REQUIRE(fired == std::vector<std::string>{"reward"});
}

TEST_CASE("flag trigger does not fire on a falling edge", "[domain][flag_trigger]") {
    EventBus bus;
    std::vector<std::string> fired;
    const FlagTriggerTable table = MakeTriggers(R"({
        "schema": 1,
        "triggers": [{"flag": "quest.done", "target_event_id": "reward"}]
    })");
    FlagTriggerSystem system(table, bus,
                             [&](const std::string& target) { fired.push_back(target); });

    bus.Publish(jrpgmaker::domain::FlagChanged{"quest.done", false});
    REQUIRE(fired.empty());
}

TEST_CASE("flag trigger re-arms after returning to false", "[domain][flag_trigger]") {
    EventBus bus;
    std::vector<std::string> fired;
    const FlagTriggerTable table = MakeTriggers(R"({
        "schema": 1,
        "triggers": [{"flag": "quest.done", "target_event_id": "reward"}]
    })");
    FlagTriggerSystem system(table, bus,
                             [&](const std::string& target) { fired.push_back(target); });

    bus.Publish(jrpgmaker::domain::FlagChanged{"quest.done", true});
    bus.Publish(jrpgmaker::domain::FlagChanged{"quest.done", false});
    bus.Publish(jrpgmaker::domain::FlagChanged{"quest.done", true});
    REQUIRE(fired == std::vector<std::string>{"reward", "reward"});
}

TEST_CASE("flag trigger ignores flags without a binding", "[domain][flag_trigger]") {
    EventBus bus;
    std::vector<std::string> fired;
    const FlagTriggerTable table = MakeTriggers(R"({
        "schema": 1,
        "triggers": [{"flag": "bound", "target_event_id": "reward"}]
    })");
    FlagTriggerSystem system(table, bus,
                             [&](const std::string& target) { fired.push_back(target); });

    bus.Publish(jrpgmaker::domain::FlagChanged{"unbound", true});
    REQUIRE(fired.empty());
}

TEST_CASE("flag trigger starts the target event on the runner (end-to-end wiring)",
          "[domain][flag_trigger][integration]") {
    EventBus bus;
    FlagStore flags;
    const jrpgmaker::domain::EventScript script =
        jrpgmaker::domain::ParseEventScript(nlohmann::json::parse(R"({
            "schema": 1,
            "events": [
                {"id": "quest", "instructions": [{"op": "set_flag", "flag": "quest.done", "value": true}]},
                {"id": "reward", "instructions": [{"op": "dialog", "speaker": "king",
                                                   "text_key": "reward.line"}]
                }
            ]
        })"));
    jrpgmaker::domain::EventRunner runner(script, flags, bus);

    // The trigger fires synchronously inside Tick (FlagChanged is published
    // while the quest event is still completing, so runner.Start would throw).
    // The host therefore queues the target and starts it at the next event
    // boundary - the standard game-loop pattern.
    std::vector<std::string> pending_starts;
    const FlagTriggerTable table = MakeTriggers(R"({
        "schema": 1,
        "triggers": [{"flag": "quest.done", "target_event_id": "reward"}]
    })");
    FlagTriggerSystem system(table, bus,
                             [&](const std::string& target) { pending_starts.push_back(target); });

    REQUIRE(runner.Start("quest"));
    runner.Tick(0.0);
    REQUIRE(runner.IsFinished());
    REQUIRE(pending_starts == std::vector<std::string>{"reward"});

    // Next event boundary: host consumes the queue.
    for (const std::string& target : pending_starts) {
        REQUIRE(runner.Start(target));
    }
    pending_starts.clear();
    runner.Tick(0.0);
    REQUIRE(runner.IsDialogPending());
    runner.AdvanceDialog();
    REQUIRE(runner.IsFinished());
}

TEST_CASE("flag trigger parses the committed demo trigger table", "[domain][flag_trigger][data]") {
#ifndef JRPGMAKER_ASSET_DIR
#error "JRPGMAKER_ASSET_DIR must be defined by the build"
#endif
    const std::filesystem::path triggers_path =
        std::filesystem::path(JRPGMAKER_ASSET_DIR) / "data" / "triggers_demo.json";
    std::ifstream triggers_file(triggers_path);
    REQUIRE(triggers_file.is_open());
    const FlagTriggerTable table = ParseFlagTriggers(nlohmann::json::parse(triggers_file));
    REQUIRE(table.triggers.size() == 2);
    REQUIRE(table.triggers[0].flag == "alice.quest.accepted");
    REQUIRE(table.triggers[0].target_event_id == "alice_reward");
    REQUIRE(table.triggers[1].flag == "chest.west.opened");
    REQUIRE(table.triggers[1].target_event_id == "chest_west_echo");

    // Every trigger target must name an event that actually exists in the demo
    // event script, otherwise firing the trigger silently no-ops (Start returns
    // false). This keeps the committed data files self-consistent.
    const std::filesystem::path events_path =
        std::filesystem::path(JRPGMAKER_ASSET_DIR) / "data" / "events_demo.json";
    std::ifstream events_file(events_path);
    REQUIRE(events_file.is_open());
    const jrpgmaker::domain::EventScript script =
        jrpgmaker::domain::ParseEventScript(nlohmann::json::parse(events_file));
    for (const FlagTrigger& trigger : table.triggers) {
        const auto exists = std::find_if(script.events.begin(), script.events.end(),
                                         [&](const jrpgmaker::domain::Event& event) {
                                             return event.id == trigger.target_event_id;
                                         });
        REQUIRE(exists != script.events.end());
    }
}
