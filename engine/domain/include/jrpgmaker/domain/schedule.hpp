#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "jrpgmaker/core/calendar.hpp"
#include "jrpgmaker/domain/event_script.hpp"

namespace jrpgmaker::domain {

struct ScheduleEntry {
    std::string id;
    std::int32_t month = 1;
    std::int32_t day = 1;
    std::int32_t minute_of_day = 0;
    std::string target_event_id;
    bool repeat_yearly = true;
};

struct ScheduleTable {
    int schema = 1;
    std::vector<ScheduleEntry> entries;
};

ScheduleTable ParseScheduleTable(const nlohmann::json& document,
                                 const core::CalendarDefinition& calendar);
void ValidateScheduleTargets(const ScheduleTable& schedule, const EventScript& event_script);

// Emits each matching entry once for every occurrence crossed since the last
// Poll. Results are ordered by trigger time and then stable entry id.
class ScheduleSystem {
public:
    ScheduleSystem(const ScheduleTable& schedule, const core::CalendarDefinition& calendar)
        : schedule_(schedule), calendar_(calendar) {}

    [[nodiscard]] std::vector<std::string> Poll(const core::GameClock& clock);
    void Reset(const core::GameClock& clock);

private:
    const ScheduleTable& schedule_;
    const core::CalendarDefinition& calendar_;
    std::int64_t previous_minutes_ = -1;
};

} // namespace jrpgmaker::domain
