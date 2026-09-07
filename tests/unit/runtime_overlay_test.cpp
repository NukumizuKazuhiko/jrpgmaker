#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <variant>

#include "golden_image.hpp"
#include "jrpgmaker/render/ui_draw_adapter.hpp"
#include "jrpgmaker/rhi/command_list.hpp"
#include "jrpgmaker/rhi/device_factory.hpp"
#include "jrpgmaker/ui/runtime_overlay.hpp"
#include "jrpgmaker/ui/text.hpp"
#include "jrpgmaker/ui/text_draw.hpp"
#include "shaders_generated.hpp"
#include "ui_text_generated.hpp"

namespace {

using namespace jrpgmaker::rhi;
namespace golden = jrpgmaker::golden;

#if defined(_WIN32)
constexpr Backend kBackend = Backend::kD3D12;
#else
constexpr Backend kBackend = Backend::kVulkan;
#endif

constexpr std::uint32_t kWidth = 256;
constexpr std::uint32_t kHeight = 160;
constexpr VertexAttribute kUiAttributes[] = {
    {0, VertexAttributeFormat::kFloat3, 0, "POSITION"},
    {1, VertexAttributeFormat::kFloat2, sizeof(float) * 3u, "TEXCOORD"},
    {2, VertexAttributeFormat::kFloat4, sizeof(float) * 5u, "COLOR"},
};
constexpr VertexAttribute kBackgroundAttributes[] = {
    {0, VertexAttributeFormat::kFloat3, 0, "POSITION"},
    {1, VertexAttributeFormat::kFloat4, sizeof(float) * 3u, "COLOR"},
};

std::filesystem::path GoldenPath(const char* name) {
    return std::filesystem::path(JRPGMAKER_GOLDEN_DIR) / name;
}

std::filesystem::path AssetPath(const char* relative) {
    return std::filesystem::path(JRPGMAKER_ASSET_DIR) / relative;
}

jrpgmaker::ui::EditorTheme DialogTheme() {
    jrpgmaker::ui::EditorTheme theme;
    theme.colors.emplace("dialog_background", jrpgmaker::ui::EditorColor{18, 35, 58, 224});
    theme.semantic_tokens.emplace("dialog.background", "dialog_background");
    theme.recipes.emplace("dialog",
                          jrpgmaker::ui::EditorThemeRecipe{{{"normal", "dialog.background"}}});
    return theme;
}

} // namespace

TEST_CASE("runtime overlay turns a visible CJK dialog into sampled glyph geometry",
          "[ui][runtime][dialog][cjk]") {
    jrpgmaker::ui::Font font;
    REQUIRE(
        font.Load((std::filesystem::path(JRPGMAKER_ASSET_DIR) / "fonts" / "NotoSansCJK-Regular.ttc")
                      .string()));

    jrpgmaker::ui::DialogPresentationSnapshot dialog{
        .visible = true,
        .speaker = {},
        .text = "世界",
        .options = {},
        .selected_option = 0,
    };
    const auto overlay =
        jrpgmaker::ui::BuildRuntimeOverlayDrawList({}, dialog, {0.0f, 0.0f, 800.0f, 600.0f}, 24.0f);
    REQUIRE(overlay.ok());

    jrpgmaker::ui::GlyphAtlas atlas(256, 128, 64);
    const auto text_draw =
        jrpgmaker::ui::BuildTextDrawList(overlay.draw_list, font, {}, atlas, 24u);
    REQUIRE(text_draw.ok());
    REQUIRE(atlas.size() == 2u);

    const auto packet =
        jrpgmaker::render::BuildUiTextDrawPacket(text_draw.draw_list, {800.0f, 600.0f});
    REQUIRE(packet.ok());
    REQUIRE(packet.vertices.size() == 8u);
    REQUIRE(packet.indices.size() == 12u);
}

