#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/domain/event_script.hpp"

namespace jrpgmaker::domain {

inline constexpr int kVerticalSliceSchemaVersion = 1;
inline constexpr double kMinimumVerticalSliceSeconds = 30.0 * 60.0;

struct VerticalSliceBeat {
    std::string id;
    double duration_seconds = 0.0;
    std::string target_event_id;
};

struct VerticalSliceDefinition {
    int schema = kVerticalSliceSchemaVersion;
    std::string id;
    std::vector<VerticalSliceBeat> beats;
    double total_duration_seconds = 0.0;
};

VerticalSliceDefinition ParseVerticalSliceDefinition(const nlohmann::json& document);
void ValidateVerticalSliceTargets(const VerticalSliceDefinition& slice,
                                  const EventScript& event_script);

} // namespace jrpgmaker::domain
