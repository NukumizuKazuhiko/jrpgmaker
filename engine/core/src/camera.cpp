#include "jrpgmaker/core/camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace jrpgmaker::core {

glm::mat4 Camera::ViewMatrix() const {
    return glm::lookAt(eye, target, up);
}

glm::mat4 Camera::ProjectionMatrix() const {
    // GLM's default is right-handed, depth range [0,1] under
    // GLM_FORCE_DEPTH_ZERO_TO_ONE... not defined; the standard OpenGL-style
    // projection maps to NDC [-1,1], which D3D12 expects natively and the
    // Vulkan backend counteracts via the negative-height viewport. Keep the
    // default convention so both backends agree.
    return glm::perspective(glm::radians(fov_y_degrees), aspect_ratio, near_plane, far_plane);
}

} // namespace jrpgmaker::core