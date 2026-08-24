#pragma once

#include <cstdint>

#include "jrpgmaker/rhi/descriptors.hpp"
#include "jrpgmaker/rhi/handles.hpp"

namespace jrpgmaker::rhi {

class ICommandList {
public:
    virtual ~ICommandList() = default;

    ICommandList(const ICommandList&) = delete;
    ICommandList& operator=(const ICommandList&) = delete;

    virtual void Begin() = 0;
    virtual void End() = 0;

    virtual void BeginRendering(TextureHandle color_target, const ClearColor& clear_color) = 0;
    virtual void EndRendering() = 0;

    virtual void SetPipeline(PipelineHandle handle) = 0;
    virtual void Draw(std::uint32_t vertex_count, std::uint32_t instance_count) = 0;

    // Binds a vertex buffer to input slot 0 (v0: single interleaved buffer).
    // `stride_bytes` must match the pipeline's vertex_input.stride_bytes.
    virtual void SetVertexBuffer(BufferHandle handle, std::uint32_t stride_bytes) = 0;
    // Binds an index buffer of uint16 or uint32 indices (glTF accessor types).
    // Requires the buffer to be created with BufferUsage::kIndex.
    virtual void SetIndexBuffer(BufferHandle handle, bool indices_are_32_bit) = 0;
    // Draws `index_count` indices; requires a bound index buffer and a pipeline
    // with vertex input enabled.
    virtual void DrawIndexed(std::uint32_t index_count, std::uint32_t instance_count) = 0;

protected:
    ICommandList() = default;
};

} // namespace jrpgmaker::rhi
