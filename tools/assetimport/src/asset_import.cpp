#define GLM_ENABLE_EXPERIMENTAL
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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

// Reads a glTF JOINTS_0 accessor (VEC4 of u8/u16) into tightly packed uint16
// joint indices (4 per vertex). Padded slots (0xFFFF from glTF, or out-of-range
// indices) are written verbatim; the matching weight must be zero.
bool ReadJoints(const cgltf_accessor* accessor, core::MeshData& out) {
    if (accessor->type != cgltf_type_vec4) {
        return false;
    }
    if (accessor->component_type != cgltf_component_type_r_8u &&
        accessor->component_type != cgltf_component_type_r_16u) {
        return false;
    }
    out.joints.resize(accessor->count * core::kMaxBoneInfluences);
    for (cgltf_size i = 0; i < accessor->count; ++i) {
        // cgltf_accessor_read_uint reads one full element (4 components here);
        // cgltf_accessor_read_index is only valid for single-component types.
        cgltf_uint values[4] = {0u, 0u, 0u, 0u};
        if (!cgltf_accessor_read_uint(accessor, i, values, core::kMaxBoneInfluences)) {
            return false;
        }
        for (std::uint32_t c = 0; c < core::kMaxBoneInfluences; ++c) {
            out.joints[static_cast<std::size_t>(i) * core::kMaxBoneInfluences + c] =
                static_cast<std::uint16_t>(values[c]);
        }
    }
    return true;
}

// Reads a glTF WEIGHTS_0 accessor (VEC4 of float) into tightly packed float
// weights (4 per vertex).
bool ReadWeights(const cgltf_accessor* accessor, core::MeshData& out) {
    if (accessor->type != cgltf_type_vec4 ||
        accessor->component_type != cgltf_component_type_r_32f) {
        return false;
    }
    out.weights.resize(accessor->count * core::kMaxBoneInfluences);
    const cgltf_size written = cgltf_accessor_unpack_floats(
        accessor, out.weights.data(), static_cast<cgltf_size>(out.weights.size()));
    return written == accessor->count * core::kMaxBoneInfluences;
}

bool LoadPrimitive(const cgltf_primitive& primitive, core::MeshData& out, std::string& message) {
    // P4 scope: triangle mesh, single primitive, triangles only. Skinned meshes
    // additionally carry JOINTS_0/WEIGHTS_0; static meshes keep P2 behavior.
    if (primitive.type != cgltf_primitive_type_triangles) {
        message = "unsupported primitive type (only triangles)";
        return false;
    }
    if (primitive.has_draco_mesh_compression) {
        message = "Draco mesh compression is not supported";
        return false;
    }

    bool found_position = false;
    bool found_joints = false;
    bool found_weights = false;
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const cgltf_attribute& attribute = primitive.attributes[i];
        switch (attribute.type) {
        case cgltf_attribute_type_position:
            if (!ReadPositions(attribute.data, out)) {
                message = "unsupported POSITION accessor layout";
                return false;
            }
            found_position = true;
            break;
        case cgltf_attribute_type_joints:
            if (!ReadJoints(attribute.data, out)) {
                message = "unsupported JOINTS_0 accessor layout";
                return false;
            }
            found_joints = true;
            break;
        case cgltf_attribute_type_weights:
            if (!ReadWeights(attribute.data, out)) {
                message = "unsupported WEIGHTS_0 accessor layout";
                return false;
            }
            found_weights = true;
            break;
        default:
            break; // NORMAL/TEXCOORD etc. are ignored in v0.
        }
    }
    if (!found_position) {
        message = "primitive has no POSITION attribute";
        return false;
    }
    if (found_joints != found_weights) {
        message = "primitive has JOINTS_0 without WEIGHTS_0 (or vice versa)";
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
void ImportNode(const cgltf_node& gltf_node, core::Scene& scene, core::AssetRegistry& assets,
                std::vector<core::Entity>& node_entities, std::size_t node_index,
                core::Entity parent,
                const std::map<const cgltf_mesh*, core::AssetHandle>& mesh_pool,
                const std::map<const cgltf_mesh*, std::size_t>& material_pool,
                const std::map<const cgltf_node*, std::size_t>& node_indices,
                const std::map<const cgltf_skin*, std::size_t>& skin_indices, std::string& message,
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
            const auto material_it = material_pool.find(gltf_node.mesh);
            if (material_it != material_pool.end()) {
                scene.Registry().emplace<MaterialRef>(entity, MaterialRef{material_it->second});
            }
        }
    }
    // P4: nodes referencing a skin (glTF node.skin) attach a SkinRef so the
    // render layer can skin the node's mesh with that skeleton's bone matrices.
    if (gltf_node.skin != nullptr && gltf_node.mesh != nullptr) {
        const auto skin_it = skin_indices.find(gltf_node.skin);
        if (skin_it != skin_indices.end()) {
            scene.Registry().emplace<SkinRef>(entity, SkinRef{skin_it->second});
        }
    }

    if (node_entities.size() <= node_index) {
        node_entities.resize(node_index + 1, core::kNullEntity);
    }
    node_entities[node_index] = entity;

    for (cgltf_size i = 0; i < gltf_node.children_count; ++i) {
        const auto child_it = node_indices.find(gltf_node.children[i]);
        const std::size_t child_index = child_it != node_indices.end() ? child_it->second : 0;
        ImportNode(*gltf_node.children[i], scene, assets, node_entities, child_index, entity,
                   mesh_pool, material_pool, node_indices, skin_indices, message, ok);
        if (!ok) {
            return;
        }
    }
}

