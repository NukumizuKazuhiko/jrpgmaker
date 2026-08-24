#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

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

// Mesh reference attached to a scene entity (index into SceneLoad::meshes).
struct MeshRef {
    std::size_t mesh_index = 0;
};

// Result of a glTF scene import: the EnTT scene graph (Transform/Parent
// components on entities, MeshRef on nodes with meshes) plus the mesh data
// pool. World matrices compose through Scene::WorldMatrix.
struct SceneLoad {
    core::Scene scene;
    std::vector<core::MeshData> meshes;

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