#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/core/camera_rig.hpp"

using jrpgmaker::core::Camera;
using jrpgmaker::core::CameraRig;
using jrpgmaker::core::FixedCameraRegion;

TEST_CASE("camera rig follows the target outside fixed regions", "[core][camera]") {
    CameraRig rig;
    rig.Update({1.0f, 0.0f, 2.0f}, 1.0f, {});

    REQUIRE(rig.active_region_id().empty());
    REQUIRE(rig.camera().target == glm::vec3(1.0f, 2.0f, 2.0f));
    REQUIRE(rig.camera().eye.z == Catch::Approx(2.0f + 4.0f * std::cos(glm::radians(15.0f))));
}

TEST_CASE("camera rig selects the highest priority fixed region", "[core][camera]") {
    Camera fixed_a;
    fixed_a.eye = {10.0f, 10.0f, 10.0f};
    Camera fixed_b;
    fixed_b.eye = {20.0f, 20.0f, 20.0f};
    CameraRig rig;

    rig.Update({0.0f, 0.0f, 0.0f}, 1.0f,
               {FixedCameraRegion{"low", {glm::vec3(-1.0f), glm::vec3(1.0f)}, fixed_a, 1},
                FixedCameraRegion{"high", {glm::vec3(-1.0f), glm::vec3(1.0f)}, fixed_b, 2}});

    REQUIRE(rig.active_region_id() == "high");
    REQUIRE(rig.camera().eye == fixed_b.eye);
}

TEST_CASE("camera rig completes a region transition after its declared duration",
          "[core][camera]") {
    Camera fixed;
    fixed.eye = {10.0f, 10.0f, 10.0f};
    fixed.target = {1.0f, 1.0f, 1.0f};
    CameraRig rig(jrpgmaker::core::ThirdPersonCameraConfig{
        .distance = 4.0f, .height = 2.0f, .pitch_degrees = 15.0f, .smoothing_seconds = 0.1f});
    const FixedCameraRegion region{"fixed", {glm::vec3(-1.0f), glm::vec3(1.0f)}, fixed, 1};

    rig.Update({0.0f, 0.0f, 0.0f}, 0.05f, {region});
    rig.Update({0.0f, 0.0f, 0.0f}, 0.05f, {region});

    REQUIRE(rig.camera().eye == fixed.eye);
    REQUIRE(rig.camera().target == fixed.target);
}
