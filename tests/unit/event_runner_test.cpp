#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
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

TEST_CASE("event runner blocks on dialog until advanced", "[domain][event_runner]") {
    FlagStore flags;
    EventBus bus;
    std::vector<DialogRequested> dialogs;
    bus.Subscribe<DialogRequested>(
        [&](const DialogRequested& request) { dialogs.push_back(request); });

    EventScript script = MakeScript(R"({
        "schema": 1,
        "events": [{"id": "talk", "instructions": [
            {"op": "dialog", "speaker": "alice", "text_key": "hi"},
            {"op": "set_flag", "flag": "after.talk"}
        ]}]
    })");
    EventRunner runner(script, flags, bus);

    runner.Start("talk");
    runner.Tick(0.0);
    REQUIRE(runner.IsDialogPending());
    REQUIRE_FALSE(runner.IsFinished());
    REQUIRE_FALSE(flags.Get("after.talk"));
    REQUIRE(dialogs.size() == 1);
    REQUIRE(dialogs[0].speaker == "alice");
    REQUIRE(dialogs[0].text_key == "hi");
    REQUIRE(dialogs[0].options.empty());

    runner.AdvanceDialog();
    REQUIRE_FALSE(runner.IsDialogPending());
    REQUIRE(runner.IsFinished());
    REQUIRE(flags.Get("after.talk"));
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

// The wait timer must consume exactly its share of a tick and pass the leftover
// time into the next instruction, so wall-clock time is never double-spent:
// two consecutive wait(0.5) complete after a cumulative 1.0s of ticks (not
// faster from the first tick being fed to both waits, nor slower from the
// leftover being discarded).
TEST_CASE("event runner consecutive waits elapse in exact cumulative time",
          "[domain][event_runner]") {
    FlagStore flags;
    EventBus bus;
    EventScript script = MakeScript(R"({
        "schema": 1,
        "events": [{"id": "w", "instructions": [
            {"op": "wait", "seconds": 0.5},
            {"op": "wait", "seconds": 0.5},
            {"op": "set_flag", "flag": "after.waits"}
        ]}]
    })");
    EventRunner runner(script, flags, bus);

    runner.Start("w");
    const double tick_values[] = {0.4, 0.3, 0.2};
    double elapsed = 0.0;
    for (const double tick : tick_values) {
        runner.Tick(tick);
        elapsed += tick;
        REQUIRE_FALSE(flags.Get("after.waits"));
    }
    REQUIRE(runner.IsActive()); // 0.9s elapsed, 0.1s of wait(0.5) remains

    runner.Tick(0.1);
    elapsed += 0.1;
    REQUIRE(runner.IsFinished());
    REQUIRE(flags.Get("after.waits"));
    REQUIRE(elapsed == Catch::Approx(1.0));
}

// A single wait whose seconds fit entirely inside one tick must not hand the
// whole tick to the following instruction: the leftover delta advances the
// next wait only by its share.
TEST_CASE("event runner wait elapsing inside one tick passes only its leftover",
          "[domain][event_runner]") {
    FlagStore flags;
    EventBus bus;
    EventScript script = MakeScript(R"({
        "schema": 1,
        "events": [{"id": "w", "instructions": [
            {"op": "wait", "seconds": 0.2},
            {"op": "wait", "seconds": 0.5},
            {"op": "set_flag", "flag": "after.waits"}
        ]}]
    })");
    EventRunner runner(script, flags, bus);

    // A single 0.5s tick: the 0.2s wait elapses and passes 0.3s leftover to the
    // 0.5s wait, which still blocks with 0.2s remaining.
    runner.Start("w");
    runner.Tick(0.5);
    REQUIRE(runner.IsActive());
    REQUIRE_FALSE(flags.Get("after.waits"));

    runner.Tick(0.2);
    REQUIRE(runner.IsFinished());
    REQUIRE(flags.Get("after.waits"));
}

