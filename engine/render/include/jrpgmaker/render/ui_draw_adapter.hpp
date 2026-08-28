#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "jrpgmaker/ui/draw_list.hpp"
#include "jrpgmaker/ui/editor_resources.hpp"
#include "jrpgmaker/rhi/command_list.hpp"
#include "jrpgmaker/rhi/device.hpp"

namespace jrpgmaker::render {

struct UiVertex {
    glm::vec2 position;
    glm::vec4 color;
};

struct UiViewport {
    float width = 0.0f;
    float height = 0.0f;
};

struct UiDrawDiagnostic {
    std::string code;
    std::size_t primitive_index = 0;
};

struct UiDrawPacket {
    std::vector<UiVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::string> text_keys;
    std::vector<UiDrawDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const { return diagnostics.empty(); }
};

struct UiGpuBatch {
    rhi::BufferHandle vertex_buffer = rhi::BufferHandle::kInvalid;
    rhi::BufferHandle index_buffer = rhi::BufferHandle::kInvalid;
    std::uint32_t index_count = 0;

    [[nodiscard]] bool empty() const { return index_count == 0; }
};

// Converts screen-space editor primitives into an ordered NDC upload packet.
// Text remains a key so glyph shaping and localization stay owned by the text
// pipeline; this adapter only owns primitive ordering and geometry conversion.
[[nodiscard]] UiDrawPacket BuildUiDrawPacket(const ui::DrawList& draw_list,
                                             const ui::EditorTheme& theme,
                                             UiViewport viewport);

// Uploads one validated packet. The returned buffers remain alive until the
// caller has submitted and waited for the command list, then DestroyUiGpuBatch
// must be called. An empty packet returns an empty batch without allocating.
[[nodiscard]] UiGpuBatch UploadUiDrawPacket(rhi::IDevice& device, const UiDrawPacket& packet);

// Records the common UI draw contract into an active rendering command list.
// The pipeline must use the UiVertex position/color layout and no resources
// beyond the vertex and index buffers.
void RecordUiDrawPacket(rhi::ICommandList& command_list, rhi::PipelineHandle pipeline,
                        const UiGpuBatch& batch);

void DestroyUiGpuBatch(rhi::IDevice& device, UiGpuBatch& batch);

} // namespace jrpgmaker::render
