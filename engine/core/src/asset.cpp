#include "jrpgmaker/core/asset.hpp"

namespace jrpgmaker::core {

AssetHandle AssetRegistry::RegisterMesh(const MeshData& mesh) {
    const std::uint32_t raw = static_cast<std::uint32_t>(next_);
    next_ = static_cast<AssetHandle>(raw + 1u);
    meshes_.emplace(next_, mesh);
    return next_;
}

const MeshData* AssetRegistry::FindMesh(AssetHandle handle) const {
    const auto it = meshes_.find(handle);
    return it != meshes_.end() ? &it->second : nullptr;
}

bool AssetRegistry::Unregister(AssetHandle handle) {
    return meshes_.erase(handle) > 0;
}

} // namespace jrpgmaker::core