#pragma once

#include <cstdint>
#include <vector>

namespace jrpgmaker::core {

// CPU-side mesh data produced by the asset import pipeline (P2: glTF via
// cgltf). Layout-independent of any RHI backend: `positions` is tightly packed
// float3 (xyz per vertex), `texcoords` is tightly packed float2 (uv per vertex),
// and `indices` is a flat triangle index list. The render layer is responsible
// for uploading this into GPU vertex/index buffers.
//
// P4 skinned meshes: `joints` and `weights` are parallel per-vertex arrays.
// `joints` holds 4 joint indices per vertex (tightly packed uint16, P4 v0:
// glTF JOINTS_0 u16 accessor), `weights` holds 4 normalized floats per vertex
// (glTF WEIGHTS_0). Each vertex contributes to at most kMaxBoneInfluences
// joints; a joint index of 0xFFFF marks "no influence" (glTF padding), and
// the corresponding weight must be zero. Empty `joints`/`weights` denotes a
// static mesh (no skinning), matching P2 behavior.
struct MeshData {
    std::vector<float> positions;
    std::vector<float> texcoords;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint16_t> joints;
    std::vector<float> weights;

    [[nodiscard]] std::size_t vertex_count() const { return positions.size() / 3u; }
    [[nodiscard]] std::size_t index_count() const { return indices.size(); }
    [[nodiscard]] bool skinned() const { return !joints.empty() && !weights.empty(); }
};

// Max bone influences per vertex (glTF JOINT_0/WEIGHT_0 set semantics).
inline constexpr std::uint32_t kMaxBoneInfluences = 4u;

} // namespace jrpgmaker::core