TEST_CASE("event runner branches on flag state", "[domain][event_runner]") {
    FlagStore flags;
    EventBus bus;
    EventScript script = MakeScript(R"({
        "schema": 1,
        "events": [{"id": "b", "instructions": [
            {"op": "branch", "flag": "npc.met",
             "if_set": [{"op": "set_flag", "flag": "chose.met"}],
             "if_not_set": [{"op": "set_flag", "flag": "chose.first"}]}
        ]}]
    })");
    EventRunner runner(script, flags, bus);

    runner.Start("b");
    runner.Tick(0.0);
    REQUIRE(runner.IsFinished());
    REQUIRE(flags.Get("chose.first"));
    REQUIRE_FALSE(flags.Get("chose.met"));

    // A fresh store: the runner leaves no cross-run state beyond FlagStore,
    // which is deliberately persistent across events in the same session.
    FlagStore fresh_flags;
    fresh_flags.Set("npc.met", true);
    EventRunner second_run(script, fresh_flags, bus);
    second_run.Start("b");
    second_run.Tick(0.0);
    REQUIRE(fresh_flags.Get("chose.met"));
    REQUIRE_FALSE(fresh_flags.Get("chose.first"));
}

TEST_CASE("event runner resolves choice and runs the picked option", "[domain][event_runner]") {
    FlagStore flags;
    EventBus bus;
    std::vector<DialogRequested> dialogs;
    bus.Subscribe<DialogRequested>(
        [&](const DialogRequested& request) { dialogs.push_back(request); });

    EventScript script = MakeScript(R"({
        "schema": 1,
        "events": [{"id": "c", "instructions": [
            {
                "op": "choice", "prompt_text_key": "ask.help",
                "options": [
                    {"text_key": "opt.yes", "instructions": [{"op": "set_flag", "flag": "help.yes"}]},
                    {"text_key": "opt.no", "instructions": [{"op": "set_flag", "flag": "help.no"}]}
                ]
            },
            {"op": "set_flag", "flag": "after.choice"}
        ]}]
    })");
    EventRunner runner(script, flags, bus);

    runner.Start("c");
    runner.Tick(0.0);
    REQUIRE(runner.IsDialogPending());
    REQUIRE_FALSE(runner.IsFinished());
    REQUIRE(dialogs.size() == 1);
    REQUIRE(dialogs[0].text_key == "ask.help");
    REQUIRE(dialogs[0].options.size() == 2);
    REQUIRE(dialogs[0].options[0].text_key == "opt.yes");
    REQUIRE(dialogs[0].options[1].text_key == "opt.no");

    runner.AdvanceDialog(1);
    REQUIRE(runner.IsFinished());
    REQUIRE(flags.Get("help.no"));
    REQUIRE_FALSE(flags.Get("help.yes"));
    REQUIRE(flags.Get("after.choice"));
}

TEST_CASE("event runner rejects advance without a pending dialog", "[domain][event_runner]") {
    FlagStore flags;
    EventBus bus;
    EventScript script = MakeScript(R"({
        "schema": 1,
        "events": [{"id": "e", "instructions": [{"op": "set_flag", "flag": "x"}]}]
    })");
    EventRunner runner(script, flags, bus);

    runner.Start("e");
    runner.Tick(0.0);
    REQUIRE(runner.IsFinished());
    REQUIRE_THROWS_AS(runner.AdvanceDialog(), std::logic_error);
}

TEST_CASE("event runner rejects out-of-range choice index", "[domain][event_runner]") {
    FlagStore flags;
    EventBus bus;
    EventScript script = MakeScript(R"({
        "schema": 1,
        "events": [{"id": "c", "instructions": [
            {
                "op": "choice", "prompt_text_key": "ask",
                "options": [{"text_key": "opt.a", "instructions": []}]
            }
        ]}]
    })");
    EventRunner runner(script, flags, bus);

    runner.Start("c");
    runner.Tick(0.0);
    REQUIRE(runner.IsDialogPending());
    REQUIRE_THROWS_AS(runner.AdvanceDialog(5), std::out_of_range);
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

