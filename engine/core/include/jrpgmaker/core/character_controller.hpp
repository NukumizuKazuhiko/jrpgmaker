#pragma once

#include <vector>

#include <glm/glm.hpp>

namespace jrpgmaker::core {

struct Aabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

struct CharacterControllerConfig {
    glm::vec3 position{0.0f};
    float radius = 0.5f;
    float half_height = 1.0f;
    float gravity = -9.81f;
};

struct CharacterControllerState {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 collision_normal{0.0f};
    bool grounded = false;
    bool blocked = false;
};

// Deterministic kinematic controller for a vertical capsule. Static obstacles
// are supplied by the world owner as AABBs; the controller owns no scene or
// platform state.
class CharacterController {
public:
    explicit CharacterController(CharacterControllerConfig config);

    void Move(glm::vec3 desired_horizontal_velocity, float delta_seconds,
              const std::vector<Aabb>& obstacles);

    const CharacterControllerState& state() const { return state_; }

private:
    CharacterControllerConfig config_;
    CharacterControllerState state_;
};

} // namespace jrpgmaker::core