// Builds the mesh pool for a scene: one MeshData per glTF mesh (first
// primitive only, P4 scope), registered into the asset registry. Returns false
// and sets `message` on the first unreadable mesh.
bool BuildMeshPool(const cgltf_data* data, core::AssetRegistry& assets,
                   std::map<const cgltf_mesh*, core::AssetHandle>& mesh_pool,
                   std::string& message) {
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
        mesh_pool.emplace(&mesh, assets.RegisterMesh(mesh_data));
    }
    return true;
}

bool DecodeImage(const cgltf_image& image, const std::filesystem::path& source_path,
                 TextureAsset& texture, std::string& message) {
    std::vector<std::uint8_t> embedded;
    const std::uint8_t* encoded_data = nullptr;
    std::size_t encoded_size = 0;
    const bool has_data_uri =
        image.uri != nullptr && std::string_view(image.uri).starts_with("data:");
    if (has_data_uri) {
        const std::string_view uri(image.uri);
        const std::size_t comma = uri.find(',');
        if (comma == std::string_view::npos ||
            uri.substr(0, comma).find(";base64") == std::string_view::npos) {
            message = "embedded image must use base64 data URI";
            return false;
        }
        const std::string_view encoded = uri.substr(comma + 1u);
        auto decode = [](char value) -> int {
            if (value >= 'A' && value <= 'Z')
                return value - 'A';
            if (value >= 'a' && value <= 'z')
                return value - 'a' + 26;
            if (value >= '0' && value <= '9')
                return value - '0' + 52;
            if (value == '+')
                return 62;
            if (value == '/')
                return 63;
            return -1;
        };
        int accumulator = 0;
        int bits = -8;
        for (const char value : encoded) {
            if (value == '=')
                break;
            const int digit = decode(value);
            if (digit < 0) {
                message = "embedded image contains invalid base64";
                return false;
            }
            accumulator = (accumulator << 6) | digit;
            bits += 6;
            if (bits >= 0) {
                embedded.push_back(static_cast<std::uint8_t>((accumulator >> bits) & 0xff));
                bits -= 8;
            }
        }
        encoded_data = embedded.data();
        encoded_size = embedded.size();
    } else if (image.buffer_view != nullptr) {
        const cgltf_buffer_view* view = image.buffer_view;
        const auto* data = cgltf_buffer_view_data(view);
        if (data == nullptr || view->size == 0) {
            message = "embedded image bufferView is empty";
            return false;
        }
        encoded_data = data;
        encoded_size = static_cast<std::size_t>(view->size);
    } else if (image.uri != nullptr && image.uri[0] != '\0') {
        const std::filesystem::path image_path = source_path.parent_path() / image.uri;
        int width = 0;
        int height = 0;
        int channels = 0;
        if (stbi_info(image_path.string().c_str(), &width, &height, &channels) == 0) {
            message = "external image is missing or invalid";
            return false;
        }
        encoded_data = nullptr;
        encoded_size = 0;
        int decoded_width = 0;
        int decoded_height = 0;
        int decoded_channels = 0;
        stbi_uc* pixels = stbi_load(image_path.string().c_str(), &decoded_width, &decoded_height,
                                    &decoded_channels, 4);
        if (pixels == nullptr) {
            message = "external image failed to decode";
            return false;
        }
        if (decoded_width > 4096 || decoded_height > 4096 ||
            static_cast<std::uint64_t>(decoded_width) * static_cast<std::uint64_t>(decoded_height) >
                16ull * 1024ull * 1024ull) {
            stbi_image_free(pixels);
            message = "external image is too large";
            return false;
        }
        texture.width = static_cast<std::uint32_t>(decoded_width);
        texture.height = static_cast<std::uint32_t>(decoded_height);
        texture.rgba8.assign(pixels, pixels + static_cast<std::size_t>(decoded_width) *
                                                  static_cast<std::size_t>(decoded_height) * 4u);
        stbi_image_free(pixels);
        return true;
    } else {
        return true;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    if (encoded_size > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        stbi_info_from_memory(encoded_data, static_cast<int>(encoded_size), &width, &height,
                              &channels) == 0 ||
        width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) >
            16ull * 1024ull * 1024ull) {
        message = "embedded image is missing, invalid, or too large";
        return false;
    }
    int decoded_width = 0;
    int decoded_height = 0;
    int decoded_channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(encoded_data, static_cast<int>(encoded_size),
                                            &decoded_width, &decoded_height, &decoded_channels, 4);
    if (pixels == nullptr) {
        message = "embedded image failed to decode";
        return false;
    }
    texture.width = static_cast<std::uint32_t>(decoded_width);
    texture.height = static_cast<std::uint32_t>(decoded_height);
    texture.rgba8.assign(pixels, pixels + static_cast<std::size_t>(decoded_width) *
                                              static_cast<std::size_t>(decoded_height) * 4u);
    stbi_image_free(pixels);
    return true;
}

