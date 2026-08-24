#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

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

// The triangle positions must match the pre-P2 SV_VertexID-generated geometry
// (see shaders/triangle.hlsl) so the golden reference stays valid: the golden
// gate verifies the RHI vertex-input path without re-baselining the image.
constexpr float kTriangleVertices[] = {
    -0.5f, -0.5f, 0.0f, //
    0.5f,  -0.5f, 0.0f, //
    0.0f,  0.5f,  0.0f, //
};
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

std::filesystem::path GoldenPath(const char* name) {
    return std::filesystem::path(JRPGMAKER_GOLDEN_DIR) / name;
}

} // namespace

// Full-frame comparison against the committed lavapipe-generated reference
// (tests/golden/triangle_64x64.ppm). The reference is asymmetric about the
// horizontal midline, so this locks the NDC-Y convention of both backends.
TEST_CASE("triangle renders match the committed golden reference", "[rhi][golden][triangle]") {
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
        BufferDesc{.size_bytes = sizeof(kTriangleVertices), .usage = BufferUsage::kVertex});
    REQUIRE(vertex_buffer != BufferHandle::kInvalid);
    device->MapWrite(vertex_buffer, kTriangleVertices, sizeof(kTriangleVertices));

    ICommandList* command_list = device->CreateCommandList();
    REQUIRE(command_list != nullptr);
    command_list->Begin();
    command_list->BeginRendering(target, kClearColor);
    command_list->SetPipeline(pipeline);
    command_list->SetVertexBuffer(vertex_buffer, kTriangleStride);
    command_list->Draw(3, 1);
    command_list->EndRendering();
    command_list->End();
    device->Submit(*command_list);
    device->WaitForGpuIdle();
    device->DestroyCommandList(command_list);
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