#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "jrpgmaker/core/camera.hpp"
#include "jrpgmaker/core/character_controller.hpp"

namespace jrpgmaker::core {

struct ThirdPersonCameraConfig {
    float distance = 4.0f;
    float height = 2.0f;
    float pitch_degrees = 15.0f;
    float smoothing_seconds = 0.0f;
};

struct FixedCameraRegion {
    std::string id;
    Aabb bounds;
    Camera camera;
    int priority = 0;
};

class CameraRig {
public:
    explicit CameraRig(ThirdPersonCameraConfig config = {});

    void Update(glm::vec3 target_position, float delta_seconds,
                const std::vector<FixedCameraRegion>& regions);

    const Camera& camera() const { return camera_; }
    const std::string& active_region_id() const { return active_region_id_; }

private:
    ThirdPersonCameraConfig config_;
    Camera camera_;
    std::string active_region_id_;
    Camera transition_start_;
    float transition_elapsed_ = 0.0f;
    bool transition_active_ = false;
};

} // namespace jrpgmaker::core
