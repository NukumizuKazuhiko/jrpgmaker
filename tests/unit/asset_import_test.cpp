#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/assetimport/asset_import.hpp"

namespace {

using jrpgmaker::assetimport::GltfLoadError;
using jrpgmaker::assetimport::LoadGltfMesh;
using jrpgmaker::core::MeshData;

#ifndef JRPGMAKER_ASSET_DIR
#error "JRPGMAKER_ASSET_DIR must be defined by the build"
#endif

std::filesystem::path AssetPath(const char* relative) {
    return std::filesystem::path(JRPGMAKER_ASSET_DIR) / relative;
}

} // namespace

TEST_CASE("gltf import reads the triangle positions and indices", "[core][assetimport]") {
    const std::filesystem::path path = AssetPath("art/meshes/triangle.gltf");
    REQUIRE(std::filesystem::exists(path));

    GltfLoadError error;
    const std::optional<MeshData> mesh = LoadGltfMesh(path, &error);
    REQUIRE(mesh.has_value());

    REQUIRE(mesh->vertex_count() == 3);
    REQUIRE(mesh->positions.size() == 9);
    REQUIRE(mesh->positions[0] == Catch::Approx(-0.5f));
    REQUIRE(mesh->positions[1] == Catch::Approx(-0.5f));
    REQUIRE(mesh->positions[2] == Catch::Approx(0.0f));
    REQUIRE(mesh->positions[3] == Catch::Approx(0.5f));
    REQUIRE(mesh->positions[4] == Catch::Approx(-0.5f));
    REQUIRE(mesh->positions[5] == Catch::Approx(0.0f));
    REQUIRE(mesh->positions[6] == Catch::Approx(0.0f));
    REQUIRE(mesh->positions[7] == Catch::Approx(0.5f));
    REQUIRE(mesh->positions[8] == Catch::Approx(0.0f));

    REQUIRE(mesh->index_count() == 3);
    REQUIRE(mesh->indices[0] == 0);
    REQUIRE(mesh->indices[1] == 1);
    REQUIRE(mesh->indices[2] == 2);
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