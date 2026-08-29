#include "jrpgmaker/render/ui_draw_adapter.hpp"

#include <cmath>
#include <limits>
#include <vector>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace jrpgmaker::render {
namespace {

constexpr std::size_t kVerticesPerRect = 4;
constexpr std::size_t kIndicesPerRect = 6;

bool ValidRect(const ui::Rect& rect) {
    return rect.x >= 0.0f && rect.y >= 0.0f && rect.width >= 0.0f &&
           rect.height >= 0.0f && std::isfinite(rect.x) && std::isfinite(rect.y) &&
           std::isfinite(rect.width) && std::isfinite(rect.height);
}

std::optional<ui::Rect> ClipToViewport(const ui::Rect& rect, const UiViewport& viewport) {
    const float left = std::max(0.0f, rect.x);
    const float top = std::max(0.0f, rect.y);
    const float right = std::min(viewport.width, rect.x + rect.width);
    const float bottom = std::min(viewport.height, rect.y + rect.height);
    if (right <= left || bottom <= top)
        return std::nullopt;
    return ui::Rect{left, top, right - left, bottom - top};
}

glm::vec4 ToColor(const ui::EditorColor& color) {
    constexpr float kByteScale = 1.0f / 255.0f;
    return {static_cast<float>(color.r) * kByteScale, static_cast<float>(color.g) * kByteScale,
            static_cast<float>(color.b) * kByteScale, static_cast<float>(color.a) * kByteScale};
}

void AddDiagnostic(UiDrawPacket& packet, std::string_view code, std::size_t index) {
    packet.diagnostics.push_back({std::string(code), index});
}

} // namespace

UiDrawPacket BuildUiDrawPacket(const ui::DrawList& draw_list, const ui::EditorTheme& theme,
                               UiViewport viewport) {
    UiDrawPacket packet;
    if (!(viewport.width > 0.0f) || !(viewport.height > 0.0f) ||
        !std::isfinite(viewport.width) || !std::isfinite(viewport.height)) {
        AddDiagnostic(packet, "ui.viewport.invalid", 0);
        return packet;
    }

    packet.vertices.reserve(draw_list.size() * kVerticesPerRect);
    packet.indices.reserve(draw_list.size() * kIndicesPerRect);
    packet.text_keys.reserve(draw_list.size());

    for (std::size_t primitive_index = 0; primitive_index < draw_list.primitives().size();
         ++primitive_index) {
        const auto& primitive = draw_list.primitives()[primitive_index];
        if (const auto* text = std::get_if<ui::DrawText>(&primitive)) {
            if (!ValidRect(text->rect))
                AddDiagnostic(packet, "ui.text.rect_invalid", primitive_index);
            if (text->text_key.empty())
                AddDiagnostic(packet, "ui.text.key_required", primitive_index);
            packet.text_keys.push_back(text->text_key);
            continue;
        }

        const auto* rect = std::get_if<ui::DrawRect>(&primitive);
        if (rect == nullptr)
            continue;
        if (!ValidRect(rect->rect)) {
            AddDiagnostic(packet, "ui.rect.invalid", primitive_index);
            continue;
        }
        const auto clipped = ClipToViewport(rect->rect, viewport);
        if (!clipped) {
            AddDiagnostic(packet, "ui.rect.out_of_viewport", primitive_index);
            continue;
        }

        const auto recipe = theme.recipes.find(rect->recipe);
        if (recipe == theme.recipes.end()) {
            AddDiagnostic(packet, "ui.recipe.unknown", primitive_index);
            continue;
        }
        const auto state = recipe->second.states.find(rect->state);
        if (state == recipe->second.states.end()) {
            AddDiagnostic(packet, "ui.recipe.state_unknown", primitive_index);
            continue;
        }
        const auto semantic = theme.semantic_tokens.find(state->second);
        if (semantic == theme.semantic_tokens.end()) {
            AddDiagnostic(packet, "ui.semantic_token.unknown", primitive_index);
            continue;
        }
        const auto color = theme.colors.find(semantic->second);
        if (color == theme.colors.end()) {
            AddDiagnostic(packet, "ui.color.unknown", primitive_index);
            continue;
        }

        const float left = 2.0f * clipped->x / viewport.width - 1.0f;
        const float right = 2.0f * (clipped->x + clipped->width) / viewport.width - 1.0f;
        const float top = 1.0f - 2.0f * clipped->y / viewport.height;
        const float bottom = 1.0f - 2.0f * (clipped->y + clipped->height) / viewport.height;
        const auto rgba = ToColor(color->second);
        const auto base = static_cast<std::uint32_t>(packet.vertices.size());
        packet.vertices.push_back({{left, top, 0.0f}, rgba});
        packet.vertices.push_back({{right, top, 0.0f}, rgba});
        packet.vertices.push_back({{right, bottom, 0.0f}, rgba});
        packet.vertices.push_back({{left, bottom, 0.0f}, rgba});
        packet.indices.insert(packet.indices.end(), {base, base + 1, base + 2,
                                                      base, base + 2, base + 3});
    }
    return packet;
}

