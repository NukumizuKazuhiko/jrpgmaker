#include "jrpgmaker/core/calendar.hpp"

#include <limits>
#include <stdexcept>

namespace jrpgmaker::core {

CalendarParseResult ParseCalendarDefinition(const nlohmann::json& document) {
    if (!document.is_object() || document.value("schema", 0) != kCalendarSchemaVersion) {
        return {.ok = false, .calendar = {}, .error = "calendar requires schema 1"};
    }
    if (!document.contains("id") || !document["id"].is_string() ||
        document["id"].get<std::string>().empty()) {
        return {.ok = false, .calendar = {}, .error = "calendar id must be a non-empty string"};
    }
    if (!document.contains("month_lengths") || !document["month_lengths"].is_array() ||
        document["month_lengths"].empty()) {
        return {.ok = false, .calendar = {}, .error = "calendar month_lengths must be non-empty"};
    }

    CalendarDefinition calendar;
    calendar.id = document["id"].get<std::string>();
    calendar.month_lengths.reserve(document["month_lengths"].size());
    for (const auto& value : document["month_lengths"]) {
        if (!value.is_number_integer() || value.get<std::int32_t>() <= 0) {
            return {.ok = false,
                    .calendar = {},
                    .error = "calendar month lengths must be positive integers"};
        }
        calendar.month_lengths.push_back(value.get<std::int32_t>());
    }
    if (calendar.month_lengths.size() > 128) {
        return {.ok = false, .calendar = {}, .error = "calendar has too many months"};
    }
    if (document.contains("days_per_week")) {
        if (!document["days_per_week"].is_number_integer()) {
            return {.ok = false, .calendar = {}, .error = "days_per_week must be an integer"};
        }
        calendar.days_per_week = document["days_per_week"].get<std::int32_t>();
    }
    if (calendar.days_per_week <= 0 || calendar.days_per_week > 100) {
        return {.ok = false, .calendar = {}, .error = "days_per_week must be in range 1..100"};
    }
    return {.ok = true, .calendar = std::move(calendar), .error = {}};
}

GameClock::GameClock(CalendarDefinition definition) : calendar_(std::move(definition)) {
    if (calendar_.schema != kCalendarSchemaVersion || calendar_.id.empty() ||
        calendar_.month_lengths.empty() || calendar_.days_per_week <= 0) {
        throw std::invalid_argument("invalid calendar definition");
    }
    for (const std::int32_t length : calendar_.month_lengths) {
        if (length <= 0) {
            throw std::invalid_argument("calendar month length must be positive");
        }
    }
    Set({.year = 1, .month = 1, .day = 1}, 0);
}

void GameClock::ValidateDate(GameDate date, std::int32_t minute_of_day) const {
    if (date.year < 1 || date.month < 1 ||
        date.month > static_cast<std::int32_t>(calendar_.month_lengths.size()) || date.day < 1 ||
        date.day > calendar_.month_lengths[static_cast<std::size_t>(date.month - 1)] ||
        minute_of_day < 0 || minute_of_day >= 24 * 60) {
        throw std::invalid_argument("date or minute_of_day is outside calendar bounds");
    }
}

std::int64_t GameClock::DaysBeforeMonth(std::int32_t month) const {
    std::int64_t days = 0;
    for (std::int32_t index = 1; index < month; ++index) {
        days += calendar_.month_lengths[static_cast<std::size_t>(index - 1)];
    }
    return days;
}

std::int64_t GameClock::DaysBeforeYear(std::int32_t year) const {
    const std::int64_t year_days = [&] {
        std::int64_t total = 0;
        for (const std::int32_t length : calendar_.month_lengths)
            total += length;
        return total;
    }();
    return (static_cast<std::int64_t>(year) - 1) * year_days;
}

void GameClock::Set(GameDate date, std::int32_t minute_of_day) {
    ValidateDate(date, minute_of_day);
    std::int64_t year_days = 0;
    for (const std::int32_t length : calendar_.month_lengths)
        year_days += length;
    const std::int64_t years_before = static_cast<std::int64_t>(date.year - 1);
    if (years_before > std::numeric_limits<std::int64_t>::max() / year_days ||
        years_before * year_days > std::numeric_limits<std::int64_t>::max() / (24 * 60)) {
        throw std::invalid_argument("date exceeds clock range");
    }
    const std::int64_t days_before_year = years_before * year_days;
    date_ = date;
    minute_of_day_ = minute_of_day;
    absolute_minutes_ =
        (days_before_year + DaysBeforeMonth(date.month) + date.day - 1) * 24 * 60 + minute_of_day;
}

void GameClock::RebuildDateFromAbsoluteMinutes() {
    const std::int64_t absolute_days = absolute_minutes_ / (24 * 60);
    minute_of_day_ = static_cast<std::int32_t>(absolute_minutes_ % (24 * 60));
    std::int64_t year_days = 0;
    for (const std::int32_t length : calendar_.month_lengths)
        year_days += length;
    date_.year = static_cast<std::int32_t>(absolute_days / year_days) + 1;
    std::int64_t day_in_year = absolute_days % year_days;
    date_.month = 1;
    for (const std::int32_t length : calendar_.month_lengths) {
        if (day_in_year < length)
            break;
        day_in_year -= length;
        ++date_.month;
    }
    date_.day = static_cast<std::int32_t>(day_in_year) + 1;
}

void GameClock::AdvanceMinutes(std::int64_t minutes) {
    if (minutes < 0 || minutes > std::numeric_limits<std::int64_t>::max() - absolute_minutes_) {
        throw std::invalid_argument("clock advance would overflow or move backwards");
    }
    const std::int64_t next_minutes = absolute_minutes_ + minutes;
    std::int64_t year_days = 0;
    for (const std::int32_t length : calendar_.month_lengths)
        year_days += length;
    const std::int64_t next_days = next_minutes / (24 * 60);
    if (next_days / year_days >= std::numeric_limits<std::int32_t>::max()) {
        throw std::invalid_argument("clock date exceeds supported year range");
    }
    absolute_minutes_ = next_minutes;
    RebuildDateFromAbsoluteMinutes();
}

} // namespace jrpgmaker::core
