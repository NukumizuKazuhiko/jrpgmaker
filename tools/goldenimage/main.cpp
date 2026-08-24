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
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <jrpgmaker/assetimport/asset_import.hpp>
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
    if (pipeline == PipelineHandle::kInvalid) {
        std::cerr << "failed to create pipeline\n";
        device->DestroyTexture(target);
        return false;
    }

    const BufferHandle vertex_buffer = device->CreateBuffer(
        BufferDesc{.size_bytes = static_cast<std::uint64_t>(mesh->positions.size()) * sizeof(float),
                   .usage = BufferUsage::kVertex});
    if (vertex_buffer == BufferHandle::kInvalid) {
        std::cerr << "failed to create vertex buffer\n";
        device->DestroyPipeline(pipeline);
        device->DestroyTexture(target);
        return false;
    }
    device->MapWrite(vertex_buffer, mesh->positions.data(),
                     static_cast<std::uint64_t>(mesh->positions.size()) * sizeof(float));

    const BufferHandle index_buffer = device->CreateBuffer(BufferDesc{
        .size_bytes = static_cast<std::uint64_t>(mesh->indices.size()) * sizeof(std::uint32_t),
        .usage = BufferUsage::kIndex});
    if (index_buffer == BufferHandle::kInvalid) {
        std::cerr << "failed to create index buffer\n";
        device->DestroyBuffer(vertex_buffer);
        device->DestroyPipeline(pipeline);
        device->DestroyTexture(target);
        return false;
    }
    device->MapWrite(index_buffer, mesh->indices.data(),
                     static_cast<std::uint64_t>(mesh->indices.size()) * sizeof(std::uint32_t));

    ICommandList* command_list = device->CreateCommandList();
    if (command_list == nullptr) {
        std::cerr << "failed to create command list\n";
        device->DestroyBuffer(index_buffer);
        device->DestroyBuffer(vertex_buffer);
        device->DestroyPipeline(pipeline);
        device->DestroyTexture(target);
        return false;
    }
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

int Generate(const std::string& output_path, const std::filesystem::path& gltf_path) {
    std::vector<std::uint8_t> rgba;
    if (!RenderTriangle(rgba, gltf_path)) {
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

int Compare(const std::string& reference_path, int tolerance,
            const std::filesystem::path& gltf_path) {
    golden::Image reference;
    std::string error;
    if (!golden::ReadPpm(reference_path, reference, error)) {
        std::cerr << error << '\n';
        return 1;
    }

    std::vector<std::uint8_t> rgba;
    if (!RenderTriangle(rgba, gltf_path)) {
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

int main(int argc, char** argv) {
    // Usage:
    //   goldenimage generate <out.ppm> [gltf_path]
    //   goldenimage compare <ref.ppm> [tolerance] [gltf_path]
    // The glTF path defaults to the committed triangle asset
    // (assets/art/meshes/triangle.gltf), whose geometry matches the golden
    // reference.
    const std::filesystem::path default_gltf =
        std::filesystem::path(JRPGMAKER_ASSET_DIR) / "art" / "meshes" / "triangle.gltf";

    if (argc < 3) {
        std::cerr << "usage: goldenimage generate <out.ppm> [gltf_path]\n"
                  << "       goldenimage compare <ref.ppm> [tolerance] [gltf_path]\n";
        return 2;
    }

    const std::string command = argv[1];
    if (command == "generate") {
        const std::filesystem::path gltf_path = argc >= 4 ? argv[3] : default_gltf;
        return Generate(argv[2], gltf_path);
    }
    if (command == "compare") {
        int tolerance = 1;
        const std::filesystem::path gltf_path = argc >= 5 ? argv[4] : default_gltf;
        if (argc >= 4) {
            tolerance = std::atoi(argv[3]);
        }
        return Compare(argv[2], tolerance, gltf_path);
    }

    std::cerr << "unknown command '" << command << "'\n";
    return 2;
}