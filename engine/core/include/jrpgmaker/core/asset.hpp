#pragma once

#include <cstdint>
#include <unordered_map>

#include "jrpgmaker/core/mesh.hpp"

namespace jrpgmaker::core {

// Strong handle into an asset registry (P2: mesh assets). Mirrors the RHI
// handle style (kInvalid = 0). Handles are stable for the registry's lifetime;
// an unregistered handle is never reused (next_ monotonically increases).
enum class AssetHandle : std::uint32_t { kInvalid = 0 };

// Owns loaded assets by handle (v0: MeshData only). Registration copies the
// asset in; Unregister removes it (unload). live_count() is the leak-detection
// probe: a well-behaved caller returns to zero after unloading everything.
//
// "Asynchronous" loading (background thread + completion callback) is a P2
// follow-up subtask; v0 loading is synchronous (assetimport parses on the
// caller's thread, then registers the result here).
class AssetRegistry {
public:
    AssetRegistry() = default;

    AssetRegistry(const AssetRegistry&) = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;
    AssetRegistry(AssetRegistry&&) noexcept = default;
    AssetRegistry& operator=(AssetRegistry&&) noexcept = default;

    // Registers a mesh, returning its new handle.
    AssetHandle RegisterMesh(const MeshData& mesh);

    // Returns the mesh for a handle, or nullptr if not registered.
    const MeshData* FindMesh(AssetHandle handle) const;

    // Unloads a mesh, invalidating its handle. Returns false if the handle was
    // not registered.
    bool Unregister(AssetHandle handle);

    // Number of live assets. Tests assert this reaches zero after unloading.
    std::size_t live_count() const { return meshes_.size(); }

private:
    std::unordered_map<AssetHandle, MeshData> meshes_;
    AssetHandle next_ = AssetHandle::kInvalid;
};

} // namespace jrpgmaker::core