TEST_CASE("event runner rejects dialog inside a branch", "[domain][event_runner]") {
    FlagStore flags;
    EventBus bus;
    EventScript script = MakeScript(R"({
        "schema": 1,
        "events": [{"id": "nested", "instructions": [{
            "op": "branch", "flag": "any",
            "if_set": [{"op": "dialog", "speaker": "alice", "text_key": "hi"}],
            "if_not_set": []
        }]}]
    })");
    EventRunner runner(script, flags, bus);

    flags.Set("any", true);
    runner.Start("nested");
    REQUIRE_THROWS_AS(runner.Tick(0.0), std::logic_error);
}

TEST_CASE("event runner executes the committed demo data file", "[domain][event_runner][data]") {
#ifndef JRPGMAKER_ASSET_DIR
#error "JRPGMAKER_ASSET_DIR must be defined by the build"
#endif
    const std::filesystem::path path =
        std::filesystem::path(JRPGMAKER_ASSET_DIR) / "data" / "events_demo.json";
    std::ifstream file(path);
    REQUIRE(file.is_open());
    const EventScript script = ParseEventScript(nlohmann::json::parse(file));

    FlagStore flags;
    EventBus bus;
    std::vector<DialogRequested> dialogs;
    bus.Subscribe<DialogRequested>(
        [&](const DialogRequested& request) { dialogs.push_back(request); });
    EventRunner runner(script, flags, bus);

    // meet_alice: branch (set_flag) then a top-level blocking dialog.
    REQUIRE(runner.Start("meet_alice"));
    runner.Tick(0.0);
    REQUIRE(runner.IsDialogPending());
    REQUIRE(dialogs.size() == 1);
    REQUIRE(dialogs[0].speaker == "alice");
    REQUIRE(dialogs[0].text_key == "alice.greeting.first");
    runner.AdvanceDialog();
    REQUIRE(runner.IsFinished());
    REQUIRE(flags.Get("alice.met"));

    // alice_ask_help: choice with two options, then a top-level dialog.
    REQUIRE(runner.Start("alice_ask_help"));
    runner.Tick(0.0);
    REQUIRE(runner.IsDialogPending());
    REQUIRE(dialogs.size() == 2);
    REQUIRE(dialogs[1].text_key == "alice.ask.help");
    REQUIRE(dialogs[1].options.size() == 2);
    runner.AdvanceDialog(0);
    REQUIRE(runner.IsDialogPending());
    REQUIRE(dialogs.size() == 3);
    REQUIRE(dialogs[2].text_key == "alice.help.reply");
    runner.AdvanceDialog();
    REQUIRE(runner.IsFinished());
    REQUIRE(flags.Get("alice.quest.accepted"));

    // chest_west: plain dialog then a set_flag.
    REQUIRE(runner.Start("chest_west"));
    runner.Tick(0.0);
    REQUIRE(runner.IsDialogPending());
    runner.AdvanceDialog();
    REQUIRE(runner.IsFinished());
    REQUIRE(flags.Get("chest.west.opened"));
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

// --- Change-detection projection sync v1 (A3): the runner broadcasts dirty
// domain-state changes on the event bus. Presentation subscribes to
// FlagChanged / DialogRequested / EventStarted / EventFinished instead of
// reading FlagStore directly (docs/01 owner map).

TEST_CASE("event runner broadcasts FlagChanged on set_flag", "[domain][event_runner][projection]") {
    FlagStore flags;
    EventBus bus;
    std::vector<jrpgmaker::domain::FlagChanged> changes;
    bus.Subscribe<jrpgmaker::domain::FlagChanged>(
        [&](const jrpgmaker::domain::FlagChanged& change) { changes.push_back(change); });
    EventScript script = MakeScript(R"({
        "schema": 1,
        "events": [{"id": "e", "instructions": [
            {"op": "set_flag", "flag": "quest.done", "value": true},
            {"op": "set_flag", "flag": "quest.done", "value": false}
        ]}]
    })");
    EventRunner runner(script, flags, bus);

    runner.Start("e");
    runner.Tick(0.0);
    REQUIRE(changes.size() == 2);
    REQUIRE(changes[0].flag == "quest.done");
    REQUIRE(changes[0].value);
    REQUIRE_FALSE(changes[1].value);
}

