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

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <jrpgmaker/assetimport/asset_import.hpp>
#include <jrpgmaker/core/scene.hpp>
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
// target. Returns false on failure.
bool RenderInto(
    std::vector<std::uint8_t>& rgba,
    const std::vector<std::pair<std::vector<float>, std::vector<std::uint32_t>>>& meshes_to_draw) {
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

    const GraphicsPipelineDesc pipeline_desc = MakeTrianglePipelineDesc();
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
    return RenderInto(rgba, {{mesh->positions, mesh->indices}});
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
    return RenderInto(rgba, meshes_to_draw);
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

int main(int argc, char** argv) {
    // Usage:
    //   goldenimage generate <out.ppm> [gltf_path]
    //   goldenimage compare <ref.ppm> [tolerance] [gltf_path]
    //   goldenimage generate-scene <out.ppm> <gltf_path>
    //   goldenimage compare-scene <ref.ppm> <gltf_path> [tolerance]
    // The single-mesh glTF path defaults to the committed triangle asset
    // (assets/art/meshes/triangle.gltf), whose geometry matches the golden
    // reference.
    const std::filesystem::path default_gltf =
        std::filesystem::path(JRPGMAKER_ASSET_DIR) / "art" / "meshes" / "triangle.gltf";

    if (argc < 3) {
        std::cerr << "usage:\n"
                  << "  goldenimage generate <out.ppm> [gltf_path]\n"
                  << "  goldenimage compare <ref.ppm> [tolerance] [gltf_path]\n"
                  << "  goldenimage generate-scene <out.ppm> <gltf_path>\n"
                  << "  goldenimage compare-scene <ref.ppm> <gltf_path> [tolerance]\n";
        return 2;
    }

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

    std::cerr << "unknown command '" << command << "'\n";
    return 2;
}