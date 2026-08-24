#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>

#include "jrpgmaker/rhi/command_list.hpp"
#include "jrpgmaker/rhi/device.hpp"
#include "jrpgmaker/rhi/device_factory.hpp"
#include "jrpgmaker/rhi/handles.hpp"
#include "shaders_generated.hpp"

namespace {

using namespace jrpgmaker::rhi;

#if defined(_WIN32)
constexpr Backend kBackend = Backend::kD3D12;
#else
constexpr Backend kBackend = Backend::kVulkan;
#endif

constexpr std::uint32_t kWidth = 64;
constexpr std::uint32_t kHeight = 64;
constexpr ClearColor kClearColor{0.0f, 0.0f, 0.0f, 1.0f};
constexpr std::uint8_t kClearByte = 0;
constexpr std::uint8_t kTriangleBlue = 255;

// The triangle covers the NDC area with vertices (-0.5,-0.5), (0.5,-0.5),
// (0,0.5). On a 64x64 target the interior spans roughly x in [16,48] and
// y in [17,47]; the sample points below stay clear of the rasterization
// edge. The corners remain the clear color.
struct SamplePoint {
    std::uint32_t x;
    std::uint32_t y;
    std::uint8_t expected_blue;
};

constexpr SamplePoint kSamples[] = {
    {32, 32, kTriangleBlue}, // triangle interior (centroid)
    {32, 24, kTriangleBlue}, // interior, lower half
    {32, 40, kTriangleBlue}, // interior, upper half
    {36, 32, kTriangleBlue}, // interior, right of center
    {28, 32, kTriangleBlue}, // interior, left of center
    {2, 2, kClearByte},      // top-left corner outside
    {62, 2, kClearByte},     // top-right corner outside
    {62, 62, kClearByte},    // bottom-right corner outside
    {2, 62, kClearByte},     // bottom-left corner outside
};

void CheckPixelBlue(const MappedTexture& mapped, std::uint32_t x, std::uint32_t y,
                    std::uint8_t expected_blue) {
    const auto* row = reinterpret_cast<const std::uint8_t*>(mapped.data) +
                      static_cast<std::uint64_t>(y) * mapped.row_pitch_bytes;
    const int r = row[x * 4 + 0];
    const int g = row[x * 4 + 1];
    const int b = row[x * 4 + 2];
    INFO("pixel (" << x << ", " << y << "): rgba(" << r << ", " << g << ", " << b << ")");
    CHECK((b > expected_blue ? b - expected_blue : expected_blue - b) <= 1);
}

} // namespace

TEST_CASE("triangle renders with vertex-shader-generated geometry", "[rhi][golden][triangle]") {
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
        .color_format = Format::kR8G8B8A8Unorm};
#else
    const GraphicsPipelineDesc pipeline_desc{
        .vertex_shader = ShaderBytecode{jrpgmaker::shaders::kTriangleVsSpv,
                                        jrpgmaker::shaders::kTriangleVsSpv_size},
        .pixel_shader = ShaderBytecode{jrpgmaker::shaders::kTrianglePsSpv,
                                       jrpgmaker::shaders::kTrianglePsSpv_size},
        .color_format = Format::kR8G8B8A8Unorm};
#endif

    const PipelineHandle pipeline = device->CreatePipeline(pipeline_desc);
    REQUIRE(pipeline != PipelineHandle::kInvalid);

    ICommandList* command_list = device->CreateCommandList();
    REQUIRE(command_list != nullptr);
    command_list->Begin();
    command_list->BeginRendering(target, kClearColor);
    command_list->SetPipeline(pipeline);
    command_list->Draw(3, 1);
    command_list->EndRendering();
    command_list->End();
    device->Submit(*command_list);
    device->WaitForGpuIdle();
    device->DestroyCommandList(command_list);

    const MappedTexture mapped = device->MapReadBack(target);
    REQUIRE(mapped.data != nullptr);

    for (const SamplePoint& sample : kSamples) {
        CheckPixelBlue(mapped, sample.x, sample.y, sample.expected_blue);
    }

    device->WaitForGpuIdle();
    device->DestroyPipeline(pipeline);
    device->DestroyTexture(target);
}