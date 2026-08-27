// goldenimage CLI: render a scene off-screen and either write a golden reference
// (generate) or compare the render against a reference (compare).
//
// Usage:
//   goldenimage generate <out.ppm> [gltf_path]
//   goldenimage compare <ref.ppm> [tolerance] [gltf_path]
//   goldenimage generate-scene <out.ppm> <gltf_path>
//   goldenimage compare-scene <ref.ppm> [tolerance] <gltf_path>
//
// `generate`/`compare` render a single mesh (the P1 triangle); the
// `-scene` variants import a glTF scene and render every mesh-bearing entity
// through its world transform. The reference images under tests/golden/ are
// lavapipe-generated, read-only build artifacts: regenerate them with the
// generate commands and commit the diff.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

#include <jrpgmaker/assetimport/asset_import.hpp>
#include <jrpgmaker/core/camera.hpp>
#include <jrpgmaker/core/scene.hpp>
#include <jrpgmaker/plugin/plugin.hpp>
#include <jrpgmaker/plugins/register.hpp>
#include <jrpgmaker/plugins/sample_style/style.hpp>
#include <jrpgmaker/plugins/sample_unlit/unlit.hpp>
#include <jrpgmaker/render/style.hpp>
#include <jrpgmaker/rhi/command_list.hpp>
#include <jrpgmaker/rhi/device.hpp>
#include <jrpgmaker/rhi/device_factory.hpp>
#include <jrpgmaker/rhi/handles.hpp>
#include <shaders_generated.hpp>

#include "golden_image.hpp"

