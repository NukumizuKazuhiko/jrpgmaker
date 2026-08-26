#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "jrpgmaker/core/event_bus.hpp"
#include "jrpgmaker/domain/event_script.hpp"

namespace jrpgmaker::domain {

struct InteractionPoint {
    std::string id;
    glm::vec3 position{0.0f};
    float radius = 1.0f;
    std::string prompt_text_key;
    std::string target_event_id;
};

struct InteractionPromptShown {
    std::string id;
    std::string prompt_text_key;
    std::string target_event_id;
};

struct InteractionPromptHidden {
    std::string id;
};

std::vector<InteractionPoint> ParseInteractionPoints(const nlohmann::json& document);

// Validates cross-file interaction -> event references at the data boundary.
// The runtime must never turn a dangling target into a silent no-op.
void ValidateInteractionTargets(const std::vector<InteractionPoint>& points,
                                const EventScript& event_script);

// Selects the nearest overlapping interaction and publishes only transitions.
// Confirmed target events are queued and must be drained at an event boundary.
class InteractionSystem {
public:
    InteractionSystem(const std::vector<InteractionPoint>& points, core::EventBus& bus)
        : points_(points), bus_(bus) {}

    void Update(glm::vec3 player_position, bool confirm_pressed);
    std::vector<std::string> DrainConfirmedEvents();
    const InteractionPoint* active() const { return active_; }

private:
    const std::vector<InteractionPoint>& points_;
    core::EventBus& bus_;
    const InteractionPoint* active_ = nullptr;
    std::vector<std::string> confirmed_events_;
    bool confirm_was_down_ = false;
};

} // namespace jrpgmaker::domain
