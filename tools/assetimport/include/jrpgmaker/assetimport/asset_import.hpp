#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

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

// Result of a glTF scene import: the EnTT scene graph (Transform/Parent
// components on entities, MeshRef on nodes with meshes) plus the mesh asset
// pool owned by the asset registry. World matrices compose through
// Scene::WorldMatrix. Unload meshes via AssetRegistry::Unregister.
struct SceneLoad {
    core::Scene scene;
    core::AssetRegistry assets;

    // Maps glTF node index -> scene entity, for callers that need to keep the
    // correspondence (e.g. naming or traversal). Empty if the file has no nodes.
    std::vector<core::Entity> node_entities;
};

// Imports a glTF 2.0 file into a runtime Scene (P2: static scene, node
// hierarchy + TRS + mesh references; animations and skins are out of scope).
// Returns nullopt with a message on failure.
std::optional<SceneLoad> LoadGltfScene(const std::filesystem::path& path,
                                       GltfLoadError* error = nullptr);

} // namespace jrpgmaker::assetimport