namespace {

using namespace jrpgmaker::rhi;
namespace golden = jrpgmaker::golden;

#if defined(_WIN32)
constexpr Backend kBackend = Backend::kD3D12;
#else
constexpr Backend kBackend = Backend::kVulkan;
#endif

constexpr std::uint32_t kWidth = 64;
constexpr std::uint32_t kHeight = 64;
constexpr ClearColor kClearColor{0.0f, 0.0f, 0.0f, 1.0f};

// Vertex input: single interleaved buffer with one float3 position attribute.
constexpr std::uint32_t kTriangleStride = 3u * sizeof(float);
constexpr VertexAttribute kTriangleAttributes[] = {
    VertexAttribute{
        .location = 0,
        .format = VertexAttributeFormat::kFloat3,
        .offset_bytes = 0,
    },
};

#ifndef JRPGMAKER_ASSET_DIR
#error "JRPGMAKER_ASSET_DIR must be defined by the build"
#endif

#ifndef JRPGMAKER_SOURCE_DIR
#error "JRPGMAKER_SOURCE_DIR must be defined by the build"
#endif

// Reads the committed shader bytecode for the current platform.
GraphicsPipelineDesc MakeTrianglePipelineDesc() {
    GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.color_format = Format::kR8G8B8A8Unorm;
    pipeline_desc.vertex_input = VertexInputLayout{
        .attributes = kTriangleAttributes,
        .attribute_count = 1,
        .stride_bytes = kTriangleStride,
    };
#if defined(_WIN32)
    pipeline_desc.vertex_shader = ShaderBytecode{jrpgmaker::shaders::kTriangleVsDxil,
                                                 jrpgmaker::shaders::kTriangleVsDxil_size};
    pipeline_desc.pixel_shader = ShaderBytecode{jrpgmaker::shaders::kTrianglePsDxil,
                                                jrpgmaker::shaders::kTrianglePsDxil_size};
#else
    pipeline_desc.vertex_shader =
        ShaderBytecode{jrpgmaker::shaders::kTriangleVsSpv, jrpgmaker::shaders::kTriangleVsSpv_size};
    pipeline_desc.pixel_shader =
        ShaderBytecode{jrpgmaker::shaders::kTrianglePsSpv, jrpgmaker::shaders::kTrianglePsSpv_size};
#endif
    return pipeline_desc;
}

// Applies a world matrix to tightly packed float3 positions.
std::vector<float> BakeWorldPositions(const std::vector<float>& positions, const glm::mat4& world) {
    std::vector<float> baked(positions.size());
    for (std::size_t i = 0; i < positions.size(); i += 3u) {
        const glm::vec4 local(positions[i], positions[i + 1u], positions[i + 2u], 1.0f);
        const glm::vec4 transformed = world * local;
        baked[i] = transformed.x;
        baked[i + 1u] = transformed.y;
        baked[i + 2u] = transformed.z;
    }
    return baked;
}

// Shared device + pipeline setup for a single render pass into the off-screen
// target. Returns false on failure. `push_constants` (optional) is uploaded to
// the pipeline's constant block before each draw (v0: view-proj for cameras).
bool RenderInto(
    std::vector<std::uint8_t>& rgba,
    const std::vector<std::pair<std::vector<float>, std::vector<std::uint32_t>>>& meshes_to_draw,
    const GraphicsPipelineDesc& pipeline_desc, const std::vector<float>* push_constants = nullptr,
    ClearColor clear_color = kClearColor,
    const jrpgmaker::render::RenderPlan* render_plan = nullptr) {
    const std::unique_ptr<IDevice> device = CreateDevice(kBackend);
    if (device == nullptr) {
        std::cerr << "failed to create device\n";
        return false;
    }

    const TextureHandle target = device->CreateTexture(
        TextureDesc{.width = kWidth,
                    .height = kHeight,
                    .format = Format::kR8G8B8A8Unorm,
                    .usage = TextureUsage::kRenderTarget | TextureUsage::kReadBack});
    if (target == TextureHandle::kInvalid) {
        std::cerr << "failed to create render target\n";
        return false;
    }

    const PipelineHandle pipeline = device->CreatePipeline(pipeline_desc);
    if (pipeline == PipelineHandle::kInvalid) {
        std::cerr << "failed to create pipeline\n";
        device->DestroyTexture(target);
        return false;
    }

    struct UploadedMesh {
        BufferHandle vertex_buffer;
        BufferHandle index_buffer;
        std::uint32_t index_count = 0;
    };
    std::vector<UploadedMesh> uploaded;
    uploaded.reserve(meshes_to_draw.size());

    bool ok = true;
    for (const auto& [positions, indices] : meshes_to_draw) {
        const BufferHandle vertex_buffer = device->CreateBuffer(
            BufferDesc{.size_bytes = static_cast<std::uint64_t>(positions.size()) * sizeof(float),
                       .usage = BufferUsage::kVertex});
        if (vertex_buffer == BufferHandle::kInvalid) {
            std::cerr << "failed to create vertex buffer\n";
            ok = false;
            break;
        }
        device->MapWrite(vertex_buffer, positions.data(),
                         static_cast<std::uint64_t>(positions.size()) * sizeof(float));

        const BufferHandle index_buffer = device->CreateBuffer(BufferDesc{
            .size_bytes = static_cast<std::uint64_t>(indices.size()) * sizeof(std::uint32_t),
            .usage = BufferUsage::kIndex});
        if (index_buffer == BufferHandle::kInvalid) {
            std::cerr << "failed to create index buffer\n";
            device->DestroyBuffer(vertex_buffer);
            ok = false;
            break;
        }
        device->MapWrite(index_buffer, indices.data(),
                         static_cast<std::uint64_t>(indices.size()) * sizeof(std::uint32_t));

        uploaded.push_back(
            UploadedMesh{vertex_buffer, index_buffer, static_cast<std::uint32_t>(indices.size())});
    }

    if (ok) {
        ICommandList* command_list = device->CreateCommandList();
        if (command_list == nullptr) {
            std::cerr << "failed to create command list\n";
            ok = false;
        } else {
            command_list->Begin();
            if (render_plan != nullptr) {
                const jrpgmaker::render::RenderPlanResolver resolver{
                    .resolve_pipeline = [pipeline](const auto&) { return std::optional{pipeline}; },
                    .resolve_mesh =
                        [&uploaded](const auto&) {
                            if (uploaded.empty()) {
                                return std::optional<jrpgmaker::render::RenderMeshBinding>{};
                            }
                            const auto& mesh = uploaded.front();
                            return std::optional{jrpgmaker::render::RenderMeshBinding{
                                .vertex_buffer = mesh.vertex_buffer,
                                .index_buffer = mesh.index_buffer,
                                .stride_bytes = kTriangleStride,
                                .index_count = mesh.index_count,
                                .indices_are_32_bit = true}};
                        },
                    .validate_material = {},
                    .bind_draw_resources =
                        [push_constants](ICommandList& list, const auto&, const auto&) {
                            if (push_constants != nullptr && !push_constants->empty()) {
                                list.SetPushConstants(push_constants->data(),
                                                      static_cast<std::uint32_t>(
                                                          push_constants->size() * sizeof(float)));
                            }
                            return jrpgmaker::render::RenderPlanValidation{};
                        }};
                const auto recorded = jrpgmaker::render::RenderPlanExecutor::Record(
                    *render_plan, target, *command_list, resolver,
                    jrpgmaker::render::RenderResourceBudget{});
                if (!recorded.ok) {
                    std::cerr << "failed to record render plan: " << recorded.error << '\n';
                    ok = false;
                }
            } else {
                command_list->BeginRendering(target, clear_color);
                command_list->SetPipeline(pipeline);
                if (push_constants != nullptr && !push_constants->empty()) {
                    command_list->SetPushConstants(
                        push_constants->data(),
                        static_cast<std::uint32_t>(push_constants->size() * sizeof(float)));
                }
                for (const UploadedMesh& mesh : uploaded) {
                    command_list->SetVertexBuffer(mesh.vertex_buffer, kTriangleStride);
                    command_list->SetIndexBuffer(mesh.index_buffer, true);
                    command_list->DrawIndexed(mesh.index_count, 1);
                }
                command_list->EndRendering();
            }
            command_list->End();
            device->Submit(*command_list);
            device->WaitForGpuIdle();
            device->DestroyCommandList(command_list);
        }
    }

    for (const UploadedMesh& mesh : uploaded) {
        device->DestroyBuffer(mesh.index_buffer);
        device->DestroyBuffer(mesh.vertex_buffer);
    }
    device->DestroyPipeline(pipeline);

    if (!ok) {
        device->DestroyTexture(target);
        return false;
    }

    const MappedTexture mapped = device->MapReadBack(target);
    if (mapped.data == nullptr) {
        std::cerr << "readback returned null\n";
        device->DestroyTexture(target);
        return false;
    }

    rgba.resize(static_cast<std::size_t>(kWidth) * kHeight * 4u);
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        const auto* row = reinterpret_cast<const std::uint8_t*>(mapped.data) +
                          static_cast<std::uint64_t>(y) * mapped.row_pitch_bytes;
        std::uint8_t* out_row = rgba.data() + static_cast<std::size_t>(y) * kWidth * 4u;
        std::memcpy(out_row, row, static_cast<std::size_t>(kWidth) * 4u);
    }

    device->DestroyTexture(target);
    return true;
}

// Renders the triangle from the committed glTF asset into tightly-packed RGBA8
// (row pitch == width*4). The mesh positions match the pre-P2
// SV_VertexID-generated geometry exactly, so the golden reference stays valid.
bool RenderTriangle(std::vector<std::uint8_t>& rgba, const std::filesystem::path& gltf_path) {
    const std::optional<jrpgmaker::core::MeshData> mesh =
        jrpgmaker::assetimport::LoadGltfMesh(gltf_path);
    if (!mesh.has_value()) {
        std::cerr << "failed to load glTF mesh: " << gltf_path.string() << '\n';
        return false;
    }
    return RenderInto(rgba, {{mesh->positions, mesh->indices}}, MakeTrianglePipelineDesc());
}