UiTextDrawPacket BuildUiTextDrawPacket(const ui::DrawList& draw_list, UiViewport viewport) {
    UiTextDrawPacket packet;
    if (!(viewport.width > 0.0f) || !(viewport.height > 0.0f) ||
        !std::isfinite(viewport.width) || !std::isfinite(viewport.height)) {
        packet.diagnostics.push_back({"ui.viewport.invalid", 0});
        return packet;
    }
    for (std::size_t index = 0; index < draw_list.primitives().size(); ++index) {
        const auto* glyph = std::get_if<ui::DrawGlyph>(&draw_list.primitives()[index]);
        if (glyph == nullptr)
            continue;
        if (!ValidRect(glyph->rect) || glyph->rect.x + glyph->rect.width > viewport.width ||
            glyph->rect.y + glyph->rect.height > viewport.height || !ValidRect(glyph->uv) ||
            glyph->uv.x + glyph->uv.width > 1.0f || glyph->uv.y + glyph->uv.height > 1.0f) {
            packet.diagnostics.push_back({"ui.glyph.rect_invalid", index});
            continue;
        }
        const float left = 2.0f * glyph->rect.x / viewport.width - 1.0f;
        const float right = 2.0f * (glyph->rect.x + glyph->rect.width) / viewport.width - 1.0f;
        const float top = 1.0f - 2.0f * glyph->rect.y / viewport.height;
        const float bottom =
            1.0f - 2.0f * (glyph->rect.y + glyph->rect.height) / viewport.height;
        const auto base = static_cast<std::uint32_t>(packet.vertices.size());
        packet.vertices.push_back({{left, top, 0.0f}, {glyph->uv.x, glyph->uv.y}, glyph->color});
        packet.vertices.push_back({{right, top, 0.0f},
                                   {glyph->uv.x + glyph->uv.width, glyph->uv.y}, glyph->color});
        packet.vertices.push_back({{right, bottom, 0.0f},
                                   {glyph->uv.x + glyph->uv.width,
                                    glyph->uv.y + glyph->uv.height},
                                   glyph->color});
        packet.vertices.push_back({{left, bottom, 0.0f},
                                   {glyph->uv.x, glyph->uv.y + glyph->uv.height}, glyph->color});
        packet.indices.insert(packet.indices.end(), {base, base + 1, base + 2,
                                                     base, base + 2, base + 3});
    }
    return packet;
}

UiTextGpuBatch UploadUiTextDrawPacket(rhi::IDevice& device, const UiTextDrawPacket& packet,
                                      const ui::GlyphAtlas& atlas) {
    if (!packet.ok())
        throw std::invalid_argument("ui text draw packet contains diagnostics");
    if (packet.vertices.empty() || packet.indices.empty())
        return {};
    UiTextGpuBatch batch;
    try {
        batch.texture = device.CreateTexture({atlas.width(), atlas.height(),
                                               rhi::Format::kR8G8B8A8Unorm,
                                               rhi::TextureUsage::kSampled});
        if (batch.texture == rhi::TextureHandle::kInvalid)
            throw std::runtime_error("ui glyph texture creation failed");
        std::vector<std::uint8_t> rgba(atlas.pixels().size() * 4u, 255u);
        for (std::size_t index = 0; index < atlas.pixels().size(); ++index)
            rgba[index * 4u + 3u] = atlas.pixels()[index];
        device.UploadTexture(batch.texture, rgba.data(),
                             static_cast<std::uint64_t>(atlas.width()) * 4u);
        batch.sampler = device.CreateSampler({rhi::SamplerFilter::kNearest,
                                              rhi::SamplerAddress::kClamp});
        if (batch.sampler == rhi::SamplerHandle::kInvalid)
            throw std::runtime_error("ui glyph sampler creation failed");
        batch.vertex_buffer = device.CreateBuffer({
            static_cast<std::uint64_t>(packet.vertices.size() * sizeof(UiTextVertex)),
            rhi::BufferUsage::kVertex});
        if (batch.vertex_buffer == rhi::BufferHandle::kInvalid)
            throw std::runtime_error("ui text vertex buffer creation failed");
        device.MapWrite(batch.vertex_buffer, packet.vertices.data(),
                        static_cast<std::uint64_t>(packet.vertices.size() * sizeof(UiTextVertex)));
        batch.index_buffer = device.CreateBuffer({
            static_cast<std::uint64_t>(packet.indices.size() * sizeof(std::uint32_t)),
            rhi::BufferUsage::kIndex});
        if (batch.index_buffer == rhi::BufferHandle::kInvalid)
            throw std::runtime_error("ui text index buffer creation failed");
        device.MapWrite(batch.index_buffer, packet.indices.data(),
                        static_cast<std::uint64_t>(packet.indices.size() * sizeof(std::uint32_t)));
        batch.index_count = static_cast<std::uint32_t>(packet.indices.size());
        return batch;
    } catch (...) {
        DestroyUiTextGpuBatch(device, batch);
        throw;
    }
}

