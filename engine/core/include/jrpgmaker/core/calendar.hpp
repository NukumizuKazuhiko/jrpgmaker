#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace jrpgmaker::core {

inline constexpr int kCalendarSchemaVersion = 1;

struct GameDate {
    std::int32_t year = 1;
    std::int32_t month = 1;
    std::int32_t day = 1;
};

struct CalendarDefinition {
    int schema = kCalendarSchemaVersion;
    std::string id;
    std::vector<std::int32_t> month_lengths;
    std::int32_t days_per_week = 7;
};

struct CalendarParseResult {
    bool ok = false;
    CalendarDefinition calendar;
    std::string error;
};

[[nodiscard]] CalendarParseResult ParseCalendarDefinition(const nlohmann::json& document);

class GameClock {
public:
    explicit GameClock(CalendarDefinition definition);

    [[nodiscard]] const CalendarDefinition& calendar() const { return calendar_; }
    [[nodiscard]] const GameDate& date() const { return date_; }
    [[nodiscard]] std::int32_t minute_of_day() const { return minute_of_day_; }
    [[nodiscard]] std::int64_t absolute_minutes() const { return absolute_minutes_; }

    void Set(GameDate date, std::int32_t minute_of_day);
    void AdvanceMinutes(std::int64_t minutes);

private:
    [[nodiscard]] std::int64_t DaysBeforeYear(std::int32_t year) const;
    [[nodiscard]] std::int64_t DaysBeforeMonth(std::int32_t month) const;
    void RebuildDateFromAbsoluteMinutes();
    void ValidateDate(GameDate date, std::int32_t minute_of_day) const;

    CalendarDefinition calendar_;
    GameDate date_;
    std::int32_t minute_of_day_ = 0;
    std::int64_t absolute_minutes_ = 0;
};

} // namespace jrpgmaker::core