bool RenderProjectStyledTriangle(std::vector<std::uint8_t>& rgba,
                                 const std::filesystem::path& gltf_path,
                                 const std::filesystem::path& project_path) {
    const std::optional<jrpgmaker::core::MeshData> mesh =
        jrpgmaker::assetimport::LoadGltfMesh(gltf_path);
    if (!mesh.has_value()) {
        return false;
    }
    jrpgmaker::render::SceneSnapshot snapshot;
    snapshot.renderables.push_back({.mesh = "triangle",
                                    .material = "triangle",
                                    .world = glm::mat4(1.0f),
                                    .material_parameters = {}});
    std::ifstream project_file(project_path);
    if (!project_file) {
        std::cerr << "failed to open project manifest: " << project_path.string() << '\n';
        return false;
    }
    const auto project_result =
        jrpgmaker::plugin::ParseProjectManifest(nlohmann::json::parse(project_file));
    if (!project_result) {
        std::cerr << "invalid project manifest: " << project_result.error->message << '\n';
        return false;
    }
    const std::filesystem::path project_root =
        project_path.parent_path().parent_path().parent_path();
    const auto read_manifest = [&project_root](const char* id) {
        std::ifstream file(project_root / "plugins" / id / "plugin.json");
        if (!file) {
            throw std::runtime_error("failed to open sample plugin manifest");
        }
        const auto result = jrpgmaker::plugin::ParseManifest(nlohmann::json::parse(file));
        if (!result) {
            throw std::runtime_error(std::string("invalid sample plugin manifest: ") +
                                     result.error->message);
        }
        return *result.manifest;
    };
    jrpgmaker::plugin::PluginRegistry registry;
    const auto registration_error = jrpgmaker::plugins::RegisterSamplePlugins(
        registry, read_manifest("sample_unlit"), read_manifest("sample_style"));
    if (registration_error.has_value()) {
        std::cerr << "failed to register sample plugins: " << registration_error->message << '\n';
        return false;
    }
    const auto style_result =
        jrpgmaker::plugin::CreateProjectRenderStyle(*project_result.manifest, registry);
    if (!style_result) {
        std::cerr << "failed to create project render style: " << style_result.error->message
                  << '\n';
        return false;
    }
    auto* style =
        dynamic_cast<jrpgmaker::render::IRenderStyleAdapter*>(style_result.instance.get());
    if (style == nullptr) {
        return false;
    }
    const auto plan = style->BuildPlan(snapshot);
    const auto validation = jrpgmaker::render::ValidateRenderPlan(plan, style->Descriptor().budget);
    if (!validation.ok || plan.passes.empty()) {
        std::cerr << "invalid style render plan: " << validation.error << '\n';
        return false;
    }
    const auto& clear = plan.passes.front().clear_color;
    auto pipeline_desc = MakeTrianglePipelineDesc();
#if defined(_WIN32)
    if (plan.passes.front().pipeline == "accent") {
        pipeline_desc.pixel_shader = ShaderBytecode{jrpgmaker::shaders::kCameraPsDxil,
                                                    jrpgmaker::shaders::kCameraPsDxil_size};
    }
#else
    if (plan.passes.front().pipeline == "accent") {
        pipeline_desc.pixel_shader =
            ShaderBytecode{jrpgmaker::shaders::kCameraPsSpv, jrpgmaker::shaders::kCameraPsSpv_size};
    }
#endif
    return RenderInto(rgba, {{mesh->positions, mesh->indices}}, pipeline_desc, nullptr,
                      ClearColor{clear.r, clear.g, clear.b, clear.a}, &plan);
}

// Camera scene pipeline: same vertex input, view-proj push constants, and a
// distinct fragment color so the camera golden is distinguishable.
GraphicsPipelineDesc MakeCameraPipelineDesc() {
    GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.color_format = Format::kR8G8B8A8Unorm;
    pipeline_desc.vertex_input = VertexInputLayout{
        .attributes = kTriangleAttributes,
        .attribute_count = 1,
        .stride_bytes = kTriangleStride,
    };
    pipeline_desc.push_constants_size = 64;
#if defined(_WIN32)
    pipeline_desc.vertex_shader =
        ShaderBytecode{jrpgmaker::shaders::kCameraVsDxil, jrpgmaker::shaders::kCameraVsDxil_size};
    pipeline_desc.pixel_shader =
        ShaderBytecode{jrpgmaker::shaders::kCameraPsDxil, jrpgmaker::shaders::kCameraPsDxil_size};
#else
    pipeline_desc.vertex_shader =
        ShaderBytecode{jrpgmaker::shaders::kCameraVsSpv, jrpgmaker::shaders::kCameraVsSpv_size};
    pipeline_desc.pixel_shader =
        ShaderBytecode{jrpgmaker::shaders::kCameraPsSpv, jrpgmaker::shaders::kCameraPsSpv_size};
#endif
    return pipeline_desc;
}

// Imports a glTF scene and renders every mesh-bearing entity through its world
// transform (P2: world-space baked on the CPU, no uniforms in v0).
bool RenderScene(std::vector<std::uint8_t>& rgba, const std::filesystem::path& gltf_path) {
    const std::optional<jrpgmaker::assetimport::SceneLoad> load =
        jrpgmaker::assetimport::LoadGltfScene(gltf_path);
    if (!load.has_value()) {
        std::cerr << "failed to load glTF scene: " << gltf_path.string() << '\n';
        return false;
    }

    std::vector<std::pair<std::vector<float>, std::vector<std::uint32_t>>> meshes_to_draw;
    const auto view = load->scene.Registry().view<jrpgmaker::assetimport::MeshRef>();
    for (const jrpgmaker::core::Entity entity : view) {
        const jrpgmaker::assetimport::MeshRef& ref =
            view.get<jrpgmaker::assetimport::MeshRef>(entity);
        const jrpgmaker::core::MeshData* mesh = load->assets.FindMesh(ref.handle);
        if (mesh == nullptr) {
            continue;
        }
        const glm::mat4 world = load->scene.WorldMatrix(entity);
        meshes_to_draw.emplace_back(BakeWorldPositions(mesh->positions, world), mesh->indices);
    }
    return RenderInto(rgba, meshes_to_draw, MakeTrianglePipelineDesc());
}

