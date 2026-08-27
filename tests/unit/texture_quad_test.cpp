#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>

#include <glm/glm.hpp>

#include "jrpgmaker/assetimport/asset_import.hpp"
#include "jrpgmaker/render/texture_resource.hpp"
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

TEST_CASE("texture resource service validates and registers RGBA8 resources", "[render][p8]") {
    const std::unique_ptr<IDevice> device = CreateDevice(kBackend);
    REQUIRE(device != nullptr);
    jrpgmaker::render::TextureResourceService resources(*device);
    const std::array<std::uint8_t, 4> pixels = {65u, 66u, 67u, 255u};

    const auto empty_id =
        resources.Register({.id = "", .width = 1, .height = 1, .rgba8 = pixels, .sampler = {}});
    REQUIRE_FALSE(empty_id);
    const auto wrong_size = resources.Register({.id = "bad",
                                                .width = 2,
                                                .height = 1,
                                                .rgba8 = std::span<const std::uint8_t>(pixels),
                                                .sampler = {}});
    REQUIRE_FALSE(wrong_size);

    const auto registered = resources.Register(
        {.id = "character.albedo", .width = 1, .height = 1, .rgba8 = pixels, .sampler = {}});
    REQUIRE(registered);
    REQUIRE(resources.size() == 1u);
    REQUIRE(resources.Find("character.albedo").has_value());
    REQUIRE_FALSE(resources.Find("missing").has_value());
    REQUIRE_FALSE(resources.Register(
        {.id = "character.albedo", .width = 1, .height = 1, .rgba8 = pixels, .sampler = {}}));
    const auto imported = jrpgmaker::assetimport::LoadGltfScene(
        std::filesystem::path(JRPGMAKER_ASSET_DIR) / "art/meshes/triangle.gltf");
    REQUIRE(imported.has_value());
    REQUIRE(imported->textures.size() == 1u);
    const auto& imported_texture = imported->textures.front();
    REQUIRE(resources.Register({.id = "triangle.albedo",
                                .width = imported_texture.width,
                                .height = imported_texture.height,
                                .rgba8 = imported_texture.rgba8,
                                .sampler = {}}));
    REQUIRE(resources.Find("triangle.albedo").has_value());
    REQUIRE(resources.size() == 2u);
    device->WaitForGpuIdle();
}

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

#ifndef JRPGMAKER_GOLDEN_DIR
#error "JRPGMAKER_GOLDEN_DIR must be defined by the build"
#endif

std::filesystem::path GoldenPath(const char* name) {
    return std::filesystem::path(JRPGMAKER_GOLDEN_DIR) / name;
}

