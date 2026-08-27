#include "jrpgmaker/domain/encounter.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace jrpgmaker::domain {

std::vector<EncounterPoint> ParseEncounterPoints(const nlohmann::json& document) {
    if (!document.is_object() || document.value("schema", 0) != kEncounterSchemaVersion ||
        !document.contains("encounters") || !document["encounters"].is_array()) {
        throw std::invalid_argument("encounter requires schema 1 and encounters array");
    }
    if (document["encounters"].size() > 256) {
        throw std::invalid_argument("encounter table contains too many points");
    }
    std::vector<EncounterPoint> points;
    for (const auto& node : document["encounters"]) {
        if (!node.is_object() || !node.contains("id") || !node["id"].is_string() ||
            !node.contains("position") || !node["position"].is_array() ||
            node["position"].size() != 3 || !node.contains("radius") ||
            !node["radius"].is_number() || !node.contains("encounter_id") ||
            !node["encounter_id"].is_string() || !node.contains("results") ||
            !node["results"].is_object()) {
            throw std::invalid_argument(
                "encounter requires id, position[3], radius, encounter_id and results");
        }
        EncounterPoint point{.id = node["id"].get<std::string>(),
                             .position = {node["position"][0].get<float>(),
                                          node["position"][1].get<float>(),
                                          node["position"][2].get<float>()},
                             .radius = node["radius"].get<float>(),
                             .plugin_id = node.value("plugin_id", std::string{}),
                             .encounter_id = node["encounter_id"].get<std::string>(),
                             .result_event_ids = {}};
        if (node.contains("plugin_id") && !node["plugin_id"].is_string()) {
            throw std::invalid_argument("encounter plugin_id must be a string");
        }
        if (point.id.empty() || point.encounter_id.empty() || !std::isfinite(point.radius) ||
            point.radius <= 0.0f ||
            std::any_of(points.begin(), points.end(),
                        [&](const auto& other) { return other.id == point.id; })) {
            throw std::invalid_argument("encounter has duplicate or invalid fields");
        }
        if (node["results"].empty() || node["results"].size() > 32) {
            throw std::invalid_argument("encounter results must contain 1..32 entries");
        }
        for (auto it = node["results"].begin(); it != node["results"].end(); ++it) {
            if (it.key().empty() || !it.value().is_string() ||
                it.value().get<std::string>().empty()) {
                throw std::invalid_argument(
                    "encounter result mapping must contain non-empty strings");
            }
            point.result_event_ids.emplace(it.key(), it.value().get<std::string>());
        }
        points.push_back(std::move(point));
    }
    return points;
}

void ValidateEncounterTargets(const std::vector<EncounterPoint>& points,
                              const EventScript& event_script) {
    std::vector<std::string> event_ids;
    event_ids.reserve(event_script.events.size());
    for (const Event& event : event_script.events)
        event_ids.push_back(event.id);
    for (const EncounterPoint& point : points) {
        for (const auto& [result_key, event_id] : point.result_event_ids) {
            if (std::find(event_ids.begin(), event_ids.end(), event_id) == event_ids.end()) {
                throw std::invalid_argument("encounter '" + point.id + "' result '" + result_key +
                                            "' references unknown event '" + event_id + "'");
            }
        }
    }
}

void EncounterSystem::Update(glm::vec3 player_position) {
    const EncounterPoint* next = nullptr;
    float next_distance = 0.0f;
    for (const EncounterPoint& point : points_) {
        const float distance =
            glm::dot(point.position - player_position, point.position - player_position);
        if (distance <= point.radius * point.radius &&
            (next == nullptr || distance < next_distance ||
             (distance == next_distance && point.id < next->id))) {
            next = &point;
            next_distance = distance;
        }
    }
    if (next != active_) {
        active_ = next;
        if (active_ != nullptr) {
            bus_.Publish(EncounterRequested{.point_id = active_->id,
                                            .plugin_id = active_->plugin_id,
                                            .encounter_id = active_->encounter_id,
                                            .result_event_ids = active_->result_event_ids});
        }
    }
}

} // namespace jrpgmaker::domain