bool BuildMaterialPool(const cgltf_data* data, std::vector<TextureAsset>& textures,
                       const std::filesystem::path& source_path,
                       std::vector<MaterialAsset>& materials,
                       std::map<const cgltf_mesh*, std::size_t>& material_pool,
                       std::string& message) {
    std::map<const cgltf_image*, std::size_t> texture_indices;
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        const cgltf_image& image = data->images[i];
        texture_indices.emplace(&image, textures.size());
        TextureAsset texture{.name = image.name != nullptr ? image.name : "",
                             .source_uri = image.uri != nullptr ? image.uri : "",
                             .width = 0,
                             .height = 0,
                             .rgba8 = {}};
        if (!DecodeImage(image, source_path, texture, message)) {
            message = "texture " + std::to_string(i) + ": " + message;
            return false;
        }
        textures.push_back(std::move(texture));
    }

    std::map<const cgltf_material*, std::size_t> material_indices;
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material& material = data->materials[i];
        MaterialAsset asset{.name = material.name != nullptr ? material.name : ""};
        if (material.has_pbr_metallic_roughness) {
            const auto& pbr = material.pbr_metallic_roughness;
            asset.base_color_factor = glm::vec4(pbr.base_color_factor[0], pbr.base_color_factor[1],
                                                pbr.base_color_factor[2], pbr.base_color_factor[3]);
            asset.metallic_factor = pbr.metallic_factor;
            asset.roughness_factor = pbr.roughness_factor;
            if (pbr.base_color_texture.texture != nullptr &&
                pbr.base_color_texture.texture->image != nullptr) {
                const auto image_it = texture_indices.find(pbr.base_color_texture.texture->image);
                if (image_it == texture_indices.end()) {
                    message = "material " + std::to_string(i) + " references an unknown image";
                    return false;
                }
                asset.base_color_texture = image_it->second;
            }
        }
        material_indices.emplace(&material, materials.size());
        materials.push_back(std::move(asset));
    }

    for (cgltf_size i = 0; i < data->meshes_count; ++i) {
        const cgltf_mesh& mesh = data->meshes[i];
        if (mesh.primitives_count == 0 || mesh.primitives[0].material == nullptr) {
            continue;
        }
        const auto material_it = material_indices.find(mesh.primitives[0].material);
        if (material_it == material_indices.end()) {
            message = "mesh " + std::to_string(i) + " references an unknown material";
            return false;
        }
        material_pool.emplace(&mesh, material_it->second);
    }
    return true;
}

