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

    // Starts a fresh recording and clears every pipeline/resource binding from
    // the previous recording. Callers must bind all state required by Draw again.
    virtual void Begin() = 0;
    virtual void End() = 0;

    virtual void BeginRendering(TextureHandle color_target, const ClearColor& clear_color) = 0;
    virtual void EndRendering() = 0;

    virtual void SetPipeline(PipelineHandle handle) = 0;
    // Requires active rendering, a bound pipeline, and every resource declared
    // by that pipeline (vertex input, constants, sampled texture, and uniform).
    virtual void Draw(std::uint32_t vertex_count, std::uint32_t instance_count) = 0;

    // Binds a vertex buffer to input slot 0 (v0: single interleaved buffer).
    // `stride_bytes` must match the pipeline's vertex_input.stride_bytes.
    virtual void SetVertexBuffer(BufferHandle handle, std::uint32_t stride_bytes) = 0;
    // Binds an index buffer of uint16 or uint32 indices (glTF accessor types).
    // Requires the buffer to be created with BufferUsage::kIndex.
    virtual void SetIndexBuffer(BufferHandle handle, bool indices_are_32_bit) = 0;
    // Draws `index_count` indices; requires all Draw bindings plus a bound index
    // buffer and a pipeline with vertex input enabled.
    virtual void DrawIndexed(std::uint32_t index_count, std::uint32_t instance_count) = 0;

    // Uploads `size_bytes` of caller data into the push-constant block the
    // active pipeline declared (v0: bound to the vertex shader only). Must be
    // called after SetPipeline and before the next Draw. `size_bytes` must not
    // exceed the pipeline's declared constants size. D3D12 maps this to root
    // constants; Vulkan maps it to push constants. The layout must match the
    // shader's cbuffer/push-constant declaration byte-for-byte.
    virtual void SetPushConstants(const void* data, std::uint32_t size_bytes) = 0;

    // Binds a sampled texture and sampler to the active pipeline's sample slot
    // 0. Requires a pipeline created with sample_slot > 0. Must be called after
    // SetPipeline and before the next Draw. The texture must have been created
    // with TextureUsage::kSampled and uploaded via IDevice::UploadTexture.
    virtual void SetSampledTexture(TextureHandle texture, SamplerHandle sampler) = 0;

    // Binds the first `size_bytes` of a kUniform buffer to the active
    // pipeline's per-object vertex-uniform slot (v0: the skinned-mesh bone-matrix
    // array). Requires a pipeline created with vertex_uniform_size > 0 and
    // size_bytes <= that declared size. Must be called after SetPipeline and
    // before the next Draw. Write the buffer via IDevice::MapWrite beforehand.
    virtual void SetVertexUniformBuffer(BufferHandle handle, std::uint32_t size_bytes) = 0;

protected:
    ICommandList() = default;
};

} // namespace jrpgmaker::rhi
