#pragma once

#include <map>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "jrpgmaker/core/event_bus.hpp"
#include "jrpgmaker/domain/event_script.hpp"

namespace jrpgmaker::domain {

inline constexpr int kEncounterSchemaVersion = 1;

struct EncounterPoint {
    std::string id;
    glm::vec3 position{0.0f};
    float radius = 1.0f;
    // Empty means the project-selected battle plugin.
    std::string plugin_id;
    std::string encounter_id;
    std::map<std::string, std::string> result_event_ids;
};

struct EncounterRequested {
    std::string point_id;
    std::string plugin_id;
    std::string encounter_id;
    std::map<std::string, std::string> result_event_ids;
};

std::vector<EncounterPoint> ParseEncounterPoints(const nlohmann::json& document);
void ValidateEncounterTargets(const std::vector<EncounterPoint>& points,
                              const EventScript& event_script);

class EncounterSystem {
public:
    EncounterSystem(const std::vector<EncounterPoint>& points, core::EventBus& bus)
        : points_(points), bus_(bus) {}

    void Update(glm::vec3 player_position);

private:
    const std::vector<EncounterPoint>& points_;
    core::EventBus& bus_;
    const EncounterPoint* active_ = nullptr;
};

} // namespace jrpgmaker::domain
