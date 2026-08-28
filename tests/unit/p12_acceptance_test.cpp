#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "jrpgmaker/core/character_controller.hpp"
#include "jrpgmaker/core/event_bus.hpp"
#include "jrpgmaker/core/map_data.hpp"
#include "jrpgmaker/core/pathfinding.hpp"
#include "jrpgmaker/domain/event_runner.hpp"
#include "jrpgmaker/domain/event_script.hpp"
#include "jrpgmaker/domain/interaction.hpp"
#include "jrpgmaker/domain/save.hpp"
#include "jrpgmaker/domain/schedule.hpp"
#include "jrpgmaker/plugin/plugin.hpp"
#include "jrpgmaker/plugins/register.hpp"

namespace {

nlohmann::json ReadAsset(const char* name) {
    std::ifstream file(std::filesystem::path(JRPGMAKER_ASSET_DIR) / "data" / name);
    REQUIRE(file.is_open());
    return nlohmann::json::parse(file);
}

nlohmann::json ReadProjectFile(const std::filesystem::path& root, const std::string& relative) {
    std::ifstream file(root / relative);
    REQUIRE(file.is_open());
    return nlohmann::json::parse(file);
}

jrpgmaker::plugin::PluginManifest ReadPluginManifest(const std::filesystem::path& root,
                                                     const std::string& relative) {
    const auto result = jrpgmaker::plugin::ParseManifest(ReadProjectFile(root, relative));
    REQUIRE(result);
    return *result.manifest;
}

} // namespace

TEST_CASE("P12 reference data closes movement interaction schedule and save flow",
          "[p12][acceptance]") {
    const std::filesystem::path asset_root = JRPGMAKER_ASSET_DIR;
    const std::filesystem::path project_root = asset_root.parent_path();
    const auto project_result = jrpgmaker::plugin::ParseProjectManifest(
        ReadProjectFile(project_root, "assets/data/project_demo.json"));
    REQUIRE(project_result);
    jrpgmaker::plugin::PluginRegistry registry;
    const auto register_render = jrpgmaker::plugins::RegisterSamplePlugins(
        registry, ReadPluginManifest(project_root, "plugins/sample_unlit/plugin.json"),
        ReadPluginManifest(project_root, "plugins/sample_style/plugin.json"));
    REQUIRE_FALSE(register_render.has_value());
    const auto register_battle = jrpgmaker::plugins::RegisterSampleBattlePlugins(
        registry, ReadPluginManifest(project_root, "plugins/sample_instant/plugin.json"),
        ReadPluginManifest(project_root, "plugins/sample_turn_based/plugin.json"));
    REQUIRE_FALSE(register_battle.has_value());
    REQUIRE_FALSE(jrpgmaker::plugin::ValidateProjectPlugins(*project_result.manifest, registry));
    REQUIRE_FALSE(
        jrpgmaker::plugin::ValidateProjectDataRoots(*project_result.manifest, project_root));
    REQUIRE(jrpgmaker::plugin::ValidateProjectPluginData(*project_result.manifest, registry,
                                                         project_root)
                .empty());
    REQUIRE(jrpgmaker::plugin::CreateProjectRenderStyle(*project_result.manifest, registry));
    REQUIRE(jrpgmaker::plugin::CreateProjectBattlePlugin(*project_result.manifest, registry));

    const auto navigation = jrpgmaker::core::ParseNavigationGrid(ReadAsset("navigation_demo.json"));
    const auto obstacles = jrpgmaker::core::ParseCollisionAabbs(ReadAsset("collision_demo.json"));
    const auto interactions =
        jrpgmaker::domain::ParseInteractionPoints(ReadAsset("interaction_demo.json"));
    const auto script = jrpgmaker::domain::ParseEventScript(ReadAsset("events_demo.json"));
    const auto calendar_result =
        jrpgmaker::core::ParseCalendarDefinition(ReadAsset("calendar_demo.json"));
    REQUIRE(calendar_result.ok);
    const auto schedule = jrpgmaker::domain::ParseScheduleTable(ReadAsset("schedule_demo.json"),
                                                                calendar_result.calendar);

    REQUIRE(navigation.FindPath({0, 0}, {4, 4}).succeeded());
    REQUIRE(navigation.FindPath({0, 0}, {2, 2}).failure ==
            jrpgmaker::core::PathFailure::kGoalBlocked);
    jrpgmaker::domain::ValidateInteractionTargets(interactions, script);
    jrpgmaker::domain::ValidateScheduleTargets(schedule, script);

    jrpgmaker::core::CharacterController controller(
        {.position = {0.0f, 1.0f, 0.0f}, .radius = 0.35f, .half_height = 0.9f});
    controller.Move({3.0f, 0.0f, 0.0f}, 1.0f, obstacles);
    REQUIRE(controller.state().blocked);
    REQUIRE(controller.state().position.x < 1.0f);

    jrpgmaker::core::EventBus bus;
    jrpgmaker::domain::FlagStore flags;
    jrpgmaker::domain::EventRunner runner(script, flags, bus);
    jrpgmaker::domain::InteractionSystem interaction_system(interactions, bus);
    std::string prompt;
    std::string dialog;
    bus.Subscribe<jrpgmaker::domain::InteractionPromptShown>(
        [&prompt](const auto& event) { prompt = event.prompt_text_key; });
    bus.Subscribe<jrpgmaker::domain::DialogRequested>(
        [&dialog](const auto& event) { dialog = event.text_key; });

    interaction_system.Update({0.0f, 0.9f, 0.0f}, false);
    REQUIRE(prompt == "prompt.talk.alice");
    interaction_system.Update({0.0f, 0.9f, 0.0f}, true);
    const auto confirmed = interaction_system.DrainConfirmedEvents();
    REQUIRE(confirmed == std::vector<std::string>{"alice_reward"});
    REQUIRE(runner.Start(confirmed.front()));
    runner.Tick(0.0);
    REQUIRE(runner.IsDialogPending());
    REQUIRE(dialog == "alice.reward.line");
    runner.AdvanceDialog();
    runner.Tick(0.0);
    REQUIRE(runner.IsFinished());

    jrpgmaker::core::GameClock clock(calendar_result.calendar);
    jrpgmaker::domain::ScheduleSystem schedule_system(schedule, calendar_result.calendar);
    REQUIRE(schedule_system.Poll(clock).empty());
    clock.AdvanceMinutes(24 * 60 + 480);
    REQUIRE(schedule_system.Poll(clock) == std::vector<std::string>{"meet_alice"});

    flags.Set("alice.met", true);
    const auto saved = jrpgmaker::domain::CaptureSave(clock, flags);
    jrpgmaker::core::GameClock restored(calendar_result.calendar);
    jrpgmaker::domain::FlagStore restored_flags;
    jrpgmaker::domain::RestoreSave(saved, restored, restored_flags);
    REQUIRE(restored.absolute_minutes() == clock.absolute_minutes());
    REQUIRE(restored_flags.Get("alice.met"));
}