// Renders the glTF scene through a camera: world transforms are baked on the
// CPU as in RenderScene, then the view-projection matrix is uploaded via push
// constants so the camera shader maps world positions to clip space.
bool RenderSceneWithCamera(std::vector<std::uint8_t>& rgba, const std::filesystem::path& gltf_path,
                           const jrpgmaker::core::Camera& camera) {
    const std::optional<jrpgmaker::assetimport::SceneLoad> load =
        jrpgmaker::assetimport::LoadGltfScene(gltf_path);
    if (!load.has_value()) {
        std::cerr << "failed to load glTF scene: " << gltf_path.string() << '\n';
        return false;
    }

    std::vector<std::pair<std::vector<float>, std::vector<std::uint32_t>>> meshes_to_draw;
    const auto view = load->scene.Registry().view<jrpgmaker::assetimport::MeshRef>();
    for (const jrpgmaker::core::Entity entity : view) {
        const jrpgmaker::assetimport::MeshRef& ref =
            view.get<jrpgmaker::assetimport::MeshRef>(entity);
        const jrpgmaker::core::MeshData* mesh = load->assets.FindMesh(ref.handle);
        if (mesh == nullptr) {
            continue;
        }
        const glm::mat4 world = load->scene.WorldMatrix(entity);
        meshes_to_draw.emplace_back(BakeWorldPositions(mesh->positions, world), mesh->indices);
    }

    // Column-major view-projection, 16 floats = 64 bytes.
    const glm::mat4 view_projection = camera.ViewProjection();
    std::vector<float> constants(16);
    std::memcpy(constants.data(), &view_projection, sizeof(view_projection));
    return RenderInto(rgba, meshes_to_draw, MakeCameraPipelineDesc(), &constants);
}

// Sampled-texture quad (P3 DEBT-029): a 2x2 four-color texture (top-left red,
// top-right green, bottom-left blue, bottom-right white) is uploaded and
// sampled across a fullscreen quad with a nearest/clamp sampler. Each color
// occupies a 32x32 quadrant of the 64x64 target, giving the golden reference a
// sharp, backend-independent color boundary.
constexpr std::uint32_t kQuadTextureSize = 2;
constexpr std::uint32_t kQuadStride = 5u * sizeof(float);
constexpr VertexAttribute kQuadAttributes[] = {
    VertexAttribute{
        .location = 0,
        .format = VertexAttributeFormat::kFloat3,
        .offset_bytes = 0,
    },
    VertexAttribute{
        .location = 1,
        .format = VertexAttributeFormat::kFloat2,
        .offset_bytes = 3u * sizeof(float),
        .semantic_name = "TEXCOORD",
    },
};

// ---------------------------------------------------------------------------
// Skinned-mesh rendering (P4): loads a glTF scene, samples an animation clip
// (or blends two clips) into per-joint bone matrices, uploads them into a
// per-object uniform buffer, and renders every skinned mesh. The shader maps
// each vertex by up to four influencing bones. Only skinned entities are
// drawn; static meshes in the same file are skipped in v0.
// ---------------------------------------------------------------------------

constexpr std::uint32_t kSkinnedStride =
    3u * sizeof(float) + 4u * sizeof(std::uint16_t) + 4u * sizeof(float);
constexpr VertexAttribute kSkinnedAttributes[] = {
    VertexAttribute{
        .location = 0,
        .format = VertexAttributeFormat::kFloat3,
        .offset_bytes = 0,
    },
    VertexAttribute{
        .location = 1,
        .format = VertexAttributeFormat::kUint16x4,
        .offset_bytes = 3u * sizeof(float),
        .semantic_name = "JOINTS",
    },
    VertexAttribute{
        .location = 2,
        .format = VertexAttributeFormat::kFloat4,
        .offset_bytes = 3u * sizeof(float) + 4u * sizeof(std::uint16_t),
        .semantic_name = "WEIGHTS",
    },
};

// Packed skinned vertex matching the RHI layout: position (float3), joint
// indices (u16x4), weights (float4). sizeof == kSkinnedStride == 36.
struct SkinnedVertex {
    float position[3];
    std::uint16_t joints[4];
    float weights[4];
};

// Interleaves positions + joints + weights into the skinned vertex layout.
std::vector<SkinnedVertex> BuildSkinnedVertices(const jrpgmaker::core::MeshData& mesh) {
    if (!mesh.skinned()) {
        return {};
    }
    std::vector<SkinnedVertex> vertices(mesh.vertex_count());
    for (std::size_t v = 0; v < mesh.vertex_count(); ++v) {
        SkinnedVertex& out = vertices[v];
        out.position[0] = mesh.positions[v * 3u];
        out.position[1] = mesh.positions[v * 3u + 1u];
        out.position[2] = mesh.positions[v * 3u + 2u];
        for (std::uint32_t c = 0; c < 4u; ++c) {
            const std::uint16_t raw = mesh.joints[v * 4u + c];
            // 0xFFFF marks "no influence"; remap to joint 0 so the u16 stays a
            // valid index (the matching weight is zero and never contributes).
            out.joints[c] = raw == 0xFFFFu ? 0u : raw;
        }
        for (std::uint32_t c = 0; c < 4u; ++c) {
            out.weights[c] = mesh.weights[v * 4u + c];
        }
    }
    return vertices;
}

GraphicsPipelineDesc MakeSkinnedPipelineDesc() {
    GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.color_format = Format::kR8G8B8A8Unorm;
    pipeline_desc.vertex_input = VertexInputLayout{
        .attributes = kSkinnedAttributes,
        .attribute_count = static_cast<std::uint32_t>(std::size(kSkinnedAttributes)),
        .stride_bytes = kSkinnedStride,
    };
    // Bone matrices: kMaxBonesPerObject mat4s (matching the shader's fixed
    // uniform array): 32 * 16 floats * 4 bytes = 2048 bytes.
    pipeline_desc.vertex_uniform_size = 32u * 16u * sizeof(float);
#if defined(_WIN32)
    pipeline_desc.vertex_shader =
        ShaderBytecode{jrpgmaker::shaders::kSkinnedVsDxil, jrpgmaker::shaders::kSkinnedVsDxil_size};
    pipeline_desc.pixel_shader =
        ShaderBytecode{jrpgmaker::shaders::kSkinnedPsDxil, jrpgmaker::shaders::kSkinnedPsDxil_size};
#else
    pipeline_desc.vertex_shader =
        ShaderBytecode{jrpgmaker::shaders::kSkinnedVsSpv, jrpgmaker::shaders::kSkinnedVsSpv_size};
    pipeline_desc.pixel_shader =
        ShaderBytecode{jrpgmaker::shaders::kSkinnedPsSpv, jrpgmaker::shaders::kSkinnedPsSpv_size};
#endif
    return pipeline_desc;
}

