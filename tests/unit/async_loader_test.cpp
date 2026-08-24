#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/assetimport/asset_import.hpp"
#include "jrpgmaker/assetimport/async_loader.hpp"
#include "jrpgmaker/core/asset.hpp"
#include "jrpgmaker/core/mesh.hpp"

namespace {

using jrpgmaker::assetimport::AsyncLoader;
using jrpgmaker::core::AssetHandle;
using jrpgmaker::core::AssetRegistry;
using jrpgmaker::core::MeshData;

#ifndef JRPGMAKER_ASSET_DIR
#error "JRPGMAKER_ASSET_DIR must be defined by the build"
#endif

std::filesystem::path AssetPath(const char* relative) {
    return std::filesystem::path(JRPGMAKER_ASSET_DIR) / relative;
}

// Pumps Poll until the loader has no pending work (bounded by wall-clock so a
// stuck loader fails the test instead of hanging; WSL 9p-mounted assets parse
// slower than a bare yield loop would keep up with).
void Drain(AsyncLoader& loader) {
    for (int i = 0; i < 5000 && loader.pending_count() > 0; ++i) {
        loader.Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(loader.pending_count() == 0);
    loader.Poll();
}

} // namespace

TEST_CASE("async loader parses a glTF mesh in the background", "[core][assetimport][async]") {
    const std::filesystem::path path = AssetPath("art/meshes/triangle.gltf");
    REQUIRE(std::filesystem::exists(path));

    AsyncLoader loader;
    std::optional<MeshData> received;
    bool callback_called = false;
    loader.Submit(path, [&](std::filesystem::path, std::optional<MeshData> mesh) {
        callback_called = true;
        received = std::move(mesh);
    });

    REQUIRE(loader.pending_count() >= 1);
    Drain(loader);

    REQUIRE(callback_called);
    REQUIRE(received.has_value());
    REQUIRE(received->vertex_count() == 3);
    REQUIRE(received->positions[0] == Catch::Approx(-0.5f));
}

TEST_CASE("async loader reports a missing file via a nullopt callback",
          "[core][assetimport][async]") {
    AsyncLoader loader;
    std::optional<MeshData> received;
    bool callback_called = false;
    loader.Submit(AssetPath("art/meshes/does_not_exist.gltf"),
                  [&](std::filesystem::path, std::optional<MeshData> mesh) {
                      callback_called = true;
                      received = std::move(mesh);
                  });

    Drain(loader);
    REQUIRE(callback_called);
    REQUIRE_FALSE(received.has_value());
}

TEST_CASE("async loader dispatches callbacks in submission order", "[core][assetimport][async]") {
    const std::filesystem::path triangle = AssetPath("art/meshes/triangle.gltf");
    const std::filesystem::path scene = AssetPath("art/meshes/scene_hierarchy.gltf");

    AsyncLoader loader;
    std::vector<std::string> order;
    loader.Submit(triangle, [&](std::filesystem::path, std::optional<MeshData>) {
        order.push_back("first");
    });
    loader.Submit(
        scene, [&](std::filesystem::path, std::optional<MeshData>) { order.push_back("second"); });

    Drain(loader);
    REQUIRE(order.size() == 2);
    REQUIRE(order[0] == "first");
    REQUIRE(order[1] == "second");
}

TEST_CASE("async loader results register into the asset registry", "[core][assetimport][async]") {
    const std::filesystem::path path = AssetPath("art/meshes/triangle.gltf");

    AsyncLoader loader;
    AssetRegistry registry;
    AssetHandle registered = AssetHandle::kInvalid;
    loader.Submit(path, [&](std::filesystem::path, std::optional<MeshData> mesh) {
        if (mesh.has_value()) {
            registered = registry.RegisterMesh(*mesh);
        }
    });

    Drain(loader);
    REQUIRE(registered != AssetHandle::kInvalid);
    REQUIRE(registry.live_count() == 1);
    REQUIRE(registry.FindMesh(registered) != nullptr);
    REQUIRE(registry.FindMesh(registered)->vertex_count() == 3);
}