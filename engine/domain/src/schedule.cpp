#include "jrpgmaker/domain/schedule.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace jrpgmaker::domain {

namespace {

std::string RequireString(const nlohmann::json& node, const char* key) {
    if (!node.contains(key) || !node[key].is_string() || node[key].get<std::string>().empty()) {
        throw std::invalid_argument(std::string("schedule requires non-empty '") + key + "'");
    }
    return node[key].get<std::string>();
}

std::int32_t RequireInt(const nlohmann::json& node, const char* key) {
    if (!node.contains(key) || !node[key].is_number_integer()) {
        throw std::invalid_argument(std::string("schedule requires integer '") + key + "'");
    }
    return node[key].get<std::int32_t>();
}

std::int64_t DaysInYear(const core::CalendarDefinition& calendar) {
    std::int64_t days = 0;
    for (const auto length : calendar.month_lengths)
        days += length;
    return days;
}

std::int64_t DaysBeforeMonth(const core::CalendarDefinition& calendar, std::int32_t month) {
    std::int64_t days = 0;
    for (std::int32_t index = 1; index < month; ++index) {
        days += calendar.month_lengths[static_cast<std::size_t>(index - 1)];
    }
    return days;
}

} // namespace

ScheduleTable ParseScheduleTable(const nlohmann::json& document,
                                 const core::CalendarDefinition& calendar) {
    if (!document.is_object() || document.value("schema", 0) != 1 ||
        !document.contains("entries") || !document["entries"].is_array()) {
        throw std::invalid_argument("schedule requires schema 1 and entries array");
    }
    ScheduleTable table;
    if (document["entries"].size() > 4096) {
        throw std::invalid_argument("schedule contains too many entries");
    }
    std::set<std::string> ids;
    for (const auto& node : document["entries"]) {
        ScheduleEntry entry{.id = RequireString(node, "id"),
                            .month = RequireInt(node, "month"),
                            .day = RequireInt(node, "day"),
                            .minute_of_day = RequireInt(node, "minute_of_day"),
                            .target_event_id = RequireString(node, "target_event_id"),
                            .repeat_yearly = node.value("repeat_yearly", true)};
        if (!ids.insert(entry.id).second || entry.month < 1 ||
            entry.month > static_cast<std::int32_t>(calendar.month_lengths.size()) ||
            entry.day < 1 ||
            entry.day > calendar.month_lengths[static_cast<std::size_t>(entry.month - 1)] ||
            entry.minute_of_day < 0 || entry.minute_of_day >= 24 * 60) {
            throw std::invalid_argument("schedule entry has duplicate or out-of-range fields");
        }
        table.entries.push_back(std::move(entry));
    }
    return table;
}

void ValidateScheduleTargets(const ScheduleTable& schedule, const EventScript& event_script) {
    std::set<std::string> event_ids;
    for (const auto& event : event_script.events)
        event_ids.insert(event.id);
    for (const auto& entry : schedule.entries) {
        if (!event_ids.contains(entry.target_event_id)) {
            throw std::invalid_argument("schedule entry '" + entry.id +
                                        "' references unknown event '" + entry.target_event_id +
                                        "'");
        }
    }
}

std::vector<std::string> ScheduleSystem::Poll(const core::GameClock& clock) {
    if (clock.calendar().schema != calendar_.schema || clock.calendar().id != calendar_.id ||
        clock.calendar().days_per_week != calendar_.days_per_week ||
        clock.calendar().month_lengths != calendar_.month_lengths) {
        throw std::invalid_argument("schedule clock calendar does not match schedule calendar");
    }
    const std::int64_t now = clock.absolute_minutes();
    if (previous_minutes_ < 0) {
        previous_minutes_ = now;
        return {};
    }
    if (now < previous_minutes_)
        throw std::invalid_argument("schedule clock moved backwards");

    struct FiredEntry {
        std::int64_t at;
        std::string id;
        std::string target;
    };
    std::vector<FiredEntry> fired;
    const std::int64_t year_days = DaysInYear(calendar_);
    const std::int64_t first_day = previous_minutes_ / (24 * 60);
    const std::int64_t last_day = now / (24 * 60);
    if (last_day - first_day > 366ll * 1000ll) {
        throw std::invalid_argument("schedule poll range is too large");
    }
    for (const auto& entry : schedule_.entries) {
        const std::int64_t day_in_year = DaysBeforeMonth(calendar_, entry.month) + entry.day - 1;
        for (std::int64_t day = first_day; day <= last_day; ++day) {
            if (!entry.repeat_yearly && day >= year_days)
                continue;
            if (day % year_days != day_in_year)
                continue;
            const std::int64_t at = day * 24 * 60 + entry.minute_of_day;
            if (at > previous_minutes_ && at <= now) {
                fired.push_back({at, entry.id, entry.target_event_id});
            }
        }
    }
    previous_minutes_ = now;
    std::sort(fired.begin(), fired.end(), [](const FiredEntry& lhs, const FiredEntry& rhs) {
        if (lhs.at != rhs.at)
            return lhs.at < rhs.at;
        return lhs.id < rhs.id;
    });
    std::vector<std::string> result;
    result.reserve(fired.size());
    for (const auto& entry : fired)
        result.push_back(entry.target);
    return result;
}

void ScheduleSystem::Reset(const core::GameClock& clock) {
    if (clock.calendar().schema != calendar_.schema || clock.calendar().id != calendar_.id ||
        clock.calendar().days_per_week != calendar_.days_per_week ||
        clock.calendar().month_lengths != calendar_.month_lengths) {
        throw std::invalid_argument("schedule clock calendar does not match schedule calendar");
    }
    previous_minutes_ = clock.absolute_minutes();
}

} // namespace jrpgmaker::domain