// Renders skinned entities of the scene with the given per-joint bone matrices.
// `bone_matrices` is a packed array of mat4 (column-major), padded with
// identity matrices up to the shader's fixed uniform array size.
bool RenderSkinned(std::vector<std::uint8_t>& rgba, const std::filesystem::path& gltf_path,
                   const std::vector<glm::mat4>& bone_matrices) {
    const std::optional<jrpgmaker::assetimport::SceneLoad> load =
        jrpgmaker::assetimport::LoadGltfScene(gltf_path);
    if (!load.has_value()) {
        std::cerr << "failed to load glTF scene: " << gltf_path.string() << '\n';
        return false;
    }

    const std::unique_ptr<IDevice> device = CreateDevice(kBackend);
    if (device == nullptr) {
        std::cerr << "failed to create device\n";
        return false;
    }

    const TextureHandle target = device->CreateTexture(
        TextureDesc{.width = kWidth,
                    .height = kHeight,
                    .format = Format::kR8G8B8A8Unorm,
                    .usage = TextureUsage::kRenderTarget | TextureUsage::kReadBack});
    if (target == TextureHandle::kInvalid) {
        std::cerr << "failed to create render target\n";
        return false;
    }

    const PipelineHandle pipeline = device->CreatePipeline(MakeSkinnedPipelineDesc());
    if (pipeline == PipelineHandle::kInvalid) {
        std::cerr << "failed to create pipeline\n";
        device->DestroyTexture(target);
        return false;
    }

    // Per-object uniform buffer holding the bone-matrix array.
    std::vector<std::byte> uniform_data(bone_matrices.size() * sizeof(glm::mat4));
    std::memcpy(uniform_data.data(), bone_matrices.data(),
                bone_matrices.size() * sizeof(glm::mat4));
    const BufferHandle uniform_buffer = device->CreateBuffer(
        BufferDesc{.size_bytes = uniform_data.size(), .usage = BufferUsage::kUniform});
    if (uniform_buffer == BufferHandle::kInvalid) {
        std::cerr << "failed to create uniform buffer\n";
        return false;
    }
    device->MapWrite(uniform_buffer, uniform_data.data(), uniform_data.size());

    bool ok = true;
    ICommandList* command_list = device->CreateCommandList();
    std::vector<BufferHandle> mesh_buffers;
    if (command_list == nullptr) {
        std::cerr << "failed to create command list\n";
        ok = false;
    } else {
        command_list->Begin();
        command_list->BeginRendering(target, kClearColor);
        command_list->SetPipeline(pipeline);
        command_list->SetVertexUniformBuffer(uniform_buffer,
                                             static_cast<std::uint32_t>(uniform_data.size()));

        const auto view =
            load->scene.Registry()
                .view<jrpgmaker::assetimport::MeshRef, jrpgmaker::assetimport::SkinRef>();
        for (const jrpgmaker::core::Entity entity : view) {
            const auto& mesh_ref = view.get<jrpgmaker::assetimport::MeshRef>(entity);
            const jrpgmaker::core::MeshData* mesh = load->assets.FindMesh(mesh_ref.handle);
            if (mesh == nullptr || !mesh->skinned()) {
                continue;
            }
            const std::vector<SkinnedVertex> vertices = BuildSkinnedVertices(*mesh);
            const BufferHandle vertex_buffer = device->CreateBuffer(BufferDesc{
                .size_bytes = static_cast<std::uint64_t>(vertices.size()) * sizeof(SkinnedVertex),
                .usage = BufferUsage::kVertex});
            if (vertex_buffer == BufferHandle::kInvalid) {
                std::cerr << "failed to create vertex buffer\n";
                ok = false;
                break;
            }
            device->MapWrite(vertex_buffer, vertices.data(),
                             static_cast<std::uint64_t>(vertices.size()) * sizeof(SkinnedVertex));
            const BufferHandle index_buffer = device->CreateBuffer(
                BufferDesc{.size_bytes = static_cast<std::uint64_t>(mesh->indices.size()) *
                                         sizeof(std::uint32_t),
                           .usage = BufferUsage::kIndex});
            if (index_buffer == BufferHandle::kInvalid) {
                std::cerr << "failed to create index buffer\n";
                device->DestroyBuffer(vertex_buffer);
                ok = false;
                break;
            }
            device->MapWrite(index_buffer, mesh->indices.data(),
                             static_cast<std::uint64_t>(mesh->indices.size()) *
                                 sizeof(std::uint32_t));
            command_list->SetVertexBuffer(vertex_buffer, kSkinnedStride);
            command_list->SetIndexBuffer(index_buffer, true);
            command_list->DrawIndexed(static_cast<std::uint32_t>(mesh->indices.size()), 1);
            // Buffers must outlive the recording; destroy after Submit below
            // (DestroyBuffer waits on the GPU, which would deadlock mid-recording).
            mesh_buffers.push_back(vertex_buffer);
            mesh_buffers.push_back(index_buffer);
        }

        command_list->EndRendering();
        command_list->End();
        device->Submit(*command_list);
        device->WaitForGpuIdle();
        device->DestroyCommandList(command_list);
    }

    for (const BufferHandle buffer : mesh_buffers) {
        device->DestroyBuffer(buffer);
    }
    device->DestroyBuffer(uniform_buffer);
    device->DestroyPipeline(pipeline);

    if (!ok) {
        device->DestroyTexture(target);
        return false;
    }

    const MappedTexture mapped = device->MapReadBack(target);
    if (mapped.data == nullptr) {
        std::cerr << "readback returned null\n";
        device->DestroyTexture(target);
        return false;
    }

    rgba.resize(static_cast<std::size_t>(kWidth) * kHeight * 4u);
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        const auto* row = reinterpret_cast<const std::uint8_t*>(mapped.data) +
                          static_cast<std::uint64_t>(y) * mapped.row_pitch_bytes;
        std::uint8_t* out_row = rgba.data() + static_cast<std::size_t>(y) * kWidth * 4u;
        std::memcpy(out_row, row, static_cast<std::size_t>(kWidth) * 4u);
    }

    device->DestroyTexture(target);
    return true;
}

