#include <limits>
#include <stdexcept>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "jrpgmaker/core/calendar.hpp"

namespace {

nlohmann::json DemoCalendar() {
    return {{"schema", 1},
            {"id", "demo"},
            {"month_lengths", nlohmann::json::array({30, 30, 30})},
            {"days_per_week", 7}};
}

} // namespace

TEST_CASE("calendar definition validates schema and ranges", "[calendar][p7]") {
    const auto parsed = jrpgmaker::core::ParseCalendarDefinition(DemoCalendar());
    REQUIRE(parsed.ok);
    REQUIRE(parsed.calendar.id == "demo");
    REQUIRE(parsed.calendar.month_lengths.size() == 3);

    auto invalid = DemoCalendar();
    invalid["month_lengths"] = nlohmann::json::array({30, 0});
    REQUIRE_FALSE(jrpgmaker::core::ParseCalendarDefinition(invalid).ok);
    invalid = DemoCalendar();
    invalid["schema"] = 2;
    REQUIRE_FALSE(jrpgmaker::core::ParseCalendarDefinition(invalid).ok);
}

TEST_CASE("game clock advances across days and months", "[calendar][p7]") {
    const auto parsed = jrpgmaker::core::ParseCalendarDefinition(DemoCalendar());
    REQUIRE(parsed.ok);
    jrpgmaker::core::GameClock clock(parsed.calendar);

    clock.Set({.year = 1, .month = 1, .day = 30}, 23 * 60 + 59);
    clock.AdvanceMinutes(1);
    REQUIRE(clock.date().month == 2);
    REQUIRE(clock.date().day == 1);
    REQUIRE(clock.minute_of_day() == 0);

    clock.AdvanceMinutes(30 * 24 * 60);
    REQUIRE(clock.date().month == 3);
    REQUIRE(clock.date().day == 1);
}

TEST_CASE("game clock rejects invalid dates and backwards movement", "[calendar][p7]") {
    const auto parsed = jrpgmaker::core::ParseCalendarDefinition(DemoCalendar());
    REQUIRE(parsed.ok);
    jrpgmaker::core::GameClock clock(parsed.calendar);

    REQUIRE_THROWS_AS(clock.Set({.year = 0, .month = 1, .day = 1}, 0), std::invalid_argument);
    REQUIRE_THROWS_AS(clock.Set({.year = 1, .month = 4, .day = 1}, 0), std::invalid_argument);
    REQUIRE_THROWS_AS(clock.Set({.year = 1, .month = 1, .day = 1}, 24 * 60), std::invalid_argument);
    REQUIRE_THROWS_AS(clock.AdvanceMinutes(-1), std::invalid_argument);
}

TEST_CASE("game clock rejects dates outside the representable range", "[calendar][p7]") {
    jrpgmaker::core::CalendarDefinition calendar{
        .schema = 1,
        .id = "large",
        .month_lengths = std::vector<std::int32_t>(128, std::numeric_limits<std::int32_t>::max()),
        .days_per_week = 7};
    jrpgmaker::core::GameClock clock(calendar);
    REQUIRE_THROWS_AS(
        clock.Set({.year = std::numeric_limits<std::int32_t>::max(), .month = 1, .day = 1}, 0),
        std::invalid_argument);
}
