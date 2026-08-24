#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "jrpgmaker/assetimport/asset_import.hpp"
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

constexpr std::uint32_t kTriangleStride = 3u * sizeof(float);
constexpr VertexAttribute kTriangleAttributes[] = {
    VertexAttribute{
        .location = 0,
        .format = VertexAttributeFormat::kFloat3,
        .offset_bytes = 0,
    },
};

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

// Applies a world matrix to tightly packed float3 positions (same baking the
// goldenimage CLI performs; kept here so the unit test is self-contained).
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

} // namespace

// Renders the committed glTF scene (assets/art/meshes/scene_hierarchy.gltf):
// a root node translated (0.25,0,0) and scaled 0.5, with a mesh child
// translated (0.4,0,0). Every mesh-bearing entity is drawn through its world
// matrix, so the resulting image differs from the plain triangle golden and
// locks the transform-composition path on both backends. The reference is
// lavapipe-generated (tests/golden/scene_64x64.ppm).
TEST_CASE("gltf scene renders through world transforms against the golden reference",
          "[rhi][golden][scene]") {
    const std::filesystem::path gltf_path = AssetPath("art/meshes/scene_hierarchy.gltf");
    REQUIRE(std::filesystem::exists(gltf_path));
    const std::optional<jrpgmaker::assetimport::SceneLoad> load =
        jrpgmaker::assetimport::LoadGltfScene(gltf_path);
    REQUIRE(load.has_value());
    REQUIRE(load->assets.live_count() == 1);

    const std::unique_ptr<IDevice> device = CreateDevice(kBackend);
    REQUIRE(device != nullptr);

    const TextureHandle target = device->CreateTexture(
        TextureDesc{.width = kWidth,
                    .height = kHeight,
                    .format = Format::kR8G8B8A8Unorm,
                    .usage = TextureUsage::kRenderTarget | TextureUsage::kReadBack});
    REQUIRE(target != TextureHandle::kInvalid);

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
    const PipelineHandle pipeline = device->CreatePipeline(pipeline_desc);
    REQUIRE(pipeline != PipelineHandle::kInvalid);

    struct UploadedMesh {
        BufferHandle vertex_buffer;
        BufferHandle index_buffer;
        std::uint32_t index_count = 0;
    };
    std::vector<UploadedMesh> uploaded;

    const auto meshes = load->scene.Registry().view<jrpgmaker::assetimport::MeshRef>();
    for (const jrpgmaker::core::Entity entity : meshes) {
        const jrpgmaker::assetimport::MeshRef& ref =
            meshes.get<jrpgmaker::assetimport::MeshRef>(entity);
        const jrpgmaker::core::MeshData* mesh = load->assets.FindMesh(ref.handle);
        REQUIRE(mesh != nullptr);

        const glm::mat4 world = load->scene.WorldMatrix(entity);
        const std::vector<float> baked = BakeWorldPositions(mesh->positions, world);

        const BufferHandle vertex_buffer = device->CreateBuffer(
            BufferDesc{.size_bytes = static_cast<std::uint64_t>(baked.size()) * sizeof(float),
                       .usage = BufferUsage::kVertex});
        REQUIRE(vertex_buffer != BufferHandle::kInvalid);
        device->MapWrite(vertex_buffer, baked.data(),
                         static_cast<std::uint64_t>(baked.size()) * sizeof(float));

        const BufferHandle index_buffer = device->CreateBuffer(BufferDesc{
            .size_bytes = static_cast<std::uint64_t>(mesh->indices.size()) * sizeof(std::uint32_t),
            .usage = BufferUsage::kIndex});
        REQUIRE(index_buffer != BufferHandle::kInvalid);
        device->MapWrite(index_buffer, mesh->indices.data(),
                         static_cast<std::uint64_t>(mesh->indices.size()) * sizeof(std::uint32_t));

        uploaded.push_back(UploadedMesh{vertex_buffer, index_buffer,
                                        static_cast<std::uint32_t>(mesh->indices.size())});
    }
    REQUIRE(uploaded.size() == 1);

    ICommandList* command_list = device->CreateCommandList();
    REQUIRE(command_list != nullptr);
    command_list->Begin();
    command_list->BeginRendering(target, kClearColor);
    command_list->SetPipeline(pipeline);
    for (const UploadedMesh& mesh : uploaded) {
        command_list->SetVertexBuffer(mesh.vertex_buffer, kTriangleStride);
        command_list->SetIndexBuffer(mesh.index_buffer, true);
        command_list->DrawIndexed(mesh.index_count, 1);
    }
    command_list->EndRendering();
    command_list->End();
    device->Submit(*command_list);
    device->WaitForGpuIdle();
    device->DestroyCommandList(command_list);

    for (const UploadedMesh& mesh : uploaded) {
        device->DestroyBuffer(mesh.index_buffer);
        device->DestroyBuffer(mesh.vertex_buffer);
    }

    const MappedTexture mapped = device->MapReadBack(target);
    REQUIRE(mapped.data != nullptr);

    golden::Image reference;
    std::string error;
    const std::filesystem::path reference_path = GoldenPath("scene_64x64.ppm");
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