#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include "jrpgmaker/core/character_controller.hpp"

using jrpgmaker::core::Aabb;
using jrpgmaker::core::CharacterController;
using jrpgmaker::core::CharacterControllerConfig;

TEST_CASE("character controller moves by the requested horizontal velocity", "[core][controller]") {
    CharacterController controller(CharacterControllerConfig{
        .position = {0.0f, 1.0f, 0.0f}, .radius = 0.25f, .half_height = 0.75f, .gravity = 0.0f});

    controller.Move({2.0f, 0.0f, -1.0f}, 0.5f, {});

    const auto& state = controller.state();
    REQUIRE(state.position.x == Catch::Approx(1.0f));
    REQUIRE(state.position.y == Catch::Approx(1.0f).margin(0.001f));
    REQUIRE(state.position.z == Catch::Approx(-0.5f));
    REQUIRE_FALSE(state.grounded);
    REQUIRE_FALSE(state.blocked);
}

TEST_CASE("character controller falls onto an AABB ground", "[core][controller]") {
    CharacterController controller(CharacterControllerConfig{
        .position = {0.0f, 3.0f, 0.0f}, .radius = 0.5f, .half_height = 1.0f, .gravity = -10.0f});
    const Aabb ground{{-10.0f, -0.1f, -10.0f}, {10.0f, 0.0f, 10.0f}};

    controller.Move({}, 1.0f, {ground});

    const auto& state = controller.state();
    REQUIRE(state.position.y == Catch::Approx(1.0f).margin(0.001f));
    REQUIRE(state.velocity.y == Catch::Approx(0.0f));
    REQUIRE(state.grounded);
    REQUIRE(state.collision_normal.y == Catch::Approx(1.0f));
}

TEST_CASE("character controller stops at a wall and slides along it", "[core][controller]") {
    CharacterController controller(CharacterControllerConfig{
        .position = {0.0f, 1.0f, 0.0f}, .radius = 0.5f, .half_height = 1.0f, .gravity = 0.0f});
    const Aabb wall{{1.0f, -1.0f, -10.0f}, {2.0f, 3.0f, 10.0f}};

    controller.Move({4.0f, 0.0f, 2.0f}, 1.0f, {wall});

    const auto& state = controller.state();
    REQUIRE(state.position.x == Catch::Approx(0.5f).margin(0.001f));
    REQUIRE(state.position.z == Catch::Approx(2.0f).margin(0.001f));
    REQUIRE(state.blocked);
    REQUIRE(state.collision_normal.x == Catch::Approx(-1.0f));
}

TEST_CASE("character controller preserves capsule clearance at an obstacle corner",
          "[core][controller]") {
    CharacterController controller(CharacterControllerConfig{
        .position = {0.0f, 1.0f, 0.6f}, .radius = 0.5f, .half_height = 0.75f, .gravity = 0.0f});
    const Aabb corner{{1.0f, -1.0f, 1.0f}, {2.0f, 3.0f, 2.0f}};

    controller.Move({0.6f, 0.0f, 0.0f}, 1.0f, {corner});

    REQUIRE(controller.state().position.x == Catch::Approx(0.6f).margin(0.001f));
    REQUIRE_FALSE(controller.state().blocked);
}
