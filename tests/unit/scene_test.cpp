#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "jrpgmaker/core/scene.hpp"

using jrpgmaker::core::Entity;
using jrpgmaker::core::Scene;
using jrpgmaker::core::Transform;

namespace {

// Extracts the translation column from a column-major GLM matrix.
glm::vec3 TranslationOf(const glm::mat4& matrix) {
    return glm::vec3(matrix[3][0], matrix[3][1], matrix[3][2]);
}

} // namespace

TEST_CASE("scene entity without transform has identity world matrix", "[core][scene]") {
    Scene scene;
    const Entity entity = scene.CreateEntity();
    const glm::mat4 world = scene.WorldMatrix(entity);
    REQUIRE(world == glm::mat4(1.0f));
}

TEST_CASE("scene world matrix equals local transform for a root entity", "[core][scene]") {
    Scene scene;
    const Entity entity = scene.CreateEntity();
    scene.Registry().emplace<Transform>(entity, Transform{.translation = {1.0f, 2.0f, 3.0f}});

    const glm::mat4 world = scene.WorldMatrix(entity);
    REQUIRE(TranslationOf(world) == glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST_CASE("scene composes parent translation into child world matrix", "[core][scene]") {
    Scene scene;
    const Entity parent = scene.CreateEntity();
    const Entity child = scene.CreateEntity();
    scene.Registry().emplace<Transform>(parent, Transform{.translation = {10.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<Transform>(child, Transform{.translation = {1.0f, 0.0f, 0.0f}});
    scene.SetParent(child, parent);

    const glm::mat4 child_world = scene.WorldMatrix(child);
    REQUIRE(TranslationOf(child_world) == glm::vec3(11.0f, 0.0f, 0.0f));
}

TEST_CASE("scene composes rotation from parent into child world", "[core][scene]") {
    Scene scene;
    const Entity parent = scene.CreateEntity();
    const Entity child = scene.CreateEntity();
    // Parent rotates 90 degrees around Z; child is at +X in parent space.
    scene.Registry().emplace<Transform>(
        parent, Transform{.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1))});
    scene.Registry().emplace<Transform>(child, Transform{.translation = {1.0f, 0.0f, 0.0f}});
    scene.SetParent(child, parent);

    const glm::mat4 child_world = scene.WorldMatrix(child);
    const glm::vec3 world_pos = TranslationOf(child_world);
    REQUIRE(world_pos.x == Catch::Approx(0.0f).margin(1e-5f));
    REQUIRE(world_pos.y == Catch::Approx(1.0f).margin(1e-5f));
    REQUIRE(world_pos.z == Catch::Approx(0.0f).margin(1e-5f));
}

TEST_CASE("scene composes scale from parent into child world", "[core][scene]") {
    Scene scene;
    const Entity parent = scene.CreateEntity();
    const Entity child = scene.CreateEntity();
    scene.Registry().emplace<Transform>(parent, Transform{.scale = {2.0f, 2.0f, 2.0f}});
    scene.Registry().emplace<Transform>(child, Transform{.translation = {1.0f, 0.0f, 0.0f}});
    scene.SetParent(child, parent);

    const glm::mat4 child_world = scene.WorldMatrix(child);
    REQUIRE(TranslationOf(child_world) == glm::vec3(2.0f, 0.0f, 0.0f));
}

TEST_CASE("scene supports a three-level hierarchy", "[core][scene]") {
    Scene scene;
    const Entity root = scene.CreateEntity();
    const Entity mid = scene.CreateEntity();
    const Entity leaf = scene.CreateEntity();
    scene.Registry().emplace<Transform>(root, Transform{.translation = {1.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<Transform>(mid, Transform{.translation = {2.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<Transform>(leaf, Transform{.translation = {4.0f, 0.0f, 0.0f}});
    scene.SetParent(mid, root);
    scene.SetParent(leaf, mid);

    REQUIRE(TranslationOf(scene.WorldMatrix(leaf)) == glm::vec3(7.0f, 0.0f, 0.0f));
}

TEST_CASE("scene detach makes an entity a root again", "[core][scene]") {
    Scene scene;
    const Entity parent = scene.CreateEntity();
    const Entity child = scene.CreateEntity();
    scene.Registry().emplace<Transform>(parent, Transform{.translation = {10.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<Transform>(child, Transform{.translation = {1.0f, 0.0f, 0.0f}});
    scene.SetParent(child, parent);
    scene.Detach(child);

    REQUIRE(TranslationOf(scene.WorldMatrix(child)) == glm::vec3(1.0f, 0.0f, 0.0f));
}

TEST_CASE("scene children_of returns direct children only", "[core][scene]") {
    Scene scene;
    const Entity parent = scene.CreateEntity();
    const Entity child_a = scene.CreateEntity();
    const Entity child_b = scene.CreateEntity();
    const Entity grandchild = scene.CreateEntity();
    scene.SetParent(child_a, parent);
    scene.SetParent(child_b, parent);
    scene.SetParent(grandchild, child_a);

    const std::vector<Entity> children = scene.ChildrenOf(parent);
    REQUIRE(children.size() == 2);
    REQUIRE((children[0] == child_a || children[0] == child_b));
    REQUIRE((children[1] == child_a || children[1] == child_b));
}

TEST_CASE("scene reparenting replaces the previous parent link", "[core][scene]") {
    Scene scene;
    const Entity parent_a = scene.CreateEntity();
    const Entity parent_b = scene.CreateEntity();
    const Entity child = scene.CreateEntity();
    scene.Registry().emplace<Transform>(parent_a, Transform{.translation = {10.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<Transform>(parent_b, Transform{.translation = {100.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<Transform>(child, Transform{});
    scene.SetParent(child, parent_a);
    scene.SetParent(child, parent_b);

    REQUIRE(TranslationOf(scene.WorldMatrix(child)) == glm::vec3(100.0f, 0.0f, 0.0f));
}