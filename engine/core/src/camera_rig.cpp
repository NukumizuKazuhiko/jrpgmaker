#include "jrpgmaker/core/camera_rig.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace jrpgmaker::core {

namespace {

bool Contains(const Aabb& bounds, glm::vec3 point) {
    return point.x >= bounds.min.x && point.x <= bounds.max.x && point.y >= bounds.min.y &&
           point.y <= bounds.max.y && point.z >= bounds.min.z && point.z <= bounds.max.z;
}

} // namespace

CameraRig::CameraRig(ThirdPersonCameraConfig config) : config_(config) {}

void CameraRig::Update(glm::vec3 target_position, float delta_seconds,
                       const std::vector<FixedCameraRegion>& regions) {
    const FixedCameraRegion* selected = nullptr;
    for (const FixedCameraRegion& region : regions) {
        if (!Contains(region.bounds, target_position) ||
            (selected != nullptr && region.priority <= selected->priority)) {
            continue;
        }
        selected = &region;
    }

    Camera desired;
    const std::string desired_region_id = selected != nullptr ? selected->id : std::string{};
    if (selected != nullptr) {
        desired = selected->camera;
    } else {
        const float pitch = glm::radians(config_.pitch_degrees);
        desired.target = target_position + glm::vec3(0.0f, config_.height, 0.0f);
        desired.eye = desired.target + glm::vec3(0.0f, std::sin(pitch) * config_.distance,
                                                 std::cos(pitch) * config_.distance);
    }

    if (desired_region_id != active_region_id_) {
        active_region_id_ = desired_region_id;
        transition_start_ = camera_;
        transition_elapsed_ = 0.0f;
        transition_active_ = config_.smoothing_seconds > 0.0f;
    }

    if (transition_active_ && delta_seconds > 0.0f) {
        transition_elapsed_ += delta_seconds;
        const float alpha = std::min(1.0f, transition_elapsed_ / config_.smoothing_seconds);
        camera_.eye = glm::mix(transition_start_.eye, desired.eye, alpha);
        camera_.target = glm::mix(transition_start_.target, desired.target, alpha);
        camera_.up = desired.up;
        camera_.fov_y_degrees = desired.fov_y_degrees;
        camera_.aspect_ratio = desired.aspect_ratio;
        camera_.near_plane = desired.near_plane;
        camera_.far_plane = desired.far_plane;
        transition_active_ = alpha < 1.0f;
    } else if (config_.smoothing_seconds > 0.0f && delta_seconds > 0.0f) {
        const float alpha = std::min(1.0f, delta_seconds / config_.smoothing_seconds);
        camera_.eye += (desired.eye - camera_.eye) * alpha;
        camera_.target += (desired.target - camera_.target) * alpha;
        camera_.up = desired.up;
        camera_.fov_y_degrees = desired.fov_y_degrees;
        camera_.aspect_ratio = desired.aspect_ratio;
        camera_.near_plane = desired.near_plane;
        camera_.far_plane = desired.far_plane;
    } else {
        camera_ = desired;
    }
}

} // namespace jrpgmaker::core
