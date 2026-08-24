#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "jrpgmaker/assetimport/asset_import.hpp"
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

// Vertex input: single interleaved buffer with one float3 position attribute.
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

} // namespace

// Full-frame comparison against the committed lavapipe-generated reference
// (tests/golden/triangle_64x64.ppm). The reference is asymmetric about the
// horizontal midline, so this locks the NDC-Y convention of both backends.
//
// The geometry is no longer hard-coded: it is imported from a glTF asset file
// (assets/art/meshes/triangle.gltf) whose vertices match the pre-P2
// SV_VertexID-generated triangle exactly, so the golden reference stays valid.
// P2 adds the index-buffer path: positions go to a vertex buffer, indices to
// an index buffer, and the draw is DrawIndexed(3, 1).
TEST_CASE("triangle renders match the committed golden reference", "[rhi][golden][triangle]") {
    const std::filesystem::path gltf_path = AssetPath("art/meshes/triangle.gltf");
    REQUIRE(std::filesystem::exists(gltf_path));
    const std::optional<jrpgmaker::core::MeshData> mesh =
        jrpgmaker::assetimport::LoadGltfMesh(gltf_path);
    REQUIRE(mesh.has_value());
    REQUIRE(mesh->vertex_count() == 3);
    REQUIRE(mesh->index_count() == 3);

    const std::unique_ptr<IDevice> device = CreateDevice(kBackend);
    REQUIRE(device != nullptr);

    const TextureHandle target = device->CreateTexture(
        TextureDesc{.width = kWidth,
                    .height = kHeight,
                    .format = Format::kR8G8B8A8Unorm,
                    .usage = TextureUsage::kRenderTarget | TextureUsage::kReadBack});
    REQUIRE(target != TextureHandle::kInvalid);

#if defined(_WIN32)
    const GraphicsPipelineDesc pipeline_desc{
        .vertex_shader = ShaderBytecode{jrpgmaker::shaders::kTriangleVsDxil,
                                        jrpgmaker::shaders::kTriangleVsDxil_size},
        .pixel_shader = ShaderBytecode{jrpgmaker::shaders::kTrianglePsDxil,
                                       jrpgmaker::shaders::kTrianglePsDxil_size},
        .color_format = Format::kR8G8B8A8Unorm,
        .vertex_input = VertexInputLayout{
            .attributes = kTriangleAttributes,
            .attribute_count = 1,
            .stride_bytes = kTriangleStride,
        }};
#else
    const GraphicsPipelineDesc pipeline_desc{
        .vertex_shader = ShaderBytecode{jrpgmaker::shaders::kTriangleVsSpv,
                                        jrpgmaker::shaders::kTriangleVsSpv_size},
        .pixel_shader = ShaderBytecode{jrpgmaker::shaders::kTrianglePsSpv,
                                       jrpgmaker::shaders::kTrianglePsSpv_size},
        .color_format = Format::kR8G8B8A8Unorm,
        .vertex_input = VertexInputLayout{
            .attributes = kTriangleAttributes,
            .attribute_count = 1,
            .stride_bytes = kTriangleStride,
        }};
#endif

    const PipelineHandle pipeline = device->CreatePipeline(pipeline_desc);
    REQUIRE(pipeline != PipelineHandle::kInvalid);

    const BufferHandle vertex_buffer = device->CreateBuffer(
        BufferDesc{.size_bytes = static_cast<std::uint64_t>(mesh->positions.size()) * sizeof(float),
                   .usage = BufferUsage::kVertex});
    REQUIRE(vertex_buffer != BufferHandle::kInvalid);
    device->MapWrite(vertex_buffer, mesh->positions.data(),
                     static_cast<std::uint64_t>(mesh->positions.size()) * sizeof(float));

    const BufferHandle index_buffer = device->CreateBuffer(BufferDesc{
        .size_bytes = static_cast<std::uint64_t>(mesh->indices.size()) * sizeof(std::uint32_t),
        .usage = BufferUsage::kIndex});
    REQUIRE(index_buffer != BufferHandle::kInvalid);
    device->MapWrite(index_buffer, mesh->indices.data(),
                     static_cast<std::uint64_t>(mesh->indices.size()) * sizeof(std::uint32_t));

    ICommandList* command_list = device->CreateCommandList();
    REQUIRE(command_list != nullptr);
    command_list->Begin();
    command_list->BeginRendering(target, kClearColor);
    command_list->SetPipeline(pipeline);
    command_list->SetVertexBuffer(vertex_buffer, kTriangleStride);
    command_list->SetIndexBuffer(index_buffer, true);
    command_list->DrawIndexed(3, 1);
    command_list->EndRendering();
    command_list->End();
    device->Submit(*command_list);
    device->WaitForGpuIdle();
    device->DestroyCommandList(command_list);
    device->DestroyBuffer(index_buffer);
    device->DestroyBuffer(vertex_buffer);

    const MappedTexture mapped = device->MapReadBack(target);
    REQUIRE(mapped.data != nullptr);

    golden::Image reference;
    std::string error;
    const std::filesystem::path reference_path = GoldenPath("triangle_64x64.ppm");
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