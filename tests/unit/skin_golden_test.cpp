#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "jrpgmaker/assetimport/asset_import.hpp"
#include "jrpgmaker/core/animation.hpp"
#include "jrpgmaker/core/asset.hpp"
#include "jrpgmaker/core/scene.hpp"
#include "jrpgmaker/rhi/command_list.hpp"
#include "jrpgmaker/rhi/device.hpp"
#include "jrpgmaker/rhi/device_factory.hpp"
#include "jrpgmaker/rhi/handles.hpp"
#include "shaders_generated.hpp"

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
constexpr int kTolerance = 2;

constexpr std::uint32_t kSkinnedStride =
    3u * sizeof(float) + 4u * sizeof(std::uint16_t) + 4u * sizeof(float);
constexpr std::uint32_t kMaxBonesPerObject = 32u;

#ifndef JRPGMAKER_GOLDEN_DIR
#error "JRPGMAKER_GOLDEN_DIR must be defined by the build"
#endif
#ifndef JRPGMAKER_ASSET_DIR
#error "JRPGMAKER_ASSET_DIR must be defined by the build"
#endif

std::filesystem::path GoldenPath(const char* name) {
    return std::filesystem::path(JRPGMAKER_GOLDEN_DIR) / name;
}

std::filesystem::path AssetPath(const char* relative) {
    return std::filesystem::path(JRPGMAKER_ASSET_DIR) / relative;
}

// Packed skinned vertex matching the pipeline layout (36 bytes).
struct SkinnedVertex {
    float position[3];
    std::uint16_t joints[4];
    float weights[4];
};

std::vector<SkinnedVertex> BuildSkinnedVertices(const jrpgmaker::core::MeshData& mesh) {
    std::vector<SkinnedVertex> vertices(mesh.vertex_count());
    for (std::size_t v = 0; v < mesh.vertex_count(); ++v) {
        SkinnedVertex& out = vertices[v];
        out.position[0] = mesh.positions[v * 3u];
        out.position[1] = mesh.positions[v * 3u + 1u];
        out.position[2] = mesh.positions[v * 3u + 2u];
        for (std::uint32_t c = 0; c < 4u; ++c) {
            const std::uint16_t raw = mesh.joints[v * 4u + c];
            out.joints[c] = raw == 0xFFFFu ? 0u : raw;
        }
        for (std::uint32_t c = 0; c < 4u; ++c) {
            out.weights[c] = mesh.weights[v * 4u + c];
        }
    }
    return vertices;
}

