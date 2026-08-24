#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/core/asset.hpp"

using jrpgmaker::core::AssetHandle;
using jrpgmaker::core::AssetRegistry;
using jrpgmaker::core::MeshData;

namespace {

MeshData MakeTriangle() {
    MeshData mesh;
    mesh.positions = {-0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.0f, 0.5f, 0.0f};
    mesh.indices = {0, 1, 2};
    return mesh;
}

} // namespace

TEST_CASE("asset registry starts empty", "[core][asset]") {
    AssetRegistry registry;
    REQUIRE(registry.live_count() == 0);
}

TEST_CASE("asset registry returns the registered mesh and grows the count", "[core][asset]") {
    AssetRegistry registry;
    const AssetHandle handle = registry.RegisterMesh(MakeTriangle());

    REQUIRE(handle != AssetHandle::kInvalid);
    REQUIRE(registry.live_count() == 1);
    REQUIRE(registry.FindMesh(handle) != nullptr);
    REQUIRE(registry.FindMesh(handle)->vertex_count() == 3);
}

TEST_CASE("asset registry reports null for an unregistered handle", "[core][asset]") {
    AssetRegistry registry;
    REQUIRE(registry.FindMesh(AssetHandle::kInvalid) == nullptr);
    REQUIRE(registry.FindMesh(static_cast<AssetHandle>(42)) == nullptr);
}

TEST_CASE("asset registry unload returns the count to zero", "[core][asset]") {
    AssetRegistry registry;
    const AssetHandle handle = registry.RegisterMesh(MakeTriangle());
    REQUIRE(registry.live_count() == 1);

    REQUIRE(registry.Unregister(handle));
    REQUIRE(registry.live_count() == 0);
    REQUIRE(registry.FindMesh(handle) == nullptr);
}

TEST_CASE("asset registry unregistering an unknown handle fails", "[core][asset]") {
    AssetRegistry registry;
    registry.RegisterMesh(MakeTriangle());

    REQUIRE_FALSE(registry.Unregister(AssetHandle::kInvalid));
    REQUIRE_FALSE(registry.Unregister(static_cast<AssetHandle>(99)));
}

TEST_CASE("asset registry keeps meshes independent after registration", "[core][asset]") {
    AssetRegistry registry;
    const AssetHandle a = registry.RegisterMesh(MakeTriangle());
    const AssetHandle b = registry.RegisterMesh(MakeTriangle());

    REQUIRE(a != b);
    REQUIRE(registry.live_count() == 2);
    REQUIRE(registry.FindMesh(a) != nullptr);
    REQUIRE(registry.FindMesh(b) != nullptr);

    registry.Unregister(a);
    REQUIRE(registry.live_count() == 1);
    REQUIRE(registry.FindMesh(a) == nullptr);
    REQUIRE(registry.FindMesh(b) != nullptr);
}

TEST_CASE("asset registry assigns unique monotonically increasing handles", "[core][asset]") {
    AssetRegistry registry;
    const AssetHandle first = registry.RegisterMesh(MakeTriangle());
    const AssetHandle second = registry.RegisterMesh(MakeTriangle());
    const AssetHandle third = registry.RegisterMesh(MakeTriangle());

    REQUIRE(first != second);
    REQUIRE(second != third);
    // Handles are assigned from 1 upward (kInvalid = 0 is never handed out).
    REQUIRE(static_cast<std::uint32_t>(first) == 1u);
    REQUIRE(static_cast<std::uint32_t>(second) == 2u);
    REQUIRE(static_cast<std::uint32_t>(third) == 3u);
}