// Computes the bone-matrix array for a scene's first skeleton at a given
// animation time, padded with identity matrices up to the shader's fixed
// uniform array size (32 mat4s). `blend_weight` in [0,1] blends clip[0] and
// clip[1] (0 = clip0 only, 1 = clip1 only); requires at least one animation.
bool ComputeSkinBoneMatrices(const std::filesystem::path& gltf_path, float time_seconds,
                             float blend_weight, std::vector<glm::mat4>& out_bones) {
    const std::optional<jrpgmaker::assetimport::SceneLoad> load =
        jrpgmaker::assetimport::LoadGltfScene(gltf_path);
    if (!load.has_value()) {
        std::cerr << "failed to load glTF scene: " << gltf_path.string() << '\n';
        return false;
    }
    if (load->skeletons.empty() || load->animations.empty()) {
        std::cerr << "glTF has no skeleton or animation to sample\n";
        return false;
    }

    const jrpgmaker::core::Skeleton& skeleton = load->skeletons.front().skeleton;
    const jrpgmaker::core::AnimationClip& clip_a = load->animations[0].clip;
    const jrpgmaker::core::AnimationClip* clip_b =
        (blend_weight > 0.0f && load->animations.size() > 1u) ? &load->animations[1].clip : nullptr;

    const jrpgmaker::core::SkeletonPose pose_a =
        jrpgmaker::core::SamplePose(skeleton, clip_a, time_seconds);
    if (clip_b != nullptr) {
        const jrpgmaker::core::SkeletonPose pose_b =
            jrpgmaker::core::SamplePose(skeleton, *clip_b, time_seconds);
        out_bones = jrpgmaker::core::BoneMatrices(
            skeleton, jrpgmaker::core::BlendPose(pose_a, pose_b, blend_weight));
    } else {
        out_bones = jrpgmaker::core::BoneMatrices(skeleton, pose_a);
    }
    // Pad the uniform array to the shader's fixed size (identity matrices are
    // never referenced because their joint weights are zero).
    const glm::mat4 identity(1.0f);
    out_bones.resize(32u, identity);
    return true;
}

const std::vector<float>& QuadVertices() {
    // Two triangles covering the full viewport. UV v is 0 at the top of the
    // texture, matching the pixel row order of the uploaded data.
    static const std::vector<float> vertices = {
        -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, //
        1.0f,  -1.0f, 0.0f, 1.0f, 1.0f, //
        -1.0f, 1.0f,  0.0f, 0.0f, 0.0f, //
        1.0f,  -1.0f, 0.0f, 1.0f, 1.0f, //
        1.0f,  1.0f,  0.0f, 1.0f, 0.0f, //
        -1.0f, 1.0f,  0.0f, 0.0f, 0.0f, //
    };
    return vertices;
}

// Four-color texture data in row-major order (top row first).
std::array<std::uint8_t, kQuadTextureSize * kQuadTextureSize * 4u> MakeQuadTexture() {
    constexpr std::uint8_t kRed[4] = {255, 0, 0, 255};
    constexpr std::uint8_t kGreen[4] = {0, 255, 0, 255};
    constexpr std::uint8_t kBlue[4] = {0, 0, 255, 255};
    constexpr std::uint8_t kWhite[4] = {255, 255, 255, 255};
    std::array<std::uint8_t, kQuadTextureSize * kQuadTextureSize * 4u> pixels{};
    std::memcpy(pixels.data(), kRed, 4u);
    std::memcpy(pixels.data() + 4u, kGreen, 4u);
    std::memcpy(pixels.data() + 8u, kBlue, 4u);
    std::memcpy(pixels.data() + 12u, kWhite, 4u);
    return pixels;
}

bool RenderTexturedQuad(std::vector<std::uint8_t>& rgba) {
    const std::unique_ptr<IDevice> device = CreateDevice(kBackend);
    if (device == nullptr) {
        std::cerr << "failed to create device\n";
        return false;
    }

    const TextureHandle target = device->CreateTexture(
        TextureDesc{.width = kWidth,
                    .height = kHeight,
                    .format = Format::kR8G8B8A8Unorm,
                    .usage = TextureUsage::kRenderTarget | TextureUsage::kReadBack});
    const TextureHandle texture =
        device->CreateTexture(TextureDesc{.width = kQuadTextureSize,
                                          .height = kQuadTextureSize,
                                          .format = Format::kR8G8B8A8Unorm,
                                          .usage = TextureUsage::kSampled});
    if (target == TextureHandle::kInvalid || texture == TextureHandle::kInvalid) {
        std::cerr << "failed to create textures\n";
        return false;
    }

    const auto pixels = MakeQuadTexture();
    device->UploadTexture(texture, pixels.data(),
                          static_cast<std::uint64_t>(kQuadTextureSize) * 4u);

    const SamplerHandle sampler = device->CreateSampler(
        SamplerDesc{.filter = SamplerFilter::kNearest, .address = SamplerAddress::kClamp});
    if (sampler == SamplerHandle::kInvalid) {
        std::cerr << "failed to create sampler\n";
        return false;
    }

    GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.color_format = Format::kR8G8B8A8Unorm;
    pipeline_desc.vertex_input = VertexInputLayout{
        .attributes = kQuadAttributes,
        .attribute_count = 2,
        .stride_bytes = kQuadStride,
    };
    pipeline_desc.sample_slot = 1;
#if defined(_WIN32)
    pipeline_desc.vertex_shader = ShaderBytecode{jrpgmaker::shaders::kTexturedVsDxil,
                                                 jrpgmaker::shaders::kTexturedVsDxil_size};
    pipeline_desc.pixel_shader = ShaderBytecode{jrpgmaker::shaders::kTexturedPsDxil,
                                                jrpgmaker::shaders::kTexturedPsDxil_size};
#else
    pipeline_desc.vertex_shader =
        ShaderBytecode{jrpgmaker::shaders::kTexturedVsSpv, jrpgmaker::shaders::kTexturedVsSpv_size};
    pipeline_desc.pixel_shader =
        ShaderBytecode{jrpgmaker::shaders::kTexturedPsSpv, jrpgmaker::shaders::kTexturedPsSpv_size};
#endif

    const PipelineHandle pipeline = device->CreatePipeline(pipeline_desc);
    if (pipeline == PipelineHandle::kInvalid) {
        std::cerr << "failed to create pipeline\n";
        return false;
    }

    const BufferHandle vertex_buffer = device->CreateBuffer(
        BufferDesc{.size_bytes = static_cast<std::uint64_t>(QuadVertices().size()) * sizeof(float),
                   .usage = BufferUsage::kVertex});
    device->MapWrite(vertex_buffer, QuadVertices().data(),
                     static_cast<std::uint64_t>(QuadVertices().size()) * sizeof(float));

    ICommandList* command_list = device->CreateCommandList();
    command_list->Begin();
    command_list->BeginRendering(target, kClearColor);
    command_list->SetPipeline(pipeline);
    command_list->SetSampledTexture(texture, sampler);
    command_list->SetVertexBuffer(vertex_buffer, kQuadStride);
    command_list->Draw(6, 1);
    command_list->EndRendering();
    command_list->End();
    device->Submit(*command_list);
    device->WaitForGpuIdle();
    device->DestroyCommandList(command_list);
    device->DestroyBuffer(vertex_buffer);
    device->DestroyPipeline(pipeline);
    device->DestroySampler(sampler);
    device->DestroyTexture(texture);

    const MappedTexture mapped = device->MapReadBack(target);
    if (mapped.data == nullptr) {
        std::cerr << "readback returned null\n";
        device->DestroyTexture(target);
        return false;
    }

    rgba.resize(static_cast<std::size_t>(kWidth) * kHeight * 4u);
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        const auto* row = reinterpret_cast<const std::uint8_t*>(mapped.data) +
                          static_cast<std::uint64_t>(y) * mapped.row_pitch_bytes;
        std::uint8_t* out_row = rgba.data() + static_cast<std::size_t>(y) * kWidth * 4u;
        std::memcpy(out_row, row, static_cast<std::size_t>(kWidth) * 4u);
    }

    device->DestroyTexture(target);
    return true;
}

