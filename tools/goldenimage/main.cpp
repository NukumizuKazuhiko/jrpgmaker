// goldenimage CLI: render a scene off-screen and either write a golden reference
// (generate) or compare the render against a reference (compare).
//
// Usage:
//   goldenimage generate <out.ppm>
//   goldenimage compare <ref.ppm> [tolerance]
//
// The render is the P1 triangle (64x64, R8G8B8A8 render target). The reference
// images under tests/golden/ are lavapipe-generated, read-only build artifacts:
// regenerate them with `generate` and commit the diff.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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

// Renders the triangle into tightly-packed RGBA8 (row pitch == width*4).
bool RenderTriangle(std::vector<std::uint8_t>& rgba) {
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
    if (pipeline == PipelineHandle::kInvalid) {
        std::cerr << "failed to create pipeline\n";
        device->DestroyTexture(target);
        return false;
    }

    ICommandList* command_list = device->CreateCommandList();
    if (command_list == nullptr) {
        std::cerr << "failed to create command list\n";
        device->DestroyPipeline(pipeline);
        device->DestroyTexture(target);
        return false;
    }
    command_list->Begin();
    command_list->BeginRendering(target, kClearColor);
    command_list->SetPipeline(pipeline);
    command_list->Draw(3, 1);
    command_list->EndRendering();
    command_list->End();
    device->Submit(*command_list);
    device->WaitForGpuIdle();
    device->DestroyCommandList(command_list);
    device->DestroyPipeline(pipeline);

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

int Generate(const std::string& output_path) {
    std::vector<std::uint8_t> rgba;
    if (!RenderTriangle(rgba)) {
        return 1;
    }

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

int Compare(const std::string& reference_path, int tolerance) {
    golden::Image reference;
    std::string error;
    if (!golden::ReadPpm(reference_path, reference, error)) {
        std::cerr << error << '\n';
        return 1;
    }

    std::vector<std::uint8_t> rgba;
    if (!RenderTriangle(rgba)) {
        return 1;
    }

    const golden::CompareResult result =
        golden::CompareRgba8(rgba.data(), static_cast<std::uint64_t>(kWidth) * 4u, reference,
                             tolerance);
    std::cout << "compared " << result.pixels_compared << " pixels against "
              << reference_path << " (tolerance " << tolerance << "): "
              << result.pixels_differing << " differing, max channel delta "
              << result.max_channel_delta << '\n';
    return result.passed ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: goldenimage generate <out.ppm>\n"
                  << "       goldenimage compare <ref.ppm> [tolerance]\n";
        return 2;
    }

    const std::string command = argv[1];
    if (command == "generate") {
        return Generate(argv[2]);
    }
    if (command == "compare") {
        int tolerance = 1;
        if (argc >= 4) {
            tolerance = std::atoi(argv[3]);
        }
        return Compare(argv[2], tolerance);
    }

    std::cerr << "unknown command '" << command << "'\n";
    return 2;
}