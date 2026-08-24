#define GLM_ENABLE_EXPERIMENTAL
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <cstddef>
#include <cstring>
#include <map>
#include <string>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

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

// Reads the glTF node transform into core::Transform. glTF nodes may carry a
// 4x4 column-major matrix (has_matrix) or decomposed TRS fields; matrix form is
// decomposed via GLM. An absent transform is identity.
bool NodeTransform(const cgltf_node& node, core::Transform& out) {
    if (node.has_matrix) {
        // glTF matrices are column-major; GLM is column-major, so the raw array
        // maps directly onto glm::mat4.
        const glm::mat4 matrix = glm::make_mat4(node.matrix);
        glm::vec3 scale;
        glm::quat rotation;
        glm::vec3 translation;
        glm::vec3 skew;
        glm::vec4 perspective;
        if (!glm::decompose(matrix, scale, rotation, translation, skew, perspective)) {
            return false;
        }
        out = core::Transform{.translation = translation, .rotation = rotation, .scale = scale};
        return true;
    }

    out = core::Transform{};
    if (node.has_translation) {
        out.translation = {node.translation[0], node.translation[1], node.translation[2]};
    }
    if (node.has_rotation) {
        // glTF quaternions are (x, y, z, w).
        out.rotation =
            glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
    }
    if (node.has_scale) {
        out.scale = {node.scale[0], node.scale[1], node.scale[2]};
    }
    return true;
}

// Imports a glTF node and its descendants into the scene. `node_index` is the
// node's index in data->nodes (from the prebuilt index map); node_entities is
// filled so callers can map glTF node index -> scene entity.
void ImportNode(const cgltf_node& gltf_node, core::Scene& scene,
                std::vector<core::MeshData>& meshes, std::vector<core::Entity>& node_entities,
                std::size_t node_index, core::Entity parent,
                const std::map<const cgltf_mesh*, std::size_t>& mesh_pool,
                const std::map<const cgltf_node*, std::size_t>& node_indices, std::string& message,
                bool& ok) {
    const core::Entity entity = scene.CreateEntity();

    core::Transform transform;
    if (!NodeTransform(gltf_node, transform)) {
        message = "failed to decompose node matrix";
        ok = false;
        return;
    }
    scene.Registry().emplace<core::Transform>(entity, transform);

    if (parent != core::kNullEntity) {
        scene.SetParent(entity, parent);
    }

    if (gltf_node.mesh != nullptr) {
        const auto it = mesh_pool.find(gltf_node.mesh);
        if (it != mesh_pool.end()) {
            scene.Registry().emplace<MeshRef>(entity, MeshRef{it->second});
        }
    }

    if (node_entities.size() <= node_index) {
        node_entities.resize(node_index + 1, core::kNullEntity);
    }
    node_entities[node_index] = entity;

    for (cgltf_size i = 0; i < gltf_node.children_count; ++i) {
        const auto child_it = node_indices.find(gltf_node.children[i]);
        const std::size_t child_index = child_it != node_indices.end() ? child_it->second : 0;
        ImportNode(*gltf_node.children[i], scene, meshes, node_entities, child_index, entity,
                   mesh_pool, node_indices, message, ok);
        if (!ok) {
            return;
        }
    }
}

// Builds the mesh pool for a scene: one MeshData per glTF mesh (first
// primitive only, P2 scope). Returns false and sets `message` on the first
// unreadable mesh.
bool BuildMeshPool(const cgltf_data* data, std::vector<core::MeshData>& meshes,
                   std::map<const cgltf_mesh*, std::size_t>& mesh_pool, std::string& message) {
    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        const cgltf_mesh& mesh = data->meshes[m];
        if (mesh.primitives_count == 0) {
            message = "mesh " + std::to_string(m) + " has no primitives";
            return false;
        }
        core::MeshData mesh_data;
        if (!LoadPrimitive(mesh.primitives[0], mesh_data, message)) {
            return false;
        }
        mesh_pool.emplace(&mesh, meshes.size());
        meshes.push_back(std::move(mesh_data));
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

std::optional<SceneLoad> LoadGltfScene(const std::filesystem::path& path, GltfLoadError* error) {
    const auto fail = [&](std::string message) -> std::optional<SceneLoad> {
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

    SceneLoad result;

    // Mesh pool: one MeshData per glTF mesh, referenced from nodes via MeshRef.
    std::map<const cgltf_mesh*, std::size_t> mesh_pool;
    std::string message;
    if (!BuildMeshPool(data, result.meshes, mesh_pool, message)) {
        cgltf_free(data);
        return fail(std::move(message));
    }

    // Map each glTF node to its index in data->nodes (for node_entities).
    std::map<const cgltf_node*, std::size_t> node_indices;
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        node_indices.emplace(&data->nodes[i], static_cast<std::size_t>(i));
    }

    // Import roots of the default scene. If no scene is declared, glTF still
    // lists nodes; import all top-level nodes.
    bool ok = true;
    if (data->scene != nullptr && data->scene->nodes_count > 0) {
        for (cgltf_size i = 0; i < data->scene->nodes_count; ++i) {
            const auto it = node_indices.find(data->scene->nodes[i]);
            const std::size_t node_index = it != node_indices.end() ? it->second : 0;
            ImportNode(*data->scene->nodes[i], result.scene, result.meshes, result.node_entities,
                       node_index, core::kNullEntity, mesh_pool, node_indices, message, ok);
            if (!ok) {
                cgltf_free(data);
                return fail(std::move(message));
            }
        }
    } else {
        for (cgltf_size i = 0; i < data->nodes_count; ++i) {
            if (data->nodes[i].parent == nullptr) {
                ImportNode(data->nodes[i], result.scene, result.meshes, result.node_entities,
                           static_cast<std::size_t>(i), core::kNullEntity, mesh_pool, node_indices,
                           message, ok);
                if (!ok) {
                    cgltf_free(data);
                    return fail(std::move(message));
                }
            }
        }
    }

    cgltf_free(data);
    return result;
}

} // namespace jrpgmaker::assetimport