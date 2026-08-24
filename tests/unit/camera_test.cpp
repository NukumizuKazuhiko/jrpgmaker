#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "jrpgmaker/core/camera.hpp"

using jrpgmaker::core::Camera;

namespace {

glm::vec3 TranslationOf(const glm::mat4& matrix) {
    return glm::vec3(matrix[3][0], matrix[3][1], matrix[3][2]);
}

// Maps a world point through the camera's view-projection and returns NDC.
glm::vec4 ProjectPoint(const Camera& camera, const glm::vec3& world) {
    const glm::vec4 clip = camera.ViewProjection() * glm::vec4(world, 1.0f);
    if (clip.w == 0.0f) {
        return glm::vec4(0.0f);
    }
    return clip / clip.w;
}

} // namespace

TEST_CASE("camera view matrix looks from eye toward target", "[core][camera]") {
    Camera camera;
    camera.eye = {0.0f, 0.0f, 5.0f};
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};

    // The view matrix translates the world so the eye is at the origin.
    const glm::mat4 view = camera.ViewMatrix();
    REQUIRE(TranslationOf(view) == glm::vec3(0.0f, 0.0f, -5.0f));
}

TEST_CASE("camera projects a point in front of the eye onto the view plane", "[core][camera]") {
    Camera camera;
    camera.eye = {0.0f, 0.0f, 5.0f};
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.aspect_ratio = 1.0f;

    // Origin is directly ahead (along -Z view axis): NDC xy == 0.
    const glm::vec4 ndc = ProjectPoint(camera, {0.0f, 0.0f, 0.0f});
    REQUIRE(ndc.x == Catch::Approx(0.0f).margin(1e-4f));
    REQUIRE(ndc.y == Catch::Approx(0.0f).margin(1e-4f));
    // Perspective divides by -view_z; points closer than near plane are clipped.
    REQUIRE(ndc.z < 1.0f);
}

TEST_CASE("camera projection preserves the NDC-Y direction of the view axis", "[core][camera]") {
    Camera camera;
    camera.eye = {0.0f, 0.0f, 5.0f};
    camera.target = {0.0f, 0.0f, 0.0f};

    // A point above the view axis (world +Y) must project above NDC y=0.
    const glm::vec4 above = ProjectPoint(camera, {0.0f, 0.5f, 0.0f});
    const glm::vec4 below = ProjectPoint(camera, {0.0f, -0.5f, 0.0f});
    REQUIRE(above.y > 0.0f);
    REQUIRE(below.y < 0.0f);
}

TEST_CASE("camera far plane grows the view frustum", "[core][camera]") {
    Camera camera;
    camera.eye = {0.0f, 0.0f, 0.0f};
    camera.target = {0.0f, 0.0f, -1.0f};

    // Projection near/far affect depth only, not NDC xy for a centered point.
    const glm::vec4 near_ndc = ProjectPoint(camera, {0.0f, 0.0f, -1.0f});
    const glm::vec4 far_ndc = ProjectPoint(camera, {0.0f, 0.0f, -10.0f});
    REQUIRE(near_ndc.x == Catch::Approx(0.0f).margin(1e-4f));
    REQUIRE(far_ndc.x == Catch::Approx(0.0f).margin(1e-4f));
}