#pragma once

#include <cstdint>
#include <vector>

namespace jrpgmaker::core {

// CPU-side mesh data produced by the asset import pipeline (P2: glTF via
// cgltf). Layout-independent of any RHI backend: `positions` is tightly packed
// float3 (xyz per vertex), `indices` is a flat triangle index list. The render
// layer is responsible for uploading this into GPU vertex/index buffers.
struct MeshData {
    std::vector<float> positions;
    std::vector<std::uint32_t> indices;

    [[nodiscard]] std::size_t vertex_count() const { return positions.size() / 3u; }
    [[nodiscard]] std::size_t index_count() const { return indices.size(); }
};

} // namespace jrpgmaker::core