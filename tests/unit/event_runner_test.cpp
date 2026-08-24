#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/core/event_bus.hpp"
#include "jrpgmaker/domain/event_runner.hpp"
#include "jrpgmaker/domain/event_script.hpp"
#include "jrpgmaker/domain/flag_store.hpp"

using jrpgmaker::core::EventBus;
using jrpgmaker::domain::DialogRequested;
using jrpgmaker::domain::EventRunner;
using jrpgmaker::domain::EventScript;
using jrpgmaker::domain::FlagStore;
using jrpgmaker::domain::ParseEventScript;

namespace {

EventScript MakeScript(const char* text) {
    return ParseEventScript(nlohmann::json::parse(text));
}

} // namespace

TEST_CASE("event runner executes set_flag and finishes", "[domain][event_runner]") {
    FlagStore flags;
    EventBus bus;
    EventScript script = MakeScript(R"({
        "schema": 1,
        "events": [{"id": "e", "instructions": [{"op": "set_flag", "flag": "quest.done"}]}]
    })");
    EventRunner runner(script, flags, bus);

    REQUIRE(runner.Start("e"));
    REQUIRE(runner.IsActive());
    runner.Tick(0.0);
    REQUIRE(runner.IsFinished());
    REQUIRE_FALSE(runner.IsActive());
    REQUIRE(flags.Get("quest.done"));
}

TEST_CASE("event runner publishes dialog requests", "[domain][event_runner]") {
    FlagStore flags;
    EventBus bus;
    std::vector<DialogRequested> dialogs;
    bus.Subscribe<DialogRequested>(
        [&](const DialogRequested& request) { dialogs.push_back(request); });

    EventScript script = MakeScript(R"({
        "schema": 1,
        "events": [{"id": "talk", "instructions": [
            {"op": "dialog", "speaker": "alice", "text_key": "hi"},
            {"op": "dialog", "speaker": "bob", "text_key": "bye"}
        ]}]
    })");
    EventRunner runner(script, flags, bus);

    runner.Start("talk");
    runner.Tick(0.0);
    REQUIRE(runner.IsFinished());
    REQUIRE(dialogs.size() == 2);
    REQUIRE(dialogs[0].speaker == "alice");
    REQUIRE(dialogs[0].text_key == "hi");
    REQUIRE(dialogs[1].speaker == "bob");
}

TEST_CASE("event runner blocks on wait until time elapses", "[domain][event_runner]") {
    FlagStore flags;
    EventBus bus;
    EventScript script = MakeScript(R"({
        "schema": 1,
        "events": [{"id": "w", "instructions": [
            {"op": "wait", "seconds": 1.0},
            {"op": "set_flag", "flag": "after.wait"}
        ]}]
    })");
    EventRunner runner(script, flags, bus);

    runner.Start("w");
    runner.Tick(0.4);
    REQUIRE(runner.IsActive());
    REQUIRE_FALSE(runner.IsFinished());
    REQUIRE_FALSE(flags.Get("after.wait"));

    runner.Tick(0.4);
    REQUIRE(runner.IsActive());

    runner.Tick(0.4);
    REQUIRE(runner.IsFinished());
    REQUIRE(flags.Get("after.wait"));
}

TEST_CASE("event runner branches on flag state", "[domain][event_runner]") {
    FlagStore flags;
    EventBus bus;
    std::vector<std::string> dialogs;
    bus.Subscribe<DialogRequested>(
        [&](const DialogRequested& request) { dialogs.push_back(request.text_key); });

    EventScript script = MakeScript(R"({
        "schema": 1,
        "events": [{"id": "b", "instructions": [{
            "op": "branch", "flag": "npc.met",
            "if_set": [{"op": "dialog", "speaker": "alice", "text_key": "again"}],
            "if_not_set": [{"op": "dialog", "speaker": "alice", "text_key": "first"}]
        }]}]
    })");
    EventRunner runner(script, flags, bus);

    runner.Start("b");
    runner.Tick(0.0);
    REQUIRE(runner.IsFinished());
    REQUIRE(dialogs.size() == 1);
    REQUIRE(dialogs[0] == "first");

    dialogs.clear();
    flags.Set("npc.met", true);
    runner.Start("b");
    runner.Tick(0.0);
    REQUIRE(dialogs.size() == 1);
    REQUIRE(dialogs[0] == "again");
}

TEST_CASE("event runner rejects nested wait inside a branch", "[domain][event_runner]") {
    FlagStore flags;
    EventBus bus;
    EventScript script = MakeScript(R"({
        "schema": 1,
        "events": [{"id": "nested", "instructions": [{
            "op": "branch", "flag": "any",
            "if_set": [{"op": "wait", "seconds": 1.0}],
            "if_not_set": []
        }]}]
    })");
    EventRunner runner(script, flags, bus);

    flags.Set("any", true);
    runner.Start("nested");
    REQUIRE_THROWS_AS(runner.Tick(0.0), std::logic_error);
}

TEST_CASE("event runner start with unknown event id returns false", "[domain][event_runner]") {
    FlagStore flags;
    EventBus bus;
    EventScript script = MakeScript(R"({"schema": 1, "events": []})");
    EventRunner runner(script, flags, bus);

    REQUIRE_FALSE(runner.Start("missing"));
    REQUIRE_FALSE(runner.IsActive());
}

TEST_CASE("event runner start while active throws", "[domain][event_runner]") {
    FlagStore flags;
    EventBus bus;
    EventScript script = MakeScript(R"({
        "schema": 1,
        "events": [{"id": "e", "instructions": [{"op": "wait", "seconds": 5.0}]}]
    })");
    EventRunner runner(script, flags, bus);

    runner.Start("e");
    REQUIRE_THROWS_AS(runner.Start("e"), std::logic_error);
}