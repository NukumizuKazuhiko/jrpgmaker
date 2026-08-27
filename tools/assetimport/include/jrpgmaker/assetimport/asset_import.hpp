#pragma once

#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec4.hpp>

#include "jrpgmaker/core/animation.hpp"
#include "jrpgmaker/core/asset.hpp"
#include "jrpgmaker/core/mesh.hpp"
#include "jrpgmaker/core/scene.hpp"

namespace jrpgmaker::assetimport {

// Loads a glTF 2.0 file into CPU-side mesh data (P2: static mesh, single
// primitive, POSITION + indices). Returns nullopt with a message on failure.
struct GltfLoadError {
    std::string message;
};

// Reads the first mesh in the glTF file. cgltf owns no global state; every
// call parses and loads buffers independently (no caching in v0).
std::optional<core::MeshData> LoadGltfMesh(const std::filesystem::path& path,
                                           GltfLoadError* error = nullptr);

// Mesh reference attached to a scene entity: a handle into SceneLoad::assets.
struct MeshRef {
    core::AssetHandle handle{core::AssetHandle::kInvalid};
};

// Skeleton reference attached to a node that carries a skinned mesh (glTF
// node.skin). Indexes into SceneLoad::skeletons.
struct SkinRef {
    std::size_t skeleton_index = 0;
};

// Generic glTF material association. The render-style adapter decides how to
// interpret the imported material values; assetimport does not select a style.
struct MaterialRef {
    std::size_t material_index = 0;
};

struct TextureAsset {
    std::string name;
    std::string source_uri;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba8;

    [[nodiscard]] bool decoded() const { return !rgba8.empty(); }
};

struct MaterialAsset {
    std::string name;
    glm::vec4 base_color_factor{1.0f};
    float metallic_factor = 1.0f;
    float roughness_factor = 1.0f;
    std::size_t base_color_texture = std::numeric_limits<std::size_t>::max();
};

// Skeleton data imported from a glTF skin: the joint hierarchy (with names and
// parents) plus per-joint inverse-bind matrices. `joint_nodes` maps joint index
// -> glTF node index, for callers that need node correspondence.
struct SkeletonAsset {
    core::Skeleton skeleton;
    std::vector<std::size_t> joint_nodes;
};

// Animation clip imported from a glTF animation (P4: all channels/samplers,
// LINEAR/STEP/CUBICSPLINE).
struct AnimationAsset {
    core::AnimationClip clip;
};

// Result of a glTF scene import: the EnTT scene graph (Transform/Parent
// components on entities, MeshRef on nodes with meshes) plus the mesh asset
// pool owned by the asset registry. World matrices compose through
// Scene::WorldMatrix. Unload meshes via AssetRegistry::Unregister.
struct SceneLoad {
    core::Scene scene;
    core::AssetRegistry assets;
    std::vector<SkeletonAsset> skeletons;
    std::vector<AnimationAsset> animations;
    std::vector<TextureAsset> textures;
    std::vector<MaterialAsset> materials;

    // Maps glTF node index -> scene entity, for callers that need to keep the
    // correspondence (e.g. naming or traversal). Empty if the file has no nodes.
    std::vector<core::Entity> node_entities;
};

// Imports a glTF 2.0 file into a runtime Scene (P4: node hierarchy + TRS +
// mesh references + skins + animations). Returns nullopt with a message on failure.
std::optional<SceneLoad> LoadGltfScene(const std::filesystem::path& path,
                                       GltfLoadError* error = nullptr);

} // namespace jrpgmaker::assetimport
