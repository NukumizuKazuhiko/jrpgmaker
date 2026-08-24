#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "jrpgmaker/core/mesh.hpp"

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

} // namespace jrpgmaker::assetimport