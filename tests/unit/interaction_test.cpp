#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/core/event_bus.hpp"
#include "jrpgmaker/domain/event_runner.hpp"
#include "jrpgmaker/domain/interaction.hpp"

TEST_CASE("interaction system publishes prompt transitions and queues confirmation",
          "[domain][interaction]") {
    jrpgmaker::core::EventBus bus;
    const std::vector<jrpgmaker::domain::InteractionPoint> points = {
        {"npc", {0.0f, 0.0f, 0.0f}, 1.0f, "prompt.talk", "npc_talk"}};
    jrpgmaker::domain::InteractionSystem system(points, bus);
    int shown = 0;
    int hidden = 0;
    bus.Subscribe<jrpgmaker::domain::InteractionPromptShown>([&](const auto&) { ++shown; });
    bus.Subscribe<jrpgmaker::domain::InteractionPromptHidden>([&](const auto&) { ++hidden; });

    system.Update({0.5f, 0.0f, 0.0f}, false);
    system.Update({0.5f, 0.0f, 0.0f}, true);
    system.Update({0.5f, 0.0f, 0.0f}, true);
    REQUIRE(shown == 1);
    REQUIRE(system.DrainConfirmedEvents() == std::vector<std::string>{"npc_talk"});
    system.Update({3.0f, 0.0f, 0.0f}, false);
    REQUIRE(hidden == 1);
}

TEST_CASE("interaction system selects nearest point and rejects malformed data",
          "[domain][interaction]") {
    const auto points =
        jrpgmaker::domain::ParseInteractionPoints(nlohmann::json{{"schema", 1},
                                                                 {"interactions",
                                                                  {{{"id", "a"},
                                                                    {"position", {0, 0, 0}},
                                                                    {"radius", 1.0},
                                                                    {"prompt_text_key", "p"},
                                                                    {"target_event_id", "e"}}}}});
    REQUIRE(points.size() == 1);
    REQUIRE_THROWS(jrpgmaker::domain::ParseInteractionPoints(nlohmann::json{{"schema", 2}}));
}

TEST_CASE("interaction confirmation reaches the event runner dialog",
          "[domain][interaction][e2e]") {
    jrpgmaker::core::EventBus bus;
    const std::vector<jrpgmaker::domain::InteractionPoint> points = {
        {"npc", {0.0f, 0.0f, 0.0f}, 1.0f, "prompt.talk", "talk"}};
    jrpgmaker::domain::InteractionSystem interactions(points, bus);
    jrpgmaker::domain::EventScript script;
    script.events.push_back(
        {"talk", {{{jrpgmaker::domain::DialogInstruction{"npc", "npc.line"}}}}});
    jrpgmaker::domain::FlagStore flags;
    jrpgmaker::domain::EventRunner runner(script, flags, bus);

    interactions.Update({0.0f, 0.0f, 0.0f}, false);
    interactions.Update({0.0f, 0.0f, 0.0f}, true);
    for (const std::string& event_id : interactions.DrainConfirmedEvents()) {
        REQUIRE_FALSE(runner.IsActive());
        REQUIRE(runner.Start(event_id));
    }
    runner.Tick(0.0);
    REQUIRE(runner.IsDialogPending());
    runner.AdvanceDialog();
    runner.Tick(0.0);
    REQUIRE(runner.IsFinished());
}

TEST_CASE("interaction targets must exist in the event script", "[domain][interaction]") {
    const std::vector<jrpgmaker::domain::InteractionPoint> points = {
        {"npc", {0.0f, 0.0f, 0.0f}, 1.0f, "prompt.talk", "missing"}};
    jrpgmaker::domain::EventScript script;
    script.events.push_back({"present", {}});
    REQUIRE_THROWS(jrpgmaker::domain::ValidateInteractionTargets(points, script));
}
