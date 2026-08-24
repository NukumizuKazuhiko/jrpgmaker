#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <cstddef>
#include <cstring>

#include "jrpgmaker/assetimport/asset_import.hpp"

namespace jrpgmaker::assetimport {

namespace {

// Copies a glTF accessor into tightly packed float3 positions. Returns false
// on any unsupported layout. cgltf_accessor_unpack_floats handles sparse
// accessors and stride via the buffer_view.
bool ReadPositions(const cgltf_accessor* accessor, core::MeshData& out) {
    if (accessor->type != cgltf_type_vec3 ||
        accessor->component_type != cgltf_component_type_r_32f) {
        return false;
    }
    out.positions.resize(accessor->count * 3u);
    const cgltf_size written = cgltf_accessor_unpack_floats(
        accessor, out.positions.data(), static_cast<cgltf_size>(out.positions.size()));
    return written == accessor->count * 3u;
}

// Reads a glTF index accessor (SCALAR of u8/u16/u32) into flat uint32 indices.
bool ReadIndices(const cgltf_accessor* accessor, core::MeshData& out) {
    if (accessor->type != cgltf_type_scalar) {
        return false;
    }
    out.indices.resize(accessor->count);
    for (cgltf_size i = 0; i < accessor->count; ++i) {
        out.indices[i] = static_cast<std::uint32_t>(cgltf_accessor_read_index(accessor, i));
    }
    return true;
}

bool LoadPrimitive(const cgltf_primitive& primitive, core::MeshData& out, std::string& message) {
    // P2 scope: static triangle mesh, single primitive, triangles only.
    if (primitive.type != cgltf_primitive_type_triangles) {
        message = "unsupported primitive type (only triangles)";
        return false;
    }
    if (primitive.has_draco_mesh_compression) {
        message = "Draco mesh compression is not supported";
        return false;
    }

    bool found_position = false;
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const cgltf_attribute& attribute = primitive.attributes[i];
        if (attribute.type == cgltf_attribute_type_position) {
            if (!ReadPositions(attribute.data, out)) {
                message = "unsupported POSITION accessor layout";
                return false;
            }
            found_position = true;
            break;
        }
    }
    if (!found_position) {
        message = "primitive has no POSITION attribute";
        return false;
    }

    if (primitive.indices == nullptr) {
        message = "primitive has no index accessor";
        return false;
    }
    if (!ReadIndices(primitive.indices, out)) {
        message = "unsupported index accessor";
        return false;
    }
    return true;
}

} // namespace

std::optional<core::MeshData> LoadGltfMesh(const std::filesystem::path& path,
                                           GltfLoadError* error) {
    const auto fail = [&](std::string message) -> std::optional<core::MeshData> {
        if (error != nullptr) {
            error->message = std::move(message);
        }
        return std::nullopt;
    };

    cgltf_options options = {};
    cgltf_data* data = nullptr;
    const cgltf_result parse_result = cgltf_parse_file(&options, path.string().c_str(), &data);
    if (parse_result != cgltf_result_success) {
        return fail("cgltf_parse_file failed with result " + std::to_string(parse_result));
    }

    if (cgltf_load_buffers(&options, data, path.string().c_str()) != cgltf_result_success) {
        cgltf_free(data);
        return fail("cgltf_load_buffers failed");
    }

    if (data->meshes_count == 0) {
        cgltf_free(data);
        return fail("glTF contains no meshes");
    }
    if (data->meshes[0].primitives_count == 0) {
        cgltf_free(data);
        return fail("first mesh has no primitives");
    }

    core::MeshData mesh;
    std::string message;
    const bool ok = LoadPrimitive(data->meshes[0].primitives[0], mesh, message);
    cgltf_free(data);
    if (!ok) {
        return fail(std::move(message));
    }
    return mesh;
}

} // namespace jrpgmaker::assetimport