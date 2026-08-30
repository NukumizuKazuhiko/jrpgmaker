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

    const auto mapped = device->MapReadBack(target);
    REQUIRE(mapped.data != nullptr);
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
    if (!golden_write_path.empty()) {
        golden::Image generated{.width = kWidth,
                                .height = kHeight,
                                .rgb = std::vector<std::uint8_t>(kWidth * kHeight * 3u)};
        for (std::uint32_t y = 0; y < kHeight; ++y) {
            const auto* source = reinterpret_cast<const std::uint8_t*>(mapped.data) +
                                 static_cast<std::uint64_t>(y) * mapped.row_pitch_bytes;
            auto* destination = generated.rgb.data() + static_cast<std::size_t>(y) * kWidth * 3u;
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                destination[x * 3u + 0u] = source[x * 4u + 0u];
                destination[x * 3u + 1u] = source[x * 4u + 1u];
                destination[x * 3u + 2u] = source[x * 4u + 2u];
            }
        }
        std::string write_error;
        REQUIRE(golden::WritePpm(golden_write_path, generated, write_error));
        device->WaitForGpuIdle();
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
    const auto result = golden::CompareRgba8(reinterpret_cast<const std::uint8_t*>(mapped.data),
                                             mapped.row_pitch_bytes, reference, 0);
    INFO("max channel delta: " << result.max_channel_delta << ", differing pixels: "
                               << result.pixels_differing << " / " << result.pixels_compared);
    CHECK(result.passed);

    device->WaitForGpuIdle();
    jrpgmaker::render::DestroyUiTextGpuBatch(*device, text_batch);
    jrpgmaker::render::DestroyUiGpuBatch(*device, background_batch);
    device->DestroyPipeline(text_pipeline);
    device->DestroyPipeline(background_pipeline);
    device->DestroyTexture(target);
}
