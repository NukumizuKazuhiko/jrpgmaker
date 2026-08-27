#include <stdexcept>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "jrpgmaker/core/calendar.hpp"
#include "jrpgmaker/domain/event_script.hpp"
#include "jrpgmaker/domain/schedule.hpp"

namespace {
jrpgmaker::core::CalendarDefinition Calendar() {
    return {.schema = 1, .id = "demo", .month_lengths = {30, 30}, .days_per_week = 7};
}
} // namespace

TEST_CASE("schedule parser validates dates and event references", "[schedule][p7]") {
    const auto calendar = Calendar();
    const auto table =
        jrpgmaker::domain::ParseScheduleTable({{"schema", 1},
                                               {"entries",
                                                {{{"id", "guide"},
                                                  {"month", 1},
                                                  {"day", 2},
                                                  {"minute_of_day", 480},
                                                  {"target_event_id", "guide.morning"}}}}},
                                              calendar);
    const auto script = jrpgmaker::domain::ParseEventScript(
        {{"schema", 1},
         {"events", {{{"id", "guide.morning"}, {"instructions", nlohmann::json::array()}}}}});
    REQUIRE_NOTHROW(jrpgmaker::domain::ValidateScheduleTargets(table, script));
    REQUIRE_THROWS(jrpgmaker::domain::ValidateScheduleTargets(
        table, jrpgmaker::domain::ParseEventScript({{"schema", 1}, {"events", {}}})));
}

TEST_CASE("schedule emits crossed triggers in stable order", "[schedule][p7]") {
    const auto calendar = Calendar();
    const auto table =
        jrpgmaker::domain::ParseScheduleTable({{"schema", 1},
                                               {"entries",
                                                {{{"id", "late"},
                                                  {"month", 1},
                                                  {"day", 2},
                                                  {"minute_of_day", 600},
                                                  {"target_event_id", "late.event"}},
                                                 {{"id", "early"},
                                                  {"month", 1},
                                                  {"day", 2},
                                                  {"minute_of_day", 480},
                                                  {"target_event_id", "early.event"}}}}},
                                              calendar);
    jrpgmaker::core::GameClock clock{calendar};
    jrpgmaker::domain::ScheduleSystem schedules(table, calendar);
    REQUIRE(schedules.Poll(clock).empty());
    clock.Set({.year = 1, .month = 1, .day = 1}, 0);
    schedules.Reset(clock);
    clock.AdvanceMinutes(24 * 60 + 11 * 60);
    const auto fired = schedules.Poll(clock);
    REQUIRE(fired == std::vector<std::string>{"early.event", "late.event"});
    REQUIRE(schedules.Poll(clock).empty());
}

TEST_CASE("schedule rejects backwards clock movement", "[schedule][p7]") {
    const auto calendar = Calendar();
    const jrpgmaker::domain::ScheduleTable table;
    jrpgmaker::core::GameClock clock{calendar};
    jrpgmaker::domain::ScheduleSystem schedules(table, calendar);
    REQUIRE(schedules.Poll(clock).empty());
    clock.AdvanceMinutes(10);
    REQUIRE(schedules.Poll(clock).empty());
    clock.Set({.year = 1, .month = 1, .day = 1}, 0);
    REQUIRE_THROWS(schedules.Poll(clock));
}

TEST_CASE("schedule rejects a calendar with matching id but different rules", "[schedule][p7]") {
    const auto calendar = Calendar();
    const jrpgmaker::domain::ScheduleTable table;
    jrpgmaker::core::GameClock clock{calendar};
    auto incompatible = calendar;
    incompatible.month_lengths[0] = 29;
    jrpgmaker::domain::ScheduleSystem schedules(table, incompatible);
    REQUIRE_THROWS(schedules.Poll(clock));
}

TEST_CASE("schedule rejects an unbounded poll range", "[schedule][p7]") {
    const auto calendar = Calendar();
    const jrpgmaker::domain::ScheduleTable table;
    jrpgmaker::core::GameClock clock{calendar};
    jrpgmaker::domain::ScheduleSystem schedules(table, calendar);
    REQUIRE(schedules.Poll(clock).empty());
    clock.AdvanceMinutes(366ll * 1000ll * 24ll * 60ll + 24ll * 60ll);
    REQUIRE_THROWS(schedules.Poll(clock));
}