GraphicsPipelineDesc MakeSkinnedPipelineDesc() {
    static const VertexAttribute kAttributes[] = {
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
    GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.color_format = Format::kR8G8B8A8Unorm;
    pipeline_desc.vertex_input = VertexInputLayout{
        .attributes = kAttributes,
        .attribute_count = 3u,
        .stride_bytes = kSkinnedStride,
    };
    pipeline_desc.vertex_uniform_size = kMaxBonesPerObject * 16u * sizeof(float);
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

// Bone matrices for the first skeleton at `time`, blending clip0 toward clip1
// by `blend` in [0,1]; padded to the shader's fixed uniform array size.
std::vector<glm::mat4> ComputeBones(const jrpgmaker::assetimport::SceneLoad& load, float time,
                                    float blend) {
    REQUIRE(!load.skeletons.empty());
    REQUIRE(load.animations.size() >= 2u);
    const jrpgmaker::core::Skeleton& skeleton = load.skeletons.front().skeleton;
    const jrpgmaker::core::SkeletonPose a =
        jrpgmaker::core::SamplePose(skeleton, load.animations[0].clip, time);
    const jrpgmaker::core::SkeletonPose b =
        jrpgmaker::core::SamplePose(skeleton, load.animations[1].clip, time);
    std::vector<glm::mat4> bones =
        jrpgmaker::core::BoneMatrices(skeleton, jrpgmaker::core::BlendPose(a, b, blend));
    bones.resize(kMaxBonesPerObject, glm::mat4(1.0f));
    return bones;
}

// Renders every skinned entity with `bones` and compares against `golden_name`.
void CheckSkinGolden(const char* golden_name, float time, float blend) {
    const std::filesystem::path gltf_path = AssetPath("art/meshes/arm_skinned.gltf");
    REQUIRE(std::filesystem::exists(gltf_path));
    const std::optional<jrpgmaker::assetimport::SceneLoad> load =
        jrpgmaker::assetimport::LoadGltfScene(gltf_path);
    REQUIRE(load.has_value());
    const std::vector<glm::mat4> bones = ComputeBones(*load, time, blend);

    const std::unique_ptr<IDevice> device = CreateDevice(kBackend);
    REQUIRE(device != nullptr);

    const TextureHandle target = device->CreateTexture(
        TextureDesc{.width = kWidth,
                    .height = kHeight,
                    .format = Format::kR8G8B8A8Unorm,
                    .usage = TextureUsage::kRenderTarget | TextureUsage::kReadBack});
    REQUIRE(target != TextureHandle::kInvalid);

    const PipelineHandle pipeline = device->CreatePipeline(MakeSkinnedPipelineDesc());
    REQUIRE(pipeline != PipelineHandle::kInvalid);

    std::vector<std::byte> uniform_data(bones.size() * sizeof(glm::mat4));
    std::memcpy(uniform_data.data(), bones.data(), uniform_data.size());
    const BufferHandle uniform_buffer = device->CreateBuffer(
        BufferDesc{.size_bytes = uniform_data.size(), .usage = BufferUsage::kUniform});
    REQUIRE(uniform_buffer != BufferHandle::kInvalid);
    device->MapWrite(uniform_buffer, uniform_data.data(), uniform_data.size());

    struct UploadedMesh {
        BufferHandle vertex_buffer;
        BufferHandle index_buffer;
        std::uint32_t index_count = 0;
    };
    std::vector<UploadedMesh> uploaded;

    const auto view = load->scene.Registry()
                          .view<jrpgmaker::assetimport::MeshRef, jrpgmaker::assetimport::SkinRef>();
    for (const jrpgmaker::core::Entity entity : view) {
        const auto& mesh_ref = view.get<jrpgmaker::assetimport::MeshRef>(entity);
        const jrpgmaker::core::MeshData* mesh = load->assets.FindMesh(mesh_ref.handle);
        REQUIRE(mesh != nullptr);
        REQUIRE(mesh->skinned());

        const std::vector<SkinnedVertex> vertices = BuildSkinnedVertices(*mesh);
        const BufferHandle vertex_buffer = device->CreateBuffer(BufferDesc{
            .size_bytes = static_cast<std::uint64_t>(vertices.size()) * sizeof(SkinnedVertex),
            .usage = BufferUsage::kVertex});
        REQUIRE(vertex_buffer != BufferHandle::kInvalid);
        device->MapWrite(vertex_buffer, vertices.data(),
                         static_cast<std::uint64_t>(vertices.size()) * sizeof(SkinnedVertex));

        const BufferHandle index_buffer = device->CreateBuffer(BufferDesc{
            .size_bytes = static_cast<std::uint64_t>(mesh->indices.size()) * sizeof(std::uint32_t),
            .usage = BufferUsage::kIndex});
        REQUIRE(index_buffer != BufferHandle::kInvalid);
        device->MapWrite(index_buffer, mesh->indices.data(),
                         static_cast<std::uint64_t>(mesh->indices.size()) * sizeof(std::uint32_t));

        uploaded.push_back(UploadedMesh{vertex_buffer, index_buffer,
                                        static_cast<std::uint32_t>(mesh->indices.size())});
    }
    REQUIRE(!uploaded.empty());

    ICommandList* command_list = device->CreateCommandList();
    REQUIRE(command_list != nullptr);
    command_list->Begin();
    command_list->BeginRendering(target, kClearColor);
    command_list->SetPipeline(pipeline);
    command_list->SetVertexUniformBuffer(uniform_buffer,
                                         static_cast<std::uint32_t>(uniform_data.size()));
    for (const UploadedMesh& item : uploaded) {
        command_list->SetVertexBuffer(item.vertex_buffer, kSkinnedStride);
        command_list->SetIndexBuffer(item.index_buffer, true);
        command_list->DrawIndexed(item.index_count, 1);
    }
    command_list->EndRendering();
    command_list->End();
    device->Submit(*command_list);
    device->WaitForGpuIdle();
    device->DestroyCommandList(command_list);

    for (const UploadedMesh& item : uploaded) {
        device->DestroyBuffer(item.index_buffer);
        device->DestroyBuffer(item.vertex_buffer);
    }
    device->DestroyBuffer(uniform_buffer);

    const MappedTexture mapped = device->MapReadBack(target);
    REQUIRE(mapped.data != nullptr);

    golden::Image reference;
    std::string error;
    const std::filesystem::path reference_path = GoldenPath(golden_name);
    REQUIRE(golden::ReadPpm(reference_path, reference, error));
    REQUIRE(reference.width == kWidth);
    REQUIRE(reference.height == kHeight);

    const golden::CompareResult result =
        golden::CompareRgba8(reinterpret_cast<const std::uint8_t*>(mapped.data),
                             mapped.row_pitch_bytes, reference, kTolerance);
    INFO("reference: " << reference_path.string());
    INFO("max channel delta: " << result.max_channel_delta << ", differing pixels: "
                               << result.pixels_differing << " / " << result.pixels_compared);
    CHECK(result.passed);

    device->WaitForGpuIdle();
    device->DestroyPipeline(pipeline);
    device->DestroyTexture(target);
}

} // namespace

// Skinned-mesh golden references (lavapipe-generated): the bind pose, the full
// wave pose (elbow rotated 90 degrees around Z), and the halfway blend. The
// three images differ, locking bone sampling, per-object uniform upload and
// two-clip blending on both backends.
TEST_CASE("skinned mesh renders the bind pose against the golden reference",
          "[rhi][golden][skin]") {
    CheckSkinGolden("skin_bind_64x64.ppm", 0.0f, 0.0f);
}

TEST_CASE("skinned mesh renders the wave pose against the golden reference",
          "[rhi][golden][skin]") {
    CheckSkinGolden("skin_wave_64x64.ppm", 1.0f, 1.0f);
}

TEST_CASE("skinned mesh renders the blended pose against the golden reference",
          "[rhi][golden][skin]") {
    CheckSkinGolden("skin_blend_64x64.ppm", 1.0f, 0.5f);
}
