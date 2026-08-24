#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "jrpgmaker/assetimport/asset_import.hpp"
#include "jrpgmaker/core/scene.hpp"

namespace {

using jrpgmaker::assetimport::GltfLoadError;
using jrpgmaker::assetimport::LoadGltfMesh;
using jrpgmaker::assetimport::LoadGltfScene;
using jrpgmaker::assetimport::MeshRef;
using jrpgmaker::core::Entity;
using jrpgmaker::core::MeshData;
using jrpgmaker::core::Parent;
using jrpgmaker::core::Transform;

#ifndef JRPGMAKER_ASSET_DIR
#error "JRPGMAKER_ASSET_DIR must be defined by the build"
#endif

std::filesystem::path AssetPath(const char* relative) {
    return std::filesystem::path(JRPGMAKER_ASSET_DIR) / relative;
}

glm::vec3 TranslationOf(const glm::mat4& matrix) {
    return glm::vec3(matrix[3][0], matrix[3][1], matrix[3][2]);
}

} // namespace

TEST_CASE("gltf scene import builds a parent-child hierarchy", "[core][assetimport]") {
    const std::filesystem::path path = AssetPath("art/meshes/scene_hierarchy.gltf");
    REQUIRE(std::filesystem::exists(path));

    GltfLoadError error;
    const std::optional<jrpgmaker::assetimport::SceneLoad> load = LoadGltfScene(path, &error);
    REQUIRE(load.has_value());

    REQUIRE(load->meshes.size() == 1);
    REQUIRE(load->node_entities.size() == 2);

    const Entity root = load->node_entities[0];
    const Entity child = load->node_entities[1];
    REQUIRE(root != jrpgmaker::core::kNullEntity);
    REQUIRE(child != jrpgmaker::core::kNullEntity);

    // Root has translation (5,0,0); child is parented under root.
    const Transform& root_transform = load->scene.Registry().get<Transform>(root);
    REQUIRE(TranslationOf(glm::mat4_cast(root_transform.rotation)) == glm::vec3(0.0f));
    REQUIRE(root_transform.translation == glm::vec3(5.0f, 0.0f, 0.0f));

    const Parent* child_parent = load->scene.Registry().try_get<Parent>(child);
    REQUIRE(child_parent != nullptr);
    REQUIRE(child_parent->parent == root);

    // Mesh reference on the child resolves into the mesh pool.
    const MeshRef* ref = load->scene.Registry().try_get<MeshRef>(child);
    REQUIRE(ref != nullptr);
    REQUIRE(ref->mesh_index == 0);
    REQUIRE(load->meshes[ref->mesh_index].vertex_count() == 3);
}

TEST_CASE("gltf scene import composes world transforms through the hierarchy",
          "[core][assetimport]") {
    const std::filesystem::path path = AssetPath("art/meshes/scene_hierarchy.gltf");
    REQUIRE(std::filesystem::exists(path));

    const std::optional<jrpgmaker::assetimport::SceneLoad> load = LoadGltfScene(path);
    REQUIRE(load.has_value());

    const Entity child = load->node_entities[1];
    REQUIRE(child != jrpgmaker::core::kNullEntity);

    // Child local translation (1,0,0) composed with root translation (5,0,0)
    // yields a world position of (6,0,0).
    const glm::mat4 child_world = load->scene.WorldMatrix(child);
    const glm::vec3 world_pos = TranslationOf(child_world);
    REQUIRE(world_pos.x == Catch::Approx(6.0f).margin(1e-5f));
    REQUIRE(world_pos.y == Catch::Approx(0.0f).margin(1e-5f));
    REQUIRE(world_pos.z == Catch::Approx(0.0f).margin(1e-5f));
}

TEST_CASE("gltf import reports a clear error for a missing file", "[core][assetimport]") {
    GltfLoadError error;
    const std::optional<MeshData> mesh =
        LoadGltfMesh(AssetPath("art/meshes/does_not_exist.gltf"), &error);
    REQUIRE_FALSE(mesh.has_value());
    REQUIRE_FALSE(error.message.empty());
}

TEST_CASE("gltf import reports an error without a message sink", "[core][assetimport]") {
    const std::optional<MeshData> mesh = LoadGltfMesh(AssetPath("art/meshes/does_not_exist.gltf"));
    REQUIRE_FALSE(mesh.has_value());
}