#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "jrpgmaker/core/calendar.hpp"
#include "jrpgmaker/domain/save.hpp"

namespace {

jrpgmaker::core::CalendarDefinition Calendar() {
    return {.schema = 1, .id = "demo", .month_lengths = {30, 30}, .days_per_week = 7};
}

} // namespace

TEST_CASE("save round trip captures clock and flags", "[save][p7]") {
    jrpgmaker::core::GameClock clock{Calendar()};
    clock.Set({.year = 2, .month = 2, .day = 3}, 615);
    jrpgmaker::domain::FlagStore flags;
    flags.Set("met.guide", true);
    flags.Set("opened.gate", true);

    const auto document =
        jrpgmaker::domain::SerializeSave(jrpgmaker::domain::CaptureSave(clock, flags));
    const auto parsed = jrpgmaker::domain::ParseSave(document);
    REQUIRE(parsed.ok);
    REQUIRE(parsed.state.calendar_id == "demo");
    REQUIRE(parsed.state.date.year == 2);
    REQUIRE(parsed.state.date.month == 2);
    REQUIRE(parsed.state.date.day == 3);
    REQUIRE(parsed.state.minute_of_day == 615);
    REQUIRE(parsed.state.true_flags == std::vector<std::string>{"met.guide", "opened.gate"});
}

TEST_CASE("save migration requires explicit advancing steps", "[save][p7]") {
    const nlohmann::json old_save = {{"schema", 0}, {"calendar_id", "demo"}};
    const auto missing = jrpgmaker::domain::MigrateAndParseSave(old_save);
    REQUIRE_FALSE(missing.ok);

    const auto migrated = jrpgmaker::domain::MigrateAndParseSave(
        old_save, {{0, [](const nlohmann::json& input) {
                        nlohmann::json output = input;
                        output["schema"] = 1;
                        output["date"] = {{"year", 1}, {"month", 1}, {"day", 1}};
                        output["minute_of_day"] = 0;
                        output["true_flags"] = nlohmann::json::array();
                        return output;
                    }}});
    REQUIRE(migrated.ok);
    REQUIRE(migrated.state.calendar_id == "demo");

    const auto negative = jrpgmaker::domain::MigrateAndParseSave({{"schema", -1}});
    REQUIRE_FALSE(negative.ok);
}

TEST_CASE("save restore rejects a different calendar", "[save][p7]") {
    jrpgmaker::core::GameClock clock{Calendar()};
    jrpgmaker::domain::FlagStore flags;
    const jrpgmaker::domain::SaveState state{
        .calendar_id = "other", .date = {}, .minute_of_day = 0, .true_flags = {}};
    REQUIRE_THROWS(jrpgmaker::domain::RestoreSave(state, clock, flags));
}

TEST_CASE("save file round trip uses the migration-aware reader", "[save][p7]") {
    const auto path = std::filesystem::temp_directory_path() / "jrpgmaker_save_test.json";
    const jrpgmaker::domain::SaveState state{.calendar_id = "demo",
                                             .date = {.year = 1, .month = 2, .day = 4},
                                             .minute_of_day = 42,
                                             .true_flags = {"a"}};
    std::string error;
    REQUIRE(jrpgmaker::domain::WriteSaveFile(path, state, error));
    REQUIRE(error.empty());
    const auto loaded = jrpgmaker::domain::ReadSaveFile(path);
    REQUIRE(loaded.ok);
    REQUIRE(loaded.state.date.month == 2);
    REQUIRE(loaded.state.minute_of_day == 42);
    std::filesystem::remove(path);
}