TEST_CASE("runtime overlay GPU batch updates in place and releases after idle",
          "[render][runtime][text][lifecycle]") {
#if defined(_WIN32)
    constexpr auto backend = jrpgmaker::rhi::Backend::kD3D12;
#else
    constexpr auto backend = jrpgmaker::rhi::Backend::kVulkan;
#endif
    const auto device = jrpgmaker::rhi::CreateDevice(backend);
    REQUIRE(device != nullptr);

    jrpgmaker::ui::Font font;
    REQUIRE(
        font.Load((std::filesystem::path(JRPGMAKER_ASSET_DIR) / "fonts" / "NotoSansCJK-Regular.ttc")
                      .string()));
    jrpgmaker::ui::GlyphAtlas atlas(256, 128, 64);
    const auto overlay = jrpgmaker::ui::BuildRuntimeOverlayDrawList(
        {}, {.visible = true, .speaker = {}, .text = "世界", .options = {}, .selected_option = 0},
        {0.0f, 0.0f, 800.0f, 600.0f}, 24.0f);
    const auto text_draw =
        jrpgmaker::ui::BuildTextDrawList(overlay.draw_list, font, {}, atlas, 24u);
    const auto packet =
        jrpgmaker::render::BuildUiTextDrawPacket(text_draw.draw_list, {800.0f, 600.0f});

    jrpgmaker::render::UiTextGpuBatch batch;
    jrpgmaker::render::UpdateUiTextGpuBatch(*device, batch, packet, atlas);
    REQUIRE(batch.index_count == packet.indices.size());
    const auto first_vertex_buffer = batch.vertex_buffer;
    const auto first_index_buffer = batch.index_buffer;
    const auto first_texture = batch.texture;
    const auto first_sampler = batch.sampler;

    jrpgmaker::render::UpdateUiTextGpuBatch(*device, batch, packet, atlas);
    REQUIRE(batch.vertex_buffer == first_vertex_buffer);
    REQUIRE(batch.index_buffer == first_index_buffer);
    REQUIRE(batch.texture == first_texture);
    REQUIRE(batch.sampler == first_sampler);

    jrpgmaker::render::UiTextDrawPacket empty_packet;
    jrpgmaker::render::UpdateUiTextGpuBatch(*device, batch, empty_packet, atlas);
    REQUIRE(batch.empty());
    REQUIRE(batch.texture != jrpgmaker::rhi::TextureHandle::kInvalid);

    device->WaitForGpuIdle();
    jrpgmaker::render::DestroyUiTextGpuBatch(*device, batch);
    REQUIRE(batch.texture == jrpgmaker::rhi::TextureHandle::kInvalid);
    REQUIRE(batch.sampler == jrpgmaker::rhi::SamplerHandle::kInvalid);
}

TEST_CASE("runtime overlay CJK dialog renders through both sampled-text and RHI golden",
          "[rhi][golden][runtime][overlay][cjk]") {
    const auto device = jrpgmaker::rhi::CreateDevice(kBackend);
    REQUIRE(device != nullptr);

    jrpgmaker::ui::Font font;
    REQUIRE(font.Load(AssetPath("fonts/NotoSansCJK-Regular.ttc").string()));
    jrpgmaker::ui::GlyphAtlas atlas(1024, 512, 256);
    const jrpgmaker::ui::DialogPresentationSnapshot dialog{
        .visible = true,
        .speaker = "ユウキ",
        .text = "你好，世界。こんにちは世界。",
        .options = {"继续", "終了"},
        .selected_option = 1,
    };
    const auto overlay = jrpgmaker::ui::BuildRuntimeOverlayDrawList(
        "", dialog, {0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight)}, 20.0f);
    REQUIRE(overlay.ok());

    jrpgmaker::ui::DrawList background;
    REQUIRE(background.Add(jrpgmaker::ui::DrawRect{{8.0f, 34.0f, 240.0f, 116.0f}, "dialog"}));
    const auto background_packet = jrpgmaker::render::BuildUiDrawPacket(
        background, DialogTheme(), {static_cast<float>(kWidth), static_cast<float>(kHeight)});
    REQUIRE(background_packet.ok());

    const auto text_draw =
        jrpgmaker::ui::BuildTextDrawList(overlay.draw_list, font, {}, atlas, 20u);
    REQUIRE(text_draw.ok());
    REQUIRE(atlas.size() >= 10u);
    const auto text_packet = jrpgmaker::render::BuildUiTextDrawPacket(
        text_draw.draw_list, {static_cast<float>(kWidth), static_cast<float>(kHeight)});
    REQUIRE(text_packet.ok());
    REQUIRE(text_packet.vertices.size() == text_draw.draw_list.size() * 4u);
    REQUIRE(text_packet.indices.size() == text_draw.draw_list.size() * 6u);