// Builds skeleton assets from the glTF skins. Joint order follows the skin's
// `joints` array; parents are the glTF node parent chain re-expressed as
// indices into that array (roots get kNullJoint). Joints whose parent is not a
// member of the skin's joint array are treated as roots (skinned mesh v0).
bool BuildSkeletons(const cgltf_data* data, std::vector<SkeletonAsset>& skeletons,
                    std::string& message) {
    for (cgltf_size s = 0; s < data->skins_count; ++s) {
        const cgltf_skin& skin = data->skins[s];
        if (skin.joints_count == 0) {
            message = "skin " + std::to_string(s) + " has no joints";
            return false;
        }
        if (skin.joints_count > core::kMaxBones) {
            message = "skin " + std::to_string(s) + " exceeds kMaxBones (" +
                      std::to_string(core::kMaxBones) + ")";
            return false;
        }

        // Map each joint node -> index within the skin's joint array.
        std::map<const cgltf_node*, std::size_t> joint_index;
        for (cgltf_size j = 0; j < skin.joints_count; ++j) {
            joint_index.emplace(skin.joints[j], static_cast<std::size_t>(j));
        }

        // Inverse-bind matrices: one mat4 per joint (column-major), identity if
        // the skin omits inverseBindMatrices (allowed by the spec).
        std::vector<glm::mat4> inverse_bind(skin.joints_count, glm::mat4(1.0f));
        if (skin.inverse_bind_matrices != nullptr) {
            if (skin.inverse_bind_matrices->type != cgltf_type_mat4 ||
                skin.inverse_bind_matrices->component_type != cgltf_component_type_r_32f ||
                skin.inverse_bind_matrices->count != skin.joints_count) {
                message = "skin " + std::to_string(s) +
                          " has an unsupported inverseBindMatrices accessor";
                return false;
            }
            std::vector<float> raw(skin.joints_count * 16u);
            const cgltf_size written = cgltf_accessor_unpack_floats(
                skin.inverse_bind_matrices, raw.data(), static_cast<cgltf_size>(raw.size()));
            if (written != skin.joints_count * 16u) {
                message = "skin " + std::to_string(s) + " inverseBindMatrices unpack failed";
                return false;
            }
            for (cgltf_size j = 0; j < skin.joints_count; ++j) {
                // glTF matrices are column-major; GLM is column-major, so the
                // raw 16-float segment maps directly onto glm::mat4.
                inverse_bind[j] = glm::make_mat4(raw.data() + static_cast<std::size_t>(j) * 16u);
            }
        }

        std::vector<core::Joint> joints;
        joints.reserve(skin.joints_count);
        for (cgltf_size j = 0; j < skin.joints_count; ++j) {
            const cgltf_node* joint_node = skin.joints[j];
            std::int32_t parent = core::kNullJoint;
            if (joint_node->parent != nullptr) {
                const auto parent_it = joint_index.find(joint_node->parent);
                if (parent_it != joint_index.end()) {
                    parent = static_cast<std::int32_t>(parent_it->second);
                }
            }
            // Static local TRS of the joint node: the base pose animation
            // channels override (glTF nodes without a transform are identity).
            core::Transform bind_transform;
            std::string transform_error;
            if (!NodeTransform(*joint_node, bind_transform)) {
                message = "skin " + std::to_string(s) + " joint " + std::to_string(j) +
                          " has an undecomposable matrix";
                return false;
            }
            joints.push_back(core::Joint{
                .name = joint_node->name != nullptr ? joint_node->name : "",
                .parent = parent,
                .inverse_bind_matrix = inverse_bind[j],
                .bind_translation = bind_transform.translation,
                .bind_rotation = bind_transform.rotation,
                .bind_scale = bind_transform.scale,
            });
        }
        // Map each joint node back to its glTF node index for animation binding.
        std::vector<std::size_t> joint_nodes;
        joint_nodes.reserve(skin.joints_count);
        for (cgltf_size j = 0; j < skin.joints_count; ++j) {
            const cgltf_node* joint_node = skin.joints[j];
            // data->nodes is a contiguous array of cgltf_node; pointer difference
            // yields the node index (cgltf keeps all nodes in one array).
            const cgltf_size node_index =
                joint_node >= data->nodes && joint_node < data->nodes + data->nodes_count
                    ? static_cast<cgltf_size>(joint_node - data->nodes)
                    : 0;
            joint_nodes.push_back(static_cast<std::size_t>(node_index));
        }

        skeletons.push_back(SkeletonAsset{
            .skeleton = core::Skeleton(std::move(joints)),
            .joint_nodes = std::move(joint_nodes),
        });
    }
    return true;
}

