#pragma once

#include <glm/glm.hpp>

namespace jrpgmaker::core {

// Pinhole camera for fly/observation (P2). View looks down -Z from `eye`
// toward `target` with `up`; projection is a perspective frustum. GLM's
// right-handed convention matches glTF Y-up and the RHI NDC-Y contract, so
// the composed view-projection can be uploaded to the push-constant block
// byte-for-byte.
class Camera {
public:
    Camera() = default;

    glm::vec3 eye{0.0f, 0.0f, 3.0f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fov_y_degrees = 60.0f;
    float aspect_ratio = 1.0f;
    float near_plane = 0.1f;
    float far_plane = 100.0f;

    [[nodiscard]] glm::mat4 ViewMatrix() const;
    [[nodiscard]] glm::mat4 ProjectionMatrix() const;

    // Composed view-projection, column-major (matches HLSL float4x4 layout).
    [[nodiscard]] glm::mat4 ViewProjection() const { return ProjectionMatrix() * ViewMatrix(); }
};

} // namespace jrpgmaker::core