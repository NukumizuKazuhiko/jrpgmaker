#include "jrpgmaker/domain/vertical_slice.hpp"

#include <cmath>
#include <set>
#include <stdexcept>

namespace jrpgmaker::domain {

VerticalSliceDefinition ParseVerticalSliceDefinition(const nlohmann::json& document) {
    if (!document.is_object() || document.value("schema", 0) != kVerticalSliceSchemaVersion ||
        !document.contains("id") || !document["id"].is_string() ||
        document["id"].get<std::string>().empty() || !document.contains("beats") ||
        !document["beats"].is_array()) {
        throw std::invalid_argument("vertical slice requires schema 1, id and beats array");
    }
    if (document["beats"].empty() || document["beats"].size() > 256) {
        throw std::invalid_argument("vertical slice beats must contain 1..256 entries");
    }

    VerticalSliceDefinition result{.schema = kVerticalSliceSchemaVersion,
                                   .id = document["id"].get<std::string>(),
                                   .beats = {},
                                   .total_duration_seconds = 0.0};
    std::set<std::string> ids;
    for (const auto& node : document["beats"]) {
        if (!node.is_object() || !node.contains("id") || !node["id"].is_string() ||
            !node.contains("duration_seconds") || !node["duration_seconds"].is_number() ||
            !node.contains("target_event_id") || !node["target_event_id"].is_string()) {
            throw std::invalid_argument("vertical slice beat requires id, duration_seconds and "
                                        "target_event_id");
        }
        const VerticalSliceBeat beat{.id = node["id"].get<std::string>(),
                                     .duration_seconds = node["duration_seconds"].get<double>(),
                                     .target_event_id = node["target_event_id"].get<std::string>()};
        if (beat.id.empty() || beat.target_event_id.empty() ||
            !std::isfinite(beat.duration_seconds) || beat.duration_seconds <= 0.0 ||
            beat.duration_seconds > 24.0 * 60.0 * 60.0 || !ids.insert(beat.id).second) {
            throw std::invalid_argument("vertical slice beat has duplicate or invalid fields");
        }
        if (result.total_duration_seconds > kMinimumVerticalSliceSeconds - beat.duration_seconds) {
            throw std::invalid_argument("vertical slice duration exceeds supported range");
        }
        result.total_duration_seconds += beat.duration_seconds;
        result.beats.push_back(beat);
    }
    if (result.total_duration_seconds < kMinimumVerticalSliceSeconds) {
        throw std::invalid_argument("vertical slice must contain at least 30 minutes of content");
    }
    return result;
}

void ValidateVerticalSliceTargets(const VerticalSliceDefinition& slice,
                                  const EventScript& event_script) {
    std::set<std::string> event_ids;
    for (const Event& event : event_script.events)
        event_ids.insert(event.id);
    for (const VerticalSliceBeat& beat : slice.beats) {
        if (!event_ids.contains(beat.target_event_id)) {
            throw std::invalid_argument("vertical slice beat '" + beat.id +
                                        "' references unknown event '" + beat.target_event_id +
                                        "'");
        }
    }
}

} // namespace jrpgmaker::domain