#if defined(_WIN32)
    const GraphicsPipelineDesc background_pipeline_desc{
        .vertex_shader = {jrpgmaker::shaders::kUiVsDxil, jrpgmaker::shaders::kUiVsDxil_size},
        .pixel_shader = {jrpgmaker::shaders::kUiPsDxil, jrpgmaker::shaders::kUiPsDxil_size},
        .color_format = Format::kR8G8B8A8Unorm,
        .vertex_input = {kBackgroundAttributes, 2, sizeof(jrpgmaker::render::UiVertex)},
        .blend_mode = BlendMode::kAlpha};
    const GraphicsPipelineDesc text_pipeline_desc{
        .vertex_shader = {jrpgmaker::shaders::kUiTextVsDxil,
                          jrpgmaker::shaders::kUiTextVsDxil_size},
        .pixel_shader = {jrpgmaker::shaders::kUiTextPsDxil, jrpgmaker::shaders::kUiTextPsDxil_size},
        .color_format = Format::kR8G8B8A8Unorm,
        .vertex_input = {kUiAttributes, 3, sizeof(jrpgmaker::render::UiTextVertex)},
        .sample_slot = 1,
        .blend_mode = BlendMode::kAlpha};
#else
    const GraphicsPipelineDesc background_pipeline_desc{
        .vertex_shader = {jrpgmaker::shaders::kUiVsSpv, jrpgmaker::shaders::kUiVsSpv_size},
        .pixel_shader = {jrpgmaker::shaders::kUiPsSpv, jrpgmaker::shaders::kUiPsSpv_size},
        .color_format = Format::kR8G8B8A8Unorm,
        .vertex_input = {kBackgroundAttributes, 2, sizeof(jrpgmaker::render::UiVertex)},
        .blend_mode = BlendMode::kAlpha};
    const GraphicsPipelineDesc text_pipeline_desc{
        .vertex_shader = {jrpgmaker::shaders::kUiTextVsSpv, jrpgmaker::shaders::kUiTextVsSpv_size},
        .pixel_shader = {jrpgmaker::shaders::kUiTextPsSpv, jrpgmaker::shaders::kUiTextPsSpv_size},
        .color_format = Format::kR8G8B8A8Unorm,
        .vertex_input = {kUiAttributes, 3, sizeof(jrpgmaker::render::UiTextVertex)},
        .sample_slot = 1,
        .blend_mode = BlendMode::kAlpha};
#endif
    const auto background_pipeline = device->CreatePipeline(background_pipeline_desc);
    const auto text_pipeline = device->CreatePipeline(text_pipeline_desc);
    REQUIRE(background_pipeline != PipelineHandle::kInvalid);
    REQUIRE(text_pipeline != PipelineHandle::kInvalid);

    const auto target =
        device->CreateTexture({kWidth, kHeight, Format::kR8G8B8A8Unorm,
                               TextureUsage::kRenderTarget | TextureUsage::kReadBack});
    REQUIRE(target != TextureHandle::kInvalid);
    auto background_batch = jrpgmaker::render::UploadUiDrawPacket(*device, background_packet);
    jrpgmaker::render::UiTextGpuBatch text_batch;
    jrpgmaker::render::UpdateUiTextGpuBatch(*device, text_batch, text_packet, atlas);

    std::vector<std::uint8_t> edge_class_pixels(atlas.pixels().size() * 4u, 0u);
    for (std::size_t index = 0; index < atlas.pixels().size(); ++index) {
        const auto coverage = atlas.pixels()[index];
        const auto is_partial = coverage > 0u && coverage < 255u;
        const auto is_nonzero = coverage > 0u;
        edge_class_pixels[index * 4u + 0u] = is_partial ? 255u : 0u;
        edge_class_pixels[index * 4u + 1u] = is_partial ? 255u : 0u;
        edge_class_pixels[index * 4u + 2u] = is_partial ? 255u : 0u;
        edge_class_pixels[index * 4u + 3u] = is_nonzero ? 255u : 0u;
    }
    const auto edge_class_texture = device->CreateTexture(
        {atlas.width(), atlas.height(), Format::kR8G8B8A8Unorm, TextureUsage::kSampled});
    REQUIRE(edge_class_texture != TextureHandle::kInvalid);
    device->UploadTexture(edge_class_texture, edge_class_pixels.data(),
                          static_cast<std::uint64_t>(atlas.width()) * 4u);
    const auto edge_class_target =
        device->CreateTexture({kWidth, kHeight, Format::kR8G8B8A8Unorm,
                               TextureUsage::kRenderTarget | TextureUsage::kReadBack});
    REQUIRE(edge_class_target != TextureHandle::kInvalid);
    auto* command_list = device->CreateCommandList();
    REQUIRE(command_list != nullptr);
    command_list->Begin();
    command_list->BeginRendering(target, {8.0f / 255.0f, 10.0f / 255.0f, 14.0f / 255.0f, 1.0f});
    jrpgmaker::render::RecordUiDrawPacket(*command_list, background_pipeline, background_batch);
    jrpgmaker::render::RecordUiTextDrawPacket(*command_list, text_pipeline, text_batch);
    command_list->EndRendering();
    command_list->End();
    device->Submit(*command_list);
    device->WaitForGpuIdle();
    device->DestroyCommandList(command_list);

    auto edge_class_batch = text_batch;
    edge_class_batch.texture = edge_class_texture;
    auto* edge_class_command_list = device->CreateCommandList();
    REQUIRE(edge_class_command_list != nullptr);
    edge_class_command_list->Begin();
    edge_class_command_list->BeginRendering(edge_class_target, {0.0f, 0.0f, 0.0f, 0.0f});
    jrpgmaker::render::RecordUiTextDrawPacket(*edge_class_command_list, text_pipeline,
                                              edge_class_batch);
    edge_class_command_list->EndRendering();
    edge_class_command_list->End();
    device->Submit(*edge_class_command_list);
    device->WaitForGpuIdle();
    device->DestroyCommandList(edge_class_command_list);

    const auto mapped = device->MapReadBack(target);
    REQUIRE(mapped.data != nullptr);
    const auto edge_class_mapped = device->MapReadBack(edge_class_target);
    REQUIRE(edge_class_mapped.data != nullptr);
    std::string golden_write_path;