void RecordUiTextDrawPacket(rhi::ICommandList& command_list, rhi::PipelineHandle pipeline,
                            const UiTextGpuBatch& batch) {
    if (batch.empty())
        return;
    if (pipeline == rhi::PipelineHandle::kInvalid ||
        batch.vertex_buffer == rhi::BufferHandle::kInvalid ||
        batch.index_buffer == rhi::BufferHandle::kInvalid ||
        batch.texture == rhi::TextureHandle::kInvalid ||
        batch.sampler == rhi::SamplerHandle::kInvalid)
        throw std::invalid_argument("ui text draw batch has invalid handles");
    command_list.SetPipeline(pipeline);
    command_list.SetSampledTexture(batch.texture, batch.sampler);
    command_list.SetVertexBuffer(batch.vertex_buffer, sizeof(UiTextVertex));
    command_list.SetIndexBuffer(batch.index_buffer, true);
    command_list.DrawIndexed(batch.index_count, 1);
}

void DestroyUiTextGpuBatch(rhi::IDevice& device, UiTextGpuBatch& batch) {
    if (batch.index_buffer != rhi::BufferHandle::kInvalid) {
        device.DestroyBuffer(batch.index_buffer);
        batch.index_buffer = rhi::BufferHandle::kInvalid;
    }
    if (batch.vertex_buffer != rhi::BufferHandle::kInvalid) {
        device.DestroyBuffer(batch.vertex_buffer);
        batch.vertex_buffer = rhi::BufferHandle::kInvalid;
    }
    if (batch.sampler != rhi::SamplerHandle::kInvalid) {
        device.DestroySampler(batch.sampler);
        batch.sampler = rhi::SamplerHandle::kInvalid;
    }
    if (batch.texture != rhi::TextureHandle::kInvalid) {
        device.DestroyTexture(batch.texture);
        batch.texture = rhi::TextureHandle::kInvalid;
    }
    batch.index_count = 0;
}

UiGpuBatch UploadUiDrawPacket(rhi::IDevice& device, const UiDrawPacket& packet) {
    if (!packet.ok())
        throw std::invalid_argument("ui draw packet contains diagnostics");
    if (packet.vertices.empty() || packet.indices.empty())
        return {};
    if (packet.indices.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("ui draw packet index count exceeds uint32");

    UiGpuBatch batch;
    try {
        batch.vertex_buffer = device.CreateBuffer(rhi::BufferDesc{
            .size_bytes = static_cast<std::uint64_t>(packet.vertices.size() * sizeof(UiVertex)),
            .usage = rhi::BufferUsage::kVertex});
        if (batch.vertex_buffer == rhi::BufferHandle::kInvalid)
            throw std::runtime_error("ui vertex buffer creation failed");
        device.MapWrite(batch.vertex_buffer, packet.vertices.data(),
                        static_cast<std::uint64_t>(packet.vertices.size() * sizeof(UiVertex)));

        batch.index_buffer = device.CreateBuffer(rhi::BufferDesc{
            .size_bytes = static_cast<std::uint64_t>(packet.indices.size() * sizeof(std::uint32_t)),
            .usage = rhi::BufferUsage::kIndex});
        if (batch.index_buffer == rhi::BufferHandle::kInvalid)
            throw std::runtime_error("ui index buffer creation failed");
        device.MapWrite(batch.index_buffer, packet.indices.data(),
                        static_cast<std::uint64_t>(packet.indices.size() * sizeof(std::uint32_t)));
        batch.index_count = static_cast<std::uint32_t>(packet.indices.size());
        return batch;
    } catch (...) {
        DestroyUiGpuBatch(device, batch);
        throw;
    }
}

void RecordUiDrawPacket(rhi::ICommandList& command_list, rhi::PipelineHandle pipeline,
                        const UiGpuBatch& batch) {
    if (batch.empty())
        return;
    if (pipeline == rhi::PipelineHandle::kInvalid ||
        batch.vertex_buffer == rhi::BufferHandle::kInvalid ||
        batch.index_buffer == rhi::BufferHandle::kInvalid)
        throw std::invalid_argument("ui draw batch has invalid handles");
    command_list.SetPipeline(pipeline);
    command_list.SetVertexBuffer(batch.vertex_buffer, sizeof(UiVertex));
    command_list.SetIndexBuffer(batch.index_buffer, true);
    command_list.DrawIndexed(batch.index_count, 1);
}

void DestroyUiGpuBatch(rhi::IDevice& device, UiGpuBatch& batch) {
    if (batch.index_buffer != rhi::BufferHandle::kInvalid) {
        device.DestroyBuffer(batch.index_buffer);
        batch.index_buffer = rhi::BufferHandle::kInvalid;
    }
    if (batch.vertex_buffer != rhi::BufferHandle::kInvalid) {
        device.DestroyBuffer(batch.vertex_buffer);
        batch.vertex_buffer = rhi::BufferHandle::kInvalid;
    }
    batch.index_count = 0;
}

} // namespace jrpgmaker::render