// Builds animation clips from the glTF animations. Every channel maps to a
// KeyframeChannel whose joint_index is resolved via the skin's joint ordering
// (v0: animation targeting a node not present in the first skin is skipped).
bool BuildAnimations(const cgltf_data* data, const std::vector<SkeletonAsset>& skeletons,
                     std::vector<AnimationAsset>& animations, std::string& message) {
    if (data->animations_count == 0) {
        return true;
    }
    if (skeletons.empty()) {
        message = "glTF has animations but no skin to bind them to";
        return false;
    }

    // Map glTF node -> index within the (first) skin's joint array.
    const SkeletonAsset& skin = skeletons.front();
    std::map<const cgltf_node*, std::size_t> node_joint;
    for (cgltf_size j = 0; j < skin.skeleton.joint_count(); ++j) {
        const std::size_t node_index = skin.joint_nodes[j];
        if (node_index < data->nodes_count) {
            node_joint.emplace(&data->nodes[node_index], static_cast<std::size_t>(j));
        }
    }

    for (cgltf_size a = 0; a < data->animations_count; ++a) {
        const cgltf_animation& animation = data->animations[a];
        core::AnimationClip clip;
        clip.name = animation.name != nullptr ? animation.name : "";
        float max_time = 0.0f;

        for (cgltf_size c = 0; c < animation.channels_count; ++c) {
            const cgltf_animation_channel& channel = animation.channels[c];
            const cgltf_animation_sampler& sampler = *channel.sampler;

            const auto joint_it = node_joint.find(channel.target_node);
            if (joint_it == node_joint.end()) {
                continue;
            }

            if (sampler.input == nullptr || sampler.input->type != cgltf_type_scalar ||
                sampler.input->component_type != cgltf_component_type_r_32f) {
                message = "animation " + std::to_string(a) + " sampler has invalid input accessor";
                return false;
            }
            if (sampler.output == nullptr) {
                message = "animation " + std::to_string(a) + " sampler has no output accessor";
                return false;
            }

            core::KeyframeChannel keyframe;
            keyframe.joint_index = static_cast<std::int32_t>(joint_it->second);
            switch (channel.target_path) {
            case cgltf_animation_path_type_translation:
                keyframe.path = core::AnimPath::kTranslation;
                break;
            case cgltf_animation_path_type_rotation:
                keyframe.path = core::AnimPath::kRotation;
                break;
            case cgltf_animation_path_type_scale:
                keyframe.path = core::AnimPath::kScale;
                break;
            default:
                message = "animation " + std::to_string(a) +
                          " channel targets unsupported path (only TRS)";
                return false;
            }
            switch (sampler.interpolation) {
            case cgltf_interpolation_type_linear:
                keyframe.interpolation = core::AnimInterpolation::kLinear;
                break;
            case cgltf_interpolation_type_step:
                keyframe.interpolation = core::AnimInterpolation::kStep;
                break;
            case cgltf_interpolation_type_cubic_spline:
                keyframe.interpolation = core::AnimInterpolation::kCubicSpline;
                break;
            default:
                message = "animation " + std::to_string(a) + " sampler has unknown interpolation";
                return false;
            }

            // Inputs: one scalar per key.
            keyframe.times.resize(sampler.input->count);
            for (cgltf_size t = 0; t < sampler.input->count; ++t) {
                cgltf_float value = 0.0f;
                if (!cgltf_accessor_read_float(sampler.input, t, &value, 1)) {
                    message = "animation " + std::to_string(a) + " sampler input read failed";
                    return false;
                }
                keyframe.times[t] = value;
            }
            if (keyframe.times.size() < 2u) {
                message = "animation " + std::to_string(a) + " sampler has fewer than 2 keys";
                return false;
            }
            max_time = std::max(max_time, keyframe.times.back());

            // Outputs: floats per key (3 for TRS, 4 for rotation, and for
            // CUBICSPLINE 9/12 = 3x per key).
            const bool is_rotation = keyframe.path == core::AnimPath::kRotation;
            const cgltf_size per_key_values = is_rotation ? 4u : 3u;
            const cgltf_size output_elements_per_key =
                sampler.interpolation == cgltf_interpolation_type_cubic_spline ? 3u : 1u;
            const bool output_count_matches =
                sampler.output->count % output_elements_per_key == 0u &&
                sampler.output->count / output_elements_per_key == sampler.input->count;
            const bool output_float_count_fits =
                sampler.output->count <= std::numeric_limits<cgltf_size>::max() / per_key_values;
            if (sampler.output->type != (is_rotation ? cgltf_type_vec4 : cgltf_type_vec3) ||
                sampler.output->component_type != cgltf_component_type_r_32f ||
                !output_count_matches || !output_float_count_fits) {
                message = "animation " + std::to_string(a) +
                          " sampler has invalid output accessor layout";
                return false;
            }
            const cgltf_size expected = sampler.output->count * per_key_values;
            keyframe.values.resize(expected);
            const cgltf_size written =
                cgltf_accessor_unpack_floats(sampler.output, keyframe.values.data(),
                                             static_cast<cgltf_size>(keyframe.values.size()));
            if (written != keyframe.values.size()) {
                message = "animation " + std::to_string(a) + " sampler output unpack failed";
                return false;
            }

            clip.channels.push_back(std::move(keyframe));
        }

        clip.duration_seconds = max_time;
        animations.push_back(AnimationAsset{.clip = std::move(clip)});
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

    // Mesh pool: one MeshData per glTF mesh, registered into the asset
    // registry and referenced from nodes via MeshRef.
    std::map<const cgltf_mesh*, core::AssetHandle> mesh_pool;
    std::string message;
    if (!BuildMeshPool(data, result.assets, mesh_pool, message)) {
        cgltf_free(data);
        return fail(std::move(message));
    }
    std::map<const cgltf_mesh*, std::size_t> material_pool;
    if (!BuildMaterialPool(data, result.textures, path, result.materials, material_pool, message)) {
        cgltf_free(data);
        return fail(std::move(message));
    }

    // Skeletons and animations (P4): imported before node traversal so nodes can
    // attach a SkinRef. Animations bind to the first skin's joint ordering.
    if (!BuildSkeletons(data, result.skeletons, message)) {
        cgltf_free(data);
        return fail(std::move(message));
    }
    if (!BuildAnimations(data, result.skeletons, result.animations, message)) {
        cgltf_free(data);
        return fail(std::move(message));
    }

    // Map each glTF node to its index in data->nodes (for node_entities).
    std::map<const cgltf_node*, std::size_t> node_indices;
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        node_indices.emplace(&data->nodes[i], static_cast<std::size_t>(i));
    }

    // Skin pointer -> skeleton index, for attaching SkinRef to mesh nodes.
    std::map<const cgltf_skin*, std::size_t> skin_indices;
    for (cgltf_size s = 0; s < data->skins_count; ++s) {
        skin_indices.emplace(&data->skins[s], static_cast<std::size_t>(s));
    }

    // Import roots of the default scene. If no scene is declared, glTF still
    // lists nodes; import all top-level nodes.
    bool ok = true;
    if (data->scene != nullptr && data->scene->nodes_count > 0) {
        for (cgltf_size i = 0; i < data->scene->nodes_count; ++i) {
            const auto it = node_indices.find(data->scene->nodes[i]);
            const std::size_t node_index = it != node_indices.end() ? it->second : 0;
            ImportNode(*data->scene->nodes[i], result.scene, result.assets, result.node_entities,
                       node_index, core::kNullEntity, mesh_pool, material_pool, node_indices,
                       skin_indices, message, ok);
            if (!ok) {
                cgltf_free(data);
                return fail(std::move(message));
            }
        }
    } else {
        for (cgltf_size i = 0; i < data->nodes_count; ++i) {
            if (data->nodes[i].parent == nullptr) {
                ImportNode(data->nodes[i], result.scene, result.assets, result.node_entities,
                           static_cast<std::size_t>(i), core::kNullEntity, mesh_pool, material_pool,
                           node_indices, skin_indices, message, ok);
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