#if defined(_WIN32)
    char* owned_output_path = nullptr;
    std::size_t output_path_size = 0;
    if (_dupenv_s(&owned_output_path, &output_path_size, "JRPGMAKER_GOLDEN_WRITE") == 0 &&
        owned_output_path != nullptr) {
        golden_write_path = owned_output_path;
        free(owned_output_path);
    }
#else
    if (const char* output_path = std::getenv("JRPGMAKER_GOLDEN_WRITE"); output_path != nullptr)
        golden_write_path = output_path;
#endif
    std::string edge_class_write_path;
#if defined(_WIN32)
    char* owned_edge_class_output_path = nullptr;
    std::size_t edge_class_output_path_size = 0;
    if (_dupenv_s(&owned_edge_class_output_path, &edge_class_output_path_size,
                  "JRPGMAKER_GOLDEN_EDGE_CLASS_WRITE") == 0 &&
        owned_edge_class_output_path != nullptr) {
        edge_class_write_path = owned_edge_class_output_path;
        free(owned_edge_class_output_path);
    }
#else
    if (const char* output_path = std::getenv("JRPGMAKER_GOLDEN_EDGE_CLASS_WRITE");
        output_path != nullptr)
        edge_class_write_path = output_path;
#endif
    REQUIRE(golden_write_path.empty() == edge_class_write_path.empty());
    if (!golden_write_path.empty())
        REQUIRE(golden_write_path != edge_class_write_path);
    if (!golden_write_path.empty()) {
        const auto readback_to_image = [](const jrpgmaker::rhi::MappedTexture& readback) {
            golden::Image image{.width = kWidth,
                                .height = kHeight,
                                .rgb = std::vector<std::uint8_t>(kWidth * kHeight * 3u)};
            for (std::uint32_t y = 0; y < kHeight; ++y) {
                const auto* source = reinterpret_cast<const std::uint8_t*>(readback.data) +
                                     static_cast<std::uint64_t>(y) * readback.row_pitch_bytes;
                auto* destination = image.rgb.data() + static_cast<std::size_t>(y) * kWidth * 3u;
                for (std::uint32_t x = 0; x < kWidth; ++x) {
                    destination[x * 3u + 0u] = source[x * 4u + 0u];
                    destination[x * 3u + 1u] = source[x * 4u + 1u];
                    destination[x * 3u + 2u] = source[x * 4u + 2u];
                }
            }
            return image;
        };
        const auto generated = readback_to_image(mapped);
        const auto generated_edge_class = readback_to_image(edge_class_mapped);
        std::string write_error;
        REQUIRE(golden::WritePpm(golden_write_path, generated, write_error));
        REQUIRE(golden::WritePpm(edge_class_write_path, generated_edge_class, write_error));
        device->WaitForGpuIdle();
        device->DestroyTexture(edge_class_target);
        device->DestroyTexture(edge_class_texture);
        jrpgmaker::render::DestroyUiTextGpuBatch(*device, text_batch);
        jrpgmaker::render::DestroyUiGpuBatch(*device, background_batch);
        device->DestroyPipeline(text_pipeline);
        device->DestroyPipeline(background_pipeline);
        device->DestroyTexture(target);
        return;
    }
    golden::Image reference;
    std::string error;
    REQUIRE(golden::ReadPpm(GoldenPath("runtime_overlay_cjk_256x160.ppm"), reference, error));
    golden::Image edge_class_reference;
    REQUIRE(golden::ReadPpm(GoldenPath("runtime_overlay_cjk_256x160_edge_class.ppm"),
                            edge_class_reference, error));
    const auto edge_class_result =
        golden::CompareRgba8(reinterpret_cast<const std::uint8_t*>(edge_class_mapped.data),
                             edge_class_mapped.row_pitch_bytes, edge_class_reference, 0);
    INFO("edge-class max channel delta: "
         << edge_class_result.max_channel_delta << ", differing pixels: "
         << edge_class_result.pixels_differing << " / " << edge_class_result.pixels_compared);
    CHECK(edge_class_result.passed);
    const auto strict = golden::CompareRgba8(reinterpret_cast<const std::uint8_t*>(mapped.data),
                                             mapped.row_pitch_bytes, reference, 0);
    const auto tolerant = golden::CompareRgba8(reinterpret_cast<const std::uint8_t*>(mapped.data),
                                               mapped.row_pitch_bytes, reference, 1);
    INFO("max channel delta: " << strict.max_channel_delta << ", differing pixels: "
                               << strict.pixels_differing << " / " << strict.pixels_compared);
    CHECK(tolerant.passed);
    std::size_t mismatches_outside_partial_coverage = 0;
    std::size_t partial_coverage_pixels = 0;
    std::size_t non_partial_coverage_pixels = 0;
    std::size_t invalid_edge_class_pixels = 0;
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        const auto* actual = reinterpret_cast<const std::uint8_t*>(mapped.data) +
                             static_cast<std::uint64_t>(y) * mapped.row_pitch_bytes;
        const auto* expected = reference.rgb.data() + static_cast<std::size_t>(y) * kWidth * 3u;
        const auto* expected_edge_class =
            edge_class_reference.rgb.data() + static_cast<std::size_t>(y) * kWidth * 3u;
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            const bool is_partial = expected_edge_class[x * 3u + 0u] == 255u &&
                                    expected_edge_class[x * 3u + 1u] == 255u &&
                                    expected_edge_class[x * 3u + 2u] == 255u;
            const bool is_non_partial = expected_edge_class[x * 3u + 0u] == 0u &&
                                        expected_edge_class[x * 3u + 1u] == 0u &&
                                        expected_edge_class[x * 3u + 2u] == 0u;
            partial_coverage_pixels += is_partial ? 1u : 0u;
            non_partial_coverage_pixels += is_non_partial ? 1u : 0u;
            invalid_edge_class_pixels += !is_partial && !is_non_partial ? 1u : 0u;
            const bool differs = actual[x * 4u + 0u] != expected[x * 3u + 0u] ||
                                 actual[x * 4u + 1u] != expected[x * 3u + 1u] ||
                                 actual[x * 4u + 2u] != expected[x * 3u + 2u];
            if (differs && !is_partial)
                ++mismatches_outside_partial_coverage;
        }
    }
    INFO("partial glyph coverage pixels: " << partial_coverage_pixels);
    INFO("non-partial glyph coverage pixels: " << non_partial_coverage_pixels);
    INFO("invalid edge-class pixels: " << invalid_edge_class_pixels);
    CHECK(partial_coverage_pixels > 0u);
    CHECK(non_partial_coverage_pixels > 0u);
    CHECK(invalid_edge_class_pixels == 0u);
    INFO("mismatches outside partial glyph coverage: " << mismatches_outside_partial_coverage);
    CHECK(mismatches_outside_partial_coverage == 0u);

    device->WaitForGpuIdle();
    device->DestroyTexture(edge_class_target);
    device->DestroyTexture(edge_class_texture);
    jrpgmaker::render::DestroyUiTextGpuBatch(*device, text_batch);
    jrpgmaker::render::DestroyUiGpuBatch(*device, background_batch);
    device->DestroyPipeline(text_pipeline);
    device->DestroyPipeline(background_pipeline);
    device->DestroyTexture(target);
}