int WritePpmFromRgba(const std::string& output_path, const std::vector<std::uint8_t>& rgba) {
    golden::Image image;
    image.width = kWidth;
    image.height = kHeight;
    image.rgb.resize(static_cast<std::size_t>(kWidth) * kHeight * 3u);
    for (std::size_t i = 0; i < image.rgb.size(); ++i) {
        image.rgb[i] = rgba[i / 3u * 4u + (i % 3u)];
    }

    std::string error;
    if (!golden::WritePpm(output_path, image, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    std::cout << "wrote " << output_path << '\n';
    return 0;
}

int CompareRgba(const std::string& reference_path, int tolerance,
                const std::vector<std::uint8_t>& rgba) {
    golden::Image reference;
    std::string error;
    if (!golden::ReadPpm(reference_path, reference, error)) {
        std::cerr << error << '\n';
        return 1;
    }

    const golden::CompareResult result = golden::CompareRgba8(
        rgba.data(), static_cast<std::uint64_t>(kWidth) * 4u, reference, tolerance);
    std::cout << "compared " << result.pixels_compared << " pixels against " << reference_path
              << " (tolerance " << tolerance << "): " << result.pixels_differing
              << " differing, max channel delta " << result.max_channel_delta << '\n';
    return result.passed ? 0 : 1;
}

} // namespace

namespace {

int RunCli(int argc, char** argv);

} // namespace

int main(int argc, char** argv) {
    // Top-level guard: an uncaught exception would abort() and pop a blocking
    // CRT error dialog in headless sessions; report and fail cleanly instead.
    try {
        return RunCli(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "goldenimage: fatal: " << error.what() << '\n';
        return 1;
    }
}

namespace {

int RunCli(int argc, char** argv) {
    // Usage:
    //   goldenimage generate <out.ppm> [gltf_path]
    //   goldenimage compare <ref.ppm> [tolerance] [gltf_path]
    //   goldenimage generate-scene <out.ppm> <gltf_path>
    //   goldenimage compare-scene <ref.ppm> <gltf_path> [tolerance]
    //   goldenimage generate-camera <out.ppm> <gltf_path>
    //   goldenimage compare-camera <ref.ppm> <gltf_path> [tolerance]
    //   goldenimage generate-texture <out.ppm>
    //   goldenimage compare-texture <ref.ppm> [tolerance]
    // The single-mesh glTF path defaults to the committed triangle asset
    // (assets/art/meshes/triangle.gltf), whose geometry matches the golden
    // reference.
    const std::filesystem::path default_gltf =
        std::filesystem::path(JRPGMAKER_ASSET_DIR) / "art" / "meshes" / "triangle.gltf";
    const std::filesystem::path default_project =
        std::filesystem::path(JRPGMAKER_ASSET_DIR) / "data" / "project_demo.json";

    if (argc < 3) {
        std::cerr
            << "usage:\n"
            << "  goldenimage generate <out.ppm> [gltf_path]\n"
            << "  goldenimage compare <ref.ppm> [tolerance] [gltf_path]\n"
            << "  goldenimage generate-scene <out.ppm> <gltf_path>\n"
            << "  goldenimage compare-scene <ref.ppm> <gltf_path> [tolerance]\n"
            << "  goldenimage generate-camera <out.ppm> <gltf_path>\n"
            << "  goldenimage compare-camera <ref.ppm> <gltf_path> [tolerance]\n"
            << "  goldenimage generate-texture <out.ppm>\n"
            << "  goldenimage compare-texture <ref.ppm> [tolerance]\n"
            << "  goldenimage generate-project-style <out.ppm> <project.json> [gltf]\n"
            << "  goldenimage compare-project-style <ref.ppm> <project.json> [tolerance] [gltf]\n"
            << "  goldenimage generate-skin <out.ppm> <gltf> [time] [blend]\n"
            << "  goldenimage compare-skin <ref.ppm> <gltf> [time] [blend] [tolerance]\n";
        return 2;
    }

    // The camera golden uses a fixed observation pose (P2 fly-camera baseline):
    // looking at the origin from (2,1.5,2), matching the committed reference.
    jrpgmaker::core::Camera camera{};
    camera.eye = {2.0f, 1.5f, 2.0f};
    camera.target = {0.45f, 0.25f, 0.0f};
    camera.aspect_ratio = static_cast<float>(kWidth) / static_cast<float>(kHeight);

    const std::string command = argv[1];
    if (command == "generate") {
        const std::filesystem::path gltf_path = argc >= 4 ? argv[3] : default_gltf;
        std::vector<std::uint8_t> rgba;
        if (!RenderTriangle(rgba, gltf_path)) {
            return 1;
        }
        return WritePpmFromRgba(argv[2], rgba);
    }
    if (command == "compare") {
        int tolerance = 1;
        const std::filesystem::path gltf_path = argc >= 5 ? argv[4] : default_gltf;
        if (argc >= 4) {
            tolerance = std::atoi(argv[3]);
        }
        std::vector<std::uint8_t> rgba;
        if (!RenderTriangle(rgba, gltf_path)) {
            return 1;
        }
        return CompareRgba(argv[2], tolerance, rgba);
    }
    if (command == "generate-scene") {
        if (argc < 4) {
            std::cerr << "generate-scene requires a glTF path\n";
            return 2;
        }
        std::vector<std::uint8_t> rgba;
        if (!RenderScene(rgba, argv[3])) {
            return 1;
        }
        return WritePpmFromRgba(argv[2], rgba);
    }
    if (command == "compare-scene") {
        if (argc < 4) {
            std::cerr << "compare-scene requires a glTF path\n";
            return 2;
        }
        int tolerance = 1;
        if (argc >= 5) {
            tolerance = std::atoi(argv[4]);
        }
        std::vector<std::uint8_t> rgba;
        if (!RenderScene(rgba, argv[3])) {
            return 1;
        }
        return CompareRgba(argv[2], tolerance, rgba);
    }
    if (command == "generate-camera") {
        if (argc < 4) {
            std::cerr << "generate-camera requires a glTF path\n";
            return 2;
        }
        std::vector<std::uint8_t> rgba;
        if (!RenderSceneWithCamera(rgba, argv[3], camera)) {
            return 1;
        }
        return WritePpmFromRgba(argv[2], rgba);
    }
    if (command == "compare-camera") {
        if (argc < 4) {
            std::cerr << "compare-camera requires a glTF path\n";
            return 2;
        }
        int tolerance = 1;
        if (argc >= 5) {
            tolerance = std::atoi(argv[4]);
        }
        std::vector<std::uint8_t> rgba;
        if (!RenderSceneWithCamera(rgba, argv[3], camera)) {
            return 1;
        }
        return CompareRgba(argv[2], tolerance, rgba);
    }
    if (command == "generate-texture") {
        std::vector<std::uint8_t> rgba;
        if (!RenderTexturedQuad(rgba)) {
            return 1;
        }
        return WritePpmFromRgba(argv[2], rgba);
    }
    if (command == "compare-texture") {
        int tolerance = 1;
        if (argc >= 4) {
            tolerance = std::atoi(argv[3]);
        }
        std::vector<std::uint8_t> rgba;
        if (!RenderTexturedQuad(rgba)) {
            return 1;
        }
        return CompareRgba(argv[2], tolerance, rgba);
    }
    if (command == "generate-project-style" || command == "compare-project-style") {
        if (argc < 4) {
            std::cerr << command << " requires a project manifest\n";
            return 2;
        }
        const bool generate = command == "generate-project-style";
        const std::filesystem::path project_path = argv[3];
        const std::filesystem::path gltf_path =
            generate ? (argc >= 5 ? argv[4] : default_gltf) : (argc >= 6 ? argv[5] : default_gltf);
        const int tolerance = !generate && argc >= 5 ? std::atoi(argv[4]) : 1;
        std::vector<std::uint8_t> rgba;
        if (!RenderProjectStyledTriangle(rgba, gltf_path, project_path)) {
            return 1;
        }
        return generate ? WritePpmFromRgba(argv[2], rgba) : CompareRgba(argv[2], tolerance, rgba);
    }
    // Skinned-mesh golden (P4): renders the scene's skinned entities with bone
    // matrices sampled from the first animation at `time` seconds, optionally
    // blended toward the second animation by `blend` in [0,1].
    //   goldenimage generate-skin <out.ppm> <gltf> [time] [blend]
    //   goldenimage compare-skin <ref.ppm> <gltf> [time] [blend] [tolerance]
    if (command == "generate-skin" || command == "compare-skin") {
        if (argc < 4) {
            std::cerr << command << " requires a glTF path\n";
            return 2;
        }
        const float time_seconds = argc >= 5 ? static_cast<float>(std::atof(argv[4])) : 0.0f;
        const float blend_weight = argc >= 6 ? static_cast<float>(std::atof(argv[5])) : 0.0f;
        std::vector<glm::mat4> bones;
        if (!ComputeSkinBoneMatrices(argv[3], time_seconds, blend_weight, bones)) {
            return 1;
        }
        std::vector<std::uint8_t> rgba;
        if (!RenderSkinned(rgba, argv[3], bones)) {
            return 1;
        }
        if (command == "generate-skin") {
            return WritePpmFromRgba(argv[2], rgba);
        }
        int tolerance = 1;
        if (argc >= 7) {
            tolerance = std::atoi(argv[6]);
        }
        return CompareRgba(argv[2], tolerance, rgba);
    }

    std::cerr << "unknown command '" << command << "'\n";
    return 2;
}

} // namespace