TEST_CASE("event runner broadcasts FlagChanged from choice option sequences",
          "[domain][event_runner][projection]") {
    FlagStore flags;
    EventBus bus;
    std::vector<jrpgmaker::domain::FlagChanged> changes;
    bus.Subscribe<jrpgmaker::domain::FlagChanged>(
        [&](const jrpgmaker::domain::FlagChanged& change) { changes.push_back(change); });
    EventScript script = MakeScript(R"({
        "schema": 1,
        "events": [{"id": "e", "instructions": [{
            "op": "choice", "prompt_text_key": "ask",
            "options": [
                {"text_key": "yes", "instructions": [{"op": "set_flag", "flag": "quest.accepted"}]}
            ]
        }]}]
    })");
    EventRunner runner(script, flags, bus);

    runner.Start("e");
    runner.Tick(0.0);
    REQUIRE(runner.IsDialogPending());
    runner.AdvanceDialog(0);
    REQUIRE(runner.IsFinished());
    REQUIRE(changes.size() == 1);
    REQUIRE(changes[0].flag == "quest.accepted");
    REQUIRE(changes[0].value);
}

TEST_CASE("event runner broadcasts event lifecycle projections",
          "[domain][event_runner][projection]") {
    FlagStore flags;
    EventBus bus;
    std::vector<std::string> started;
    std::vector<std::string> finished;
    bus.Subscribe<jrpgmaker::domain::EventStarted>(
        [&](const jrpgmaker::domain::EventStarted& event) { started.push_back(event.event_id); });
    bus.Subscribe<jrpgmaker::domain::EventFinished>(
        [&](const jrpgmaker::domain::EventFinished& event) { finished.push_back(event.event_id); });
    EventScript script = MakeScript(R"({
        "schema": 1,
        "events": [{"id": "e", "instructions": [{"op": "set_flag", "flag": "a"}]}]
    })");
    EventRunner runner(script, flags, bus);

    runner.Start("e");
    REQUIRE(started == std::vector<std::string>{"e"});
    runner.Tick(0.0);
    REQUIRE(runner.IsFinished());
    REQUIRE(finished == std::vector<std::string>{"e"});
}

TEST_CASE("event runner does not broadcast lifecycle projections for unknown event id",
          "[domain][event_runner][projection]") {
    FlagStore flags;
    EventBus bus;
    std::vector<std::string> started;
    bus.Subscribe<jrpgmaker::domain::EventStarted>(
        [&](const jrpgmaker::domain::EventStarted& event) { started.push_back(event.event_id); });
    EventScript script = MakeScript(R"({"schema": 1, "events": []})");
    EventRunner runner(script, flags, bus);

    REQUIRE_FALSE(runner.Start("missing"));
    REQUIRE(started.empty());
}

TEST_CASE("event runner broadcasts FlagChanged before dialog blocks",
          "[domain][event_runner][projection]") {
    FlagStore flags;
    EventBus bus;
    std::vector<jrpgmaker::domain::FlagChanged> changes;
    std::vector<jrpgmaker::domain::DialogRequested> dialogs;
    bus.Subscribe<jrpgmaker::domain::FlagChanged>(
        [&](const jrpgmaker::domain::FlagChanged& change) { changes.push_back(change); });
    bus.Subscribe<jrpgmaker::domain::DialogRequested>(
        [&](const jrpgmaker::domain::DialogRequested& dialog) { dialogs.push_back(dialog); });
    EventScript script = MakeScript(R"({
        "schema": 1,
        "events": [{"id": "e", "instructions": [
            {"op": "set_flag", "flag": "alice.met", "value": true},
            {"op": "dialog", "speaker": "alice", "text_key": "hello"}
        ]}]
    })");
    EventRunner runner(script, flags, bus);

    runner.Start("e");
    runner.Tick(0.0);
    REQUIRE(runner.IsDialogPending());
    REQUIRE(changes.size() == 1);
    REQUIRE(changes[0].flag == "alice.met");
    REQUIRE(dialogs.size() == 1);
}