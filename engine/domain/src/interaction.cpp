#include "jrpgmaker/domain/interaction.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace jrpgmaker::domain {

namespace {

[[noreturn]] void ParseError(const std::string& message) {
    throw std::invalid_argument("interaction parse error: " + message);
}

} // namespace

std::vector<InteractionPoint> ParseInteractionPoints(const nlohmann::json& document) {
    if (!document.is_object() || document.value("schema", 0) != 1 ||
        !document.contains("interactions") || !document["interactions"].is_array()) {
        ParseError("expected schema 1 and an interactions array");
    }
    std::vector<InteractionPoint> points;
    for (const auto& node : document["interactions"]) {
        if (!node.is_object() || !node.contains("id") || !node["id"].is_string() ||
            !node.contains("position") || !node["position"].is_array() ||
            node["position"].size() != 3 || !node.contains("radius") ||
            !node["radius"].is_number() || !node.contains("prompt_text_key") ||
            !node["prompt_text_key"].is_string() || !node.contains("target_event_id") ||
            !node["target_event_id"].is_string()) {
            ParseError("interaction requires id, position[3], radius, prompt_text_key and "
                       "target_event_id");
        }
        InteractionPoint point{.id = node["id"].get<std::string>(),
                               .position = {node["position"][0].get<float>(),
                                            node["position"][1].get<float>(),
                                            node["position"][2].get<float>()},
                               .radius = node["radius"].get<float>(),
                               .prompt_text_key = node["prompt_text_key"].get<std::string>(),
                               .target_event_id = node["target_event_id"].get<std::string>()};
        if (point.id.empty() || point.prompt_text_key.empty() || point.target_event_id.empty() ||
            point.radius <= 0.0f) {
            ParseError("interaction has an empty field or non-positive radius");
        }
        if (std::any_of(points.begin(), points.end(),
                        [&](const InteractionPoint& other) { return other.id == point.id; })) {
            ParseError("duplicate interaction id '" + point.id + "'");
        }
        points.push_back(std::move(point));
    }
    return points;
}

void ValidateInteractionTargets(const std::vector<InteractionPoint>& points,
                                const EventScript& event_script) {
    std::unordered_set<std::string> event_ids;
    for (const Event& event : event_script.events) {
        if (!event_ids.insert(event.id).second) {
            ParseError("event script contains duplicate event id '" + event.id + "'");
        }
    }
    for (const InteractionPoint& point : points) {
        if (!event_ids.contains(point.target_event_id)) {
            ParseError("interaction '" + point.id + "' references missing event '" +
                       point.target_event_id + "'");
        }
    }
}

void InteractionSystem::Update(glm::vec3 player_position, bool confirm_pressed) {
    const InteractionPoint* next = nullptr;
    float next_distance = 0.0f;
    for (const InteractionPoint& point : points_) {
        const float distance =
            glm::dot(point.position - player_position, point.position - player_position);
        if (distance > point.radius * point.radius ||
            (next != nullptr &&
             (distance > next_distance || (distance == next_distance && point.id >= next->id)))) {
            continue;
        }
        next = &point;
        next_distance = distance;
    }

    if (next != active_) {
        if (active_ != nullptr) {
            bus_.Publish(InteractionPromptHidden{active_->id});
        }
        active_ = next;
        if (active_ != nullptr) {
            bus_.Publish(InteractionPromptShown{active_->id, active_->prompt_text_key,
                                                active_->target_event_id});
        }
    }
    if (confirm_pressed && !confirm_was_down_ && active_ != nullptr) {
        confirmed_events_.push_back(active_->target_event_id);
    }
    confirm_was_down_ = confirm_pressed;
}

std::vector<std::string> InteractionSystem::DrainConfirmedEvents() {
    std::vector<std::string> events;
    events.swap(confirmed_events_);
    return events;
}

} // namespace jrpgmaker::domain
