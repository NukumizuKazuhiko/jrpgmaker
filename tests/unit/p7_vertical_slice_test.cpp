#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "jrpgmaker/core/calendar.hpp"
#include "jrpgmaker/core/event_bus.hpp"
#include "jrpgmaker/domain/event_runner.hpp"
#include "jrpgmaker/domain/event_script.hpp"
#include "jrpgmaker/domain/flag_store.hpp"
#include "jrpgmaker/domain/save.hpp"
#include "jrpgmaker/domain/schedule.hpp"

TEST_CASE("data-driven schedule reaches dialog and survives save restore", "[p7][vertical-slice]") {
    const auto calendar_result = jrpgmaker::core::ParseCalendarDefinition(
        {{"schema", 1}, {"id", "slice"}, {"month_lengths", {30, 30}}});
    REQUIRE(calendar_result.ok);
    jrpgmaker::core::GameClock clock{calendar_result.calendar};

    const auto schedule =
        jrpgmaker::domain::ParseScheduleTable({{"schema", 1},
                                               {"entries",
                                                {{{"id", "morning"},
                                                  {"month", 1},
                                                  {"day", 1},
                                                  {"minute_of_day", 30},
                                                  {"target_event_id", "morning.event"}}}}},
                                              calendar_result.calendar);
    const auto script = jrpgmaker::domain::ParseEventScript(
        {{"schema", 1},
         {"events",
          {{{"id", "morning.event"},
            {"instructions",
             {{{"op", "dialog"}, {"speaker", "guide"}, {"text_key", "guide.morning"}},
              {{"op", "set_flag"}, {"flag", "met.guide"}, {"value", true}}}}}}}});
    jrpgmaker::domain::ValidateScheduleTargets(schedule, script);

    jrpgmaker::core::EventBus bus;
    jrpgmaker::domain::FlagStore flags;
    jrpgmaker::domain::EventRunner runner(script, flags, bus);
    jrpgmaker::domain::ScheduleSystem schedule_system(schedule, calendar_result.calendar);
    REQUIRE(schedule_system.Poll(clock).empty());
    clock.AdvanceMinutes(30);
    const auto due = schedule_system.Poll(clock);
    REQUIRE(due == std::vector<std::string>{"morning.event"});
    REQUIRE(runner.Start(due.front()));
    runner.Tick(0.0);
    REQUIRE(runner.IsDialogPending());
    runner.AdvanceDialog();
    runner.Tick(0.0);
    REQUIRE(runner.IsFinished());
    REQUIRE(flags.Get("met.guide"));

    const auto saved = jrpgmaker::domain::CaptureSave(clock, flags);
    jrpgmaker::core::GameClock restored_clock{calendar_result.calendar};
    jrpgmaker::domain::FlagStore restored_flags;
    jrpgmaker::domain::RestoreSave(saved, restored_clock, restored_flags);
    REQUIRE(restored_clock.absolute_minutes() == clock.absolute_minutes());
    REQUIRE(restored_flags.Get("met.guide"));
}