const std::vector<float>& QuadVertices() {
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

// A 2x2 four-color texture (top-left red, top-right green, bottom-left blue,
// bottom-right white) sampled across a fullscreen quad with a nearest/clamp
// sampler. The 64x64 target resolves to four sharp 32x32 color quadrants. The
// reference is lavapipe-generated (tests/golden/texture_quad_64x64.ppm); this
// also exercises the DEBT-029 texture pipeline end to end on both backends.
TEST_CASE("sampled texture quad matches the committed golden reference", "[rhi][golden][texture]") {
    const std::unique_ptr<IDevice> device = CreateDevice(kBackend);
    REQUIRE(device != nullptr);

    const TextureHandle target = device->CreateTexture(
        TextureDesc{.width = kWidth,
                    .height = kHeight,
                    .format = Format::kR8G8B8A8Unorm,
                    .usage = TextureUsage::kRenderTarget | TextureUsage::kReadBack});
    REQUIRE(target != TextureHandle::kInvalid);

    const auto pixels = MakeQuadTexture();
    jrpgmaker::render::TextureResourceService texture_resources(*device);
    REQUIRE(texture_resources.Register(
        {.id = "quad.texture",
         .width = kQuadTextureSize,
         .height = kQuadTextureSize,
         .rgba8 = pixels,
         .sampler = {.filter = SamplerFilter::kNearest, .address = SamplerAddress::kClamp}}));

#if defined(_WIN32)
    const GraphicsPipelineDesc pipeline_desc{
        .vertex_shader = ShaderBytecode{jrpgmaker::shaders::kTexturedVsDxil,
                                        jrpgmaker::shaders::kTexturedVsDxil_size},
        .pixel_shader = ShaderBytecode{jrpgmaker::shaders::kTexturedPsDxil,
                                       jrpgmaker::shaders::kTexturedPsDxil_size},
        .color_format = Format::kR8G8B8A8Unorm,
        .vertex_input =
            VertexInputLayout{
                .attributes = kQuadAttributes,
                .attribute_count = 2,
                .stride_bytes = kQuadStride,
            },
        .sample_slot = 1,
    };
#else
    const GraphicsPipelineDesc pipeline_desc{
        .vertex_shader = ShaderBytecode{jrpgmaker::shaders::kTexturedVsSpv,
                                        jrpgmaker::shaders::kTexturedVsSpv_size},
        .pixel_shader = ShaderBytecode{jrpgmaker::shaders::kTexturedPsSpv,
                                       jrpgmaker::shaders::kTexturedPsSpv_size},
        .color_format = Format::kR8G8B8A8Unorm,
        .vertex_input =
            VertexInputLayout{
                .attributes = kQuadAttributes,
                .attribute_count = 2,
                .stride_bytes = kQuadStride,
            },
        .sample_slot = 1,
    };
#endif

    const PipelineHandle pipeline = device->CreatePipeline(pipeline_desc);
    REQUIRE(pipeline != PipelineHandle::kInvalid);

    const BufferHandle vertex_buffer = device->CreateBuffer(
        BufferDesc{.size_bytes = static_cast<std::uint64_t>(QuadVertices().size()) * sizeof(float),
                   .usage = BufferUsage::kVertex});
    REQUIRE(vertex_buffer != BufferHandle::kInvalid);
    device->MapWrite(vertex_buffer, QuadVertices().data(),
                     static_cast<std::uint64_t>(QuadVertices().size()) * sizeof(float));
    constexpr std::array<std::uint32_t, 6> kQuadIndices = {0, 1, 2, 3, 4, 5};
    const BufferHandle index_buffer = device->CreateBuffer(
        BufferDesc{.size_bytes = sizeof(kQuadIndices), .usage = BufferUsage::kIndex});
    REQUIRE(index_buffer != BufferHandle::kInvalid);
    device->MapWrite(index_buffer, kQuadIndices.data(), sizeof(kQuadIndices));

    ICommandList* command_list = device->CreateCommandList();
    REQUIRE(command_list != nullptr);
    command_list->Begin();
    const jrpgmaker::render::RenderPlan plan{
        .view_projection = glm::mat4(1.0f),
        .passes = {{.id = "textured",
                    .clear_color = {kClearColor.r, kClearColor.g, kClearColor.b, kClearColor.a},
                    .clear_target = true,
                    .pipeline = "textured",
                    .draws = {{.mesh = "quad",
                               .material = "quad",
                               .world = glm::mat4(1.0f),
                               .material_parameters = {},
                               .sampled_texture = "quad.texture"}}}}};
    const jrpgmaker::render::RenderPlanResolver resolver{
        .resolve_pipeline = [pipeline](const auto&) { return std::optional{pipeline}; },
        .resolve_mesh =
            [vertex_buffer, index_buffer](const auto&) {
                return std::optional{
                    jrpgmaker::render::RenderMeshBinding{.vertex_buffer = vertex_buffer,
                                                         .index_buffer = index_buffer,
                                                         .stride_bytes = kQuadStride,
                                                         .index_count = 6,
                                                         .indices_are_32_bit = true}};
            },
        .resolve_sampled_texture =
            [&texture_resources](const auto& draw) {
                return texture_resources.Find(draw.sampled_texture);
            },
        .validate_material = {},
        .bind_draw_resources = {}};
    const auto recorded = jrpgmaker::render::RenderPlanExecutor::Record(
        plan, target, *command_list, resolver, jrpgmaker::render::RenderResourceBudget{});
    REQUIRE(recorded.ok);

    command_list->BeginRendering(target, kClearColor);
    command_list->SetPipeline(pipeline);
    command_list->SetVertexBuffer(vertex_buffer, kQuadStride);
    command_list->SetIndexBuffer(index_buffer, true);
    REQUIRE_THROWS_AS(command_list->DrawIndexed(6, 1), std::runtime_error);
    const auto sampled = texture_resources.Find("quad.texture");
    REQUIRE(sampled.has_value());
    command_list->SetSampledTexture(sampled->texture, sampled->sampler);
    command_list->Draw(6, 1);
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
    const std::filesystem::path reference_path = GoldenPath("texture_quad_64x64.ppm");
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

TEST_CASE("SetSampledTexture rejects a pipeline without a declared sample slot",
          "[rhi][contract]") {
    const std::unique_ptr<IDevice> device = CreateDevice(kBackend);
    REQUIRE(device != nullptr);

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
    const auto pixels = MakeQuadTexture();
    device->UploadTexture(texture, pixels.data(),
                          static_cast<std::uint64_t>(kQuadTextureSize) * 4u);
    const SamplerHandle sampler = device->CreateSampler(
        SamplerDesc{.filter = SamplerFilter::kNearest, .address = SamplerAddress::kClamp});

    // A pipeline with the default sample_slot == 0 (no sampling declared): the
    // contract forbids binding a sampled texture to it.
#if defined(_WIN32)
    const GraphicsPipelineDesc pipeline_desc{
        .vertex_shader = ShaderBytecode{jrpgmaker::shaders::kTriangleVsDxil,
                                        jrpgmaker::shaders::kTriangleVsDxil_size},
        .pixel_shader = ShaderBytecode{jrpgmaker::shaders::kTrianglePsDxil,
                                       jrpgmaker::shaders::kTrianglePsDxil_size},
        .color_format = Format::kR8G8B8A8Unorm,
        .vertex_input = VertexInputLayout{
            .attributes = kQuadAttributes,
            .attribute_count = 2,
            .stride_bytes = kQuadStride,
        }};
#else
    const GraphicsPipelineDesc pipeline_desc{
        .vertex_shader = ShaderBytecode{jrpgmaker::shaders::kTriangleVsSpv,
                                        jrpgmaker::shaders::kTriangleVsSpv_size},
        .pixel_shader = ShaderBytecode{jrpgmaker::shaders::kTrianglePsSpv,
                                       jrpgmaker::shaders::kTrianglePsSpv_size},
        .color_format = Format::kR8G8B8A8Unorm,
        .vertex_input = VertexInputLayout{
            .attributes = kQuadAttributes,
            .attribute_count = 2,
            .stride_bytes = kQuadStride,
        }};
#endif

    const PipelineHandle pipeline = device->CreatePipeline(pipeline_desc);
    REQUIRE(pipeline != PipelineHandle::kInvalid);

    ICommandList* command_list = device->CreateCommandList();
    command_list->Begin();
    command_list->SetPipeline(pipeline);
    REQUIRE_THROWS_AS(command_list->SetSampledTexture(texture, sampler), std::runtime_error);
    command_list->End();

    device->WaitForGpuIdle();
    device->DestroyCommandList(command_list);
    device->DestroySampler(sampler);
    device->DestroyPipeline(pipeline);
    device->DestroyTexture(texture);
    device->DestroyTexture(target);
}

} // namespace
