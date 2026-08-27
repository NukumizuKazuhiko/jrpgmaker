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

TEST_CASE("gltf skin and animation import produces a skinned mesh",
          "[assetimport][p4][cubic-spline]") {
    const std::filesystem::path path = AssetPath("art/meshes/arm_skinned.gltf");
    REQUIRE(std::filesystem::exists(path));

    GltfLoadError error;
    const std::optional<jrpgmaker::assetimport::SceneLoad> load = LoadGltfScene(path, &error);
    INFO(error.message);
    REQUIRE(load.has_value());

    // One skinned mesh: 8 vertices with JOINTS_0/WEIGHTS_0, 12 indices.
    REQUIRE(load->assets.live_count() == 1);
    bool found_skinned = false;
    const auto view = load->scene.Registry().view<MeshRef>();
    for (const Entity entity : view) {
        const MeshRef& ref = view.get<MeshRef>(entity);
        const MeshData* mesh = load->assets.FindMesh(ref.handle);
        REQUIRE(mesh != nullptr);
        if (mesh->skinned()) {
            found_skinned = true;
            // The mesh node referencing the skin must carry a SkinRef (glTF
            // node.skin), otherwise the render layer would skip the draw.
            REQUIRE(load->scene.Registry().all_of<jrpgmaker::assetimport::SkinRef>(entity));
            REQUIRE(mesh->vertex_count() == 8u);
            REQUIRE(mesh->index_count() == 12u);
            REQUIRE(mesh->joints.size() == mesh->vertex_count() * 4u);
            REQUIRE(mesh->weights.size() == mesh->vertex_count() * 4u);
            // First four vertices belong to joint 0 (weight 1), last four to joint 1.
            for (std::size_t v = 0; v < 4u; ++v) {
                REQUIRE(mesh->joints[v * 4u] == 0u);
                REQUIRE(mesh->weights[v * 4u] == 1.0f);
            }
            for (std::size_t v = 4u; v < 8u; ++v) {
                REQUIRE(mesh->joints[v * 4u] == 1u);
                REQUIRE(mesh->weights[v * 4u] == 1.0f);
            }
        }
    }
    REQUIRE(found_skinned);

    // Skeleton: two joints, elbow parented under root.
    REQUIRE(load->skeletons.size() == 1u);
    const jrpgmaker::core::Skeleton& skeleton = load->skeletons.front().skeleton;
    REQUIRE(skeleton.joint_count() == 2u);
    REQUIRE(skeleton.joints()[0].parent == jrpgmaker::core::kNullJoint);
    REQUIRE(skeleton.joints()[1].parent == 0);

    // Animations: idle + wave + cubic clips bound to the elbow joint.
    REQUIRE(load->animations.size() == 3u);
    const auto& idle = load->animations[0].clip;
    const auto& wave = load->animations[1].clip;
    const auto& cubic = load->animations[2].clip;
    REQUIRE(idle.channels.size() == 1u);
    REQUIRE(wave.channels.size() == 1u);
    REQUIRE(wave.duration_seconds > 0.0f);
    REQUIRE(cubic.channels.size() == 1u);
    REQUIRE(cubic.channels[0].interpolation == jrpgmaker::core::AnimInterpolation::kCubicSpline);
    REQUIRE(cubic.channels[0].times.size() == 2u);
    REQUIRE(cubic.channels[0].values.size() == 24u);
}

TEST_CASE("gltf scene import builds a parent-child hierarchy", "[core][assetimport]") {
    const std::filesystem::path path = AssetPath("art/meshes/scene_hierarchy.gltf");
    REQUIRE(std::filesystem::exists(path));

    GltfLoadError error;
    const std::optional<jrpgmaker::assetimport::SceneLoad> load = LoadGltfScene(path, &error);
    REQUIRE(load.has_value());

    REQUIRE(load->assets.live_count() == 1);
    REQUIRE(load->node_entities.size() == 2);

    const Entity root = load->node_entities[0];
    const Entity child = load->node_entities[1];
    REQUIRE(root != jrpgmaker::core::kNullEntity);
    REQUIRE(child != jrpgmaker::core::kNullEntity);

    // Root has translation (0.25,0,0) and scale (0.5); child is parented under
    // root.
    const Transform& root_transform = load->scene.Registry().get<Transform>(root);
    REQUIRE(TranslationOf(glm::mat4_cast(root_transform.rotation)) == glm::vec3(0.0f));
    REQUIRE(root_transform.translation == glm::vec3(0.25f, 0.0f, 0.0f));
    REQUIRE(root_transform.scale == glm::vec3(0.5f, 0.5f, 0.5f));

    const Parent* child_parent = load->scene.Registry().try_get<Parent>(child);
    REQUIRE(child_parent != nullptr);
    REQUIRE(child_parent->parent == root);

    // Mesh reference on the child resolves into the asset registry.
    const MeshRef* ref = load->scene.Registry().try_get<MeshRef>(child);
    REQUIRE(ref != nullptr);
    REQUIRE(ref->handle != jrpgmaker::core::AssetHandle::kInvalid);
    const MeshData* mesh = load->assets.FindMesh(ref->handle);
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->vertex_count() == 3);
}

TEST_CASE("gltf scene import composes world transforms through the hierarchy",
          "[core][assetimport]") {
    const std::filesystem::path path = AssetPath("art/meshes/scene_hierarchy.gltf");
    REQUIRE(std::filesystem::exists(path));

    const std::optional<jrpgmaker::assetimport::SceneLoad> load = LoadGltfScene(path);
    REQUIRE(load.has_value());

    const Entity child = load->node_entities[1];
    REQUIRE(child != jrpgmaker::core::kNullEntity);

    // Child local translation (0.4,0,0) composed with root translation
    // (0.25,0,0) and scale (0.5) yields a world position of (0.45,0,0).
    const glm::mat4 child_world = load->scene.WorldMatrix(child);
    const glm::vec3 world_pos = TranslationOf(child_world);
    REQUIRE(world_pos.x == Catch::Approx(0.45f).margin(1e-5f));
    REQUIRE(world_pos.y == Catch::Approx(0.0f).margin(1e-5f));
    REQUIRE(world_pos.z == Catch::Approx(0.0f).margin(1e-5f));
}

TEST_CASE("gltf scene import preserves generic material data", "[core][assetimport][p6]") {
    jrpgmaker::assetimport::GltfLoadError error;
    const std::optional<jrpgmaker::assetimport::SceneLoad> load =
        LoadGltfScene(AssetPath("art/meshes/triangle.gltf"), &error);
    INFO(error.message);
    REQUIRE(load.has_value());
    REQUIRE(load->textures.size() == 1u);
    REQUIRE(load->textures[0].name == "triangle_albedo");
    REQUIRE(load->textures[0].source_uri == "triangle_albedo.ppm");
    REQUIRE(load->textures[0].decoded());
    REQUIRE(load->textures[0].width == 1u);
    REQUIRE(load->textures[0].height == 1u);
    REQUIRE(load->textures[0].rgba8 == std::vector<std::uint8_t>{65u, 66u, 67u, 255u});
    REQUIRE(load->materials.size() == 1u);
    const auto& material = load->materials.front();
    REQUIRE(material.name == "triangle_material");
    REQUIRE(material.base_color_factor == glm::vec4(0.2f, 0.4f, 0.8f, 1.0f));
    REQUIRE(material.metallic_factor == 0.25f);
    REQUIRE(material.roughness_factor == 0.75f);
    REQUIRE(material.base_color_texture == 0u);

    const Entity node = load->node_entities.front();
    const auto* material_ref =
        load->scene.Registry().try_get<jrpgmaker::assetimport::MaterialRef>(node);
    REQUIRE(material_ref != nullptr);
    REQUIRE(material_ref->material_index == 0u);
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
