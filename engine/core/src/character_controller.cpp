#include "jrpgmaker/core/character_controller.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace jrpgmaker::core {

namespace {

constexpr float kEpsilon = 0.0001f;
constexpr int kMaxContacts = 3;

struct Hit {
    float time = 1.0f;
    glm::vec3 normal{0.0f};
    bool hit = false;
};

Hit SweepPointAgainstAabb(const glm::vec3& origin, const glm::vec3& delta, const Aabb& box) {
    float entry = 0.0f;
    float exit = 1.0f;
    glm::vec3 entry_normal{0.0f};

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(delta[axis]) < kEpsilon) {
            if (origin[axis] < box.min[axis] || origin[axis] > box.max[axis]) {
                return {};
            }
            continue;
        }

        const float inverse_delta = 1.0f / delta[axis];
        float near_time = (box.min[axis] - origin[axis]) * inverse_delta;
        float far_time = (box.max[axis] - origin[axis]) * inverse_delta;
        float near_normal = -1.0f;
        if (near_time > far_time) {
            std::swap(near_time, far_time);
            near_normal = 1.0f;
        }
        if (near_time > entry) {
            entry = near_time;
            entry_normal = glm::vec3(0.0f);
            entry_normal[axis] = near_normal;
        }
        exit = std::min(exit, far_time);
        if (entry > exit || exit < 0.0f || entry > 1.0f) {
            return {};
        }
    }

    return Hit{std::max(0.0f, entry), entry_normal, true};
}

bool Overlaps(const glm::vec3& position, float radius, float half_height, const Aabb& obstacle) {
    const float segment_half_height = std::max(0.0f, half_height - radius);
    const float segment_min_y = position.y - segment_half_height;
    const float segment_max_y = position.y + segment_half_height;
    const float horizontal_x =
        std::max({obstacle.min.x - position.x, 0.0f, position.x - obstacle.max.x});
    const float horizontal_z =
        std::max({obstacle.min.z - position.z, 0.0f, position.z - obstacle.max.z});
    const float vertical =
        std::max({obstacle.min.y - segment_max_y, 0.0f, segment_min_y - obstacle.max.y});
    return horizontal_x * horizontal_x + horizontal_z * horizontal_z + vertical * vertical <=
           radius * radius + kEpsilon;
}

glm::vec3 ContactNormal(const glm::vec3& position, float radius, float half_height,
                        const Aabb& obstacle) {
    const float segment_half_height = std::max(0.0f, half_height - radius);
    const float segment_min_y = position.y - segment_half_height;
    const float segment_max_y = position.y + segment_half_height;
    const float segment_y = segment_max_y < obstacle.min.y ? segment_max_y
                            : segment_min_y > obstacle.max.y
                                ? segment_min_y
                                : std::clamp(position.y, obstacle.min.y, obstacle.max.y);
    const glm::vec3 closest{std::clamp(position.x, obstacle.min.x, obstacle.max.x),
                            std::clamp(segment_y, obstacle.min.y, obstacle.max.y),
                            std::clamp(position.z, obstacle.min.z, obstacle.max.z)};
    const glm::vec3 segment_point{position.x, segment_y, position.z};
    const glm::vec3 difference = segment_point - closest;
    const float length_squared = glm::dot(difference, difference);
    if (length_squared > kEpsilon * kEpsilon) {
        return difference / std::sqrt(length_squared);
    }

    const float distances[] = {position.x - obstacle.min.x, obstacle.max.x - position.x,
                               position.y - obstacle.min.y, obstacle.max.y - position.y,
                               position.z - obstacle.min.z, obstacle.max.z - position.z};
    int nearest = 0;
    for (int i = 1; i < 6; ++i) {
        if (distances[i] < distances[nearest]) {
            nearest = i;
        }
    }
    glm::vec3 normal{0.0f};
    normal[nearest / 2] = nearest % 2 == 0 ? -1.0f : 1.0f;
    return normal;
}

Hit SweepCapsuleAgainstAabb(const glm::vec3& origin, const glm::vec3& delta, float radius,
                            float half_height, const Aabb& obstacle) {
    const Aabb broadphase{
        {obstacle.min.x - radius, obstacle.min.y - half_height, obstacle.min.z - radius},
        {obstacle.max.x + radius, obstacle.max.y + half_height, obstacle.max.z + radius}};
    if (!SweepPointAgainstAabb(origin, delta, broadphase).hit) {
        return {};
    }
    if (Overlaps(origin, radius, half_height, obstacle)) {
        return Hit{0.0f, ContactNormal(origin, radius, half_height, obstacle), true};
    }

    const float travel = std::sqrt(glm::dot(delta, delta));
    const int samples = std::clamp(static_cast<int>(std::ceil(travel / radius * 64.0f)), 64, 4096);
    float previous = 0.0f;
    for (int sample = 1; sample <= samples; ++sample) {
        const float current = static_cast<float>(sample) / static_cast<float>(samples);
        if (!Overlaps(origin + delta * current, radius, half_height, obstacle)) {
            previous = current;
            continue;
        }
        float low = previous;
        float high = current;
        for (int iteration = 0; iteration < 20; ++iteration) {
            const float middle = (low + high) * 0.5f;
            if (Overlaps(origin + delta * middle, radius, half_height, obstacle)) {
                high = middle;
            } else {
                low = middle;
            }
        }
        const glm::vec3 contact_position = origin + delta * high;
        return Hit{high, ContactNormal(contact_position, radius, half_height, obstacle), true};
    }
    return {};
}

} // namespace

CharacterController::CharacterController(CharacterControllerConfig config) : config_(config) {
    if (config_.radius <= 0.0f || config_.half_height < config_.radius) {
        throw std::invalid_argument("core: capsule half_height must be at least radius");
    }
    state_.position = config_.position;
}

void CharacterController::Move(glm::vec3 desired_horizontal_velocity, float delta_seconds,
                               const std::vector<Aabb>& obstacles) {
    if (delta_seconds <= 0.0f) {
        return;
    }

    state_.velocity.x = desired_horizontal_velocity.x;
    state_.velocity.z = desired_horizontal_velocity.z;
    state_.velocity.y += config_.gravity * delta_seconds;
    state_.grounded = false;
    state_.blocked = false;
    state_.collision_normal = glm::vec3(0.0f);

    glm::vec3 remaining = state_.velocity * delta_seconds;
    for (int contact = 0; contact < kMaxContacts; ++contact) {
        Hit best_hit;
        for (const Aabb& obstacle : obstacles) {
            const Hit hit = SweepCapsuleAgainstAabb(state_.position, remaining, config_.radius,
                                                    config_.half_height, obstacle);
            if (hit.hit && hit.time < best_hit.time) {
                best_hit = hit;
            }
        }

        if (!best_hit.hit) {
            state_.position += remaining;
            break;
        }

        const float remaining_length = std::sqrt(glm::dot(remaining, remaining));
        const float time_epsilon =
            remaining_length > kEpsilon ? kEpsilon / remaining_length : kEpsilon;
        state_.position += remaining * std::max(0.0f, best_hit.time - time_epsilon);
        remaining *= (1.0f - best_hit.time);
        remaining -= best_hit.normal * glm::dot(remaining, best_hit.normal);
        state_.collision_normal = best_hit.normal;
        state_.blocked = true;
        if (best_hit.normal.y > 0.5f) {
            state_.grounded = true;
            state_.velocity.y = 0.0f;
        } else if (best_hit.normal.y < -0.5f) {
            state_.velocity.y = 0.0f;
        } else {
            state_.velocity -= best_hit.normal * glm::dot(state_.velocity, best_hit.normal);
        }

        if (glm::dot(remaining, remaining) < kEpsilon * kEpsilon) {
            break;
        }
    }

    // A controller spawned inside an obstacle is moved to the nearest face.
    // This also prevents a malformed map from leaving the player permanently
    // embedded in geometry.
    for (const Aabb& obstacle : obstacles) {
        if (!Overlaps(state_.position, config_.radius, config_.half_height, obstacle)) {
            continue;
        }
        const float distances[] = {state_.position.x - (obstacle.min.x - config_.radius),
                                   (obstacle.max.x + config_.radius) - state_.position.x,
                                   state_.position.y - (obstacle.min.y - config_.half_height),
                                   (obstacle.max.y + config_.half_height) - state_.position.y,
                                   state_.position.z - (obstacle.min.z - config_.radius),
                                   (obstacle.max.z + config_.radius) - state_.position.z};
        int nearest = 0;
        for (int i = 1; i < 6; ++i) {
            if (distances[i] < distances[nearest]) {
                nearest = i;
            }
        }
        const int axis = nearest / 2;
        const float direction = nearest % 2 == 0 ? -1.0f : 1.0f;
        state_.position[axis] += direction * distances[nearest];
        state_.collision_normal = glm::vec3(0.0f);
        state_.collision_normal[axis] = direction;
        state_.blocked = true;
        if (axis == 1 && direction > 0.0f) {
            state_.grounded = true;
            state_.velocity.y = 0.0f;
        }
    }
}

} // namespace jrpgmaker